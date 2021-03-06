/*
 * airspy_yoga
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include <airspy.h>

#include "upd.h"
#include "yoga.h"

#define TAG "airspy_yoga"

struct param {
	int mode_capture;
	int short_ok;
	int lna_gain;
	int mix_gain;
	int vga_gain;
};

static struct param par;

/* Only accessed by the receiving thread, not locked. */
static struct rstate rs;

static pthread_mutex_t rx_mutex;
static pthread_cond_t rx_cond;

unsigned long sample_count;
unsigned long error_count;
struct timeval count_last;

struct cap1 {
	int bias;
	unsigned int len;	// in samples, not bytes
	unsigned char buf[];
};

struct pack1 {
	struct pack1 *next;
	unsigned int plen;
	unsigned char packet[112/8];

	int avg_p;
	unsigned long timed_n, timed_e;
};

struct cap1 *pcap;
unsigned int pcnt;
struct pack1 *phead, *ptail;

static void rstate_hunt(struct rstate *rsp);
static void packet_deliver(struct rstate *rsp);
static void packet_timer(struct rstate *rsp, unsigned long n, unsigned long e);

/*
 * We're treating the offset by 0x800 as a part of the DC bias.
 */
#define BVLEN  (128)
static unsigned int dc_bias = 0x800;

static void Usage(void) {
	fprintf(stderr, "Usage: airspy_yoga [-c pre|NNNN] [-S]"
            " [-ga lna_gain] [-gm mix_gain] [-gv vga_gain]\n");
	exit(1);
}

// Method Zero: direct calculation of the average
#if 1
static unsigned int bias_timer;
static unsigned int dc_bias_update(unsigned char *sp)
{
	int i;
	unsigned int sum;

	sum = 0;
	for (i = 0; i < BVLEN; i++) {
		sum += ((unsigned int) sp[1])<<8 | sp[0];
		sp += 2;
	}
	return sum / BVLEN;
}
#endif

// Method B: optimized with no loop
#if 0
static unsigned short bvec_b[BVLEN];
static unsigned int bvx_b;
static unsigned int bcur;
static inline unsigned int dc_bias_update_b(unsigned int sample)
{
	unsigned int bsub;

	bsub = bvec_b[bvx_b];
	bvec_b[bvx_b] = sample;
	bvx_b = (bvx_b + 1) % BVLEN;

	bcur -= bsub;	// overflows the unsigned, but it's all right
	bcur += sample;
	// if (bcur >= 0x1000)
	// 	return 0x800;
	return bcur / BVLEN;
}
static void dc_bias_init_b(void)
{
	int i;
	for (i = 0; i < BVLEN; i++)
		bvec_b[i] = dc_bias;
	bcur = dc_bias * BVLEN;
}
#endif

static int rx_callback(airspy_transfer_t *xfer)
{
	struct timeval now;
	unsigned char *sp;
	unsigned int sample;
	int value, p;
	int i;

#if 1 /* Method Zero */
	if (bias_timer == 0) {
		if (xfer->sample_count >= BVLEN) {
			sp = xfer->samples;
			dc_bias = dc_bias_update(sp);
		}
	}
	bias_timer = (bias_timer + 1) % 10;
#endif

	gettimeofday(&now, NULL);
	if (now.tv_sec >= count_last.tv_sec + 10) {
		packet_timer(&rs, sample_count, error_count);
		sample_count = 0;
		error_count = 0;
		count_last = now;
	}

	sp = xfer->samples;
	for (i = 0; i < xfer->sample_count; i++) {

		// You'll never believe it, but loading shorts like this
		// is not at all faster than the facilities of <endian.h>.
		// #include <endian.h>
		// unsigned short int sp;
		// sample = le16toh(*sp);
		sample = sp[1]<<8 | sp[0];
#if 0 /* Method B */
		dc_bias = dc_bias_update_b(sample);
#endif
		value = (int) sample - (int) dc_bias;
		p = upd_ate(&rs.smoo, abs(value));

		if (rs.state == HUNT) {
			if (++rs.dec >= DF) {
				if (preamble_match(&rs, p)) {
					rs.state = HALF;
					rs.data_len = 56;
					rs.bit_cnt = 0;
					memset(rs.packet, 0, 112/8);
				}
				rs.dec = 0;
			}
		} else if (rs.state == HALF) {
			if (++rs.dec >= SPB/2) {
				rs.p_half = p;
				rs.state = DATA;
				rs.dec = 0;
			}
		} else {
			if (++rs.dec >= SPB/2) {
				if (bit_decode(&rs, p) == 0) {
					if (++rs.bit_cnt >= rs.data_len) {
						if (rs.data_len == 56 &&
						    (rs.packet[0] & 0x80) != 0)
						{
							rs.data_len = 112;
							rs.state = HALF;
						} else {
							packet_deliver(&rs);
							rstate_hunt(&rs);
						}
					} else {
						rs.state = HALF;
					}
				} else {
					/*
					 * Not sure if we should skip
					 * up to data_len bits here.
					 * For simplicity, we just go to HUNT.
					 */
					rstate_hunt(&rs);
					pthread_mutex_lock(&rx_mutex);
					error_count++;
					pthread_mutex_unlock(&rx_mutex);
				}
				rs.dec = 0;
			}
		}

		sp += 2;
	}

	pthread_mutex_lock(&rx_mutex);
	sample_count += xfer->sample_count;
	pthread_mutex_unlock(&rx_mutex);

	// We are supposed to return -1 if the buffer was not processed, but
	// we don't see how this can ever be useful. What is the library
	// going to do with this indication? Stop the streaming?
	return 0;
}

/*
 * We're promiscuous with the manchester, by accepting any level change.
 * But we may change to only accept the levels used by preamble_match().
 */
int bit_decode(struct rstate *rsp, int p)
{
	unsigned char bit;

	/*
	 * Clearly bogus.
	 */
	if (rsp->p_half <= 0 || p <= 0)
		return -1;

	/*
	 * Manchester proper.
	 */
	if (rsp->p_half < p) {
		bit = 0;
	} else if (rsp->p_half > p) {
		bit = 1;
	} else {
		return -1;
	}

	/*
	 * Save the bit.
	 */
	rsp->packet[rsp->bit_cnt >> 3] |= bit << (7 - (rsp->bit_cnt & 07));

	return 0;
}

/*
 * Let's avoid triggering an erroneous match with stale samples.
 */
static void rstate_hunt(struct rstate *rsp)
{

	rsp->tx = 0;
	memset(rsp->tvec, 0, sizeof(struct track)*NT);

	rsp->state = HUNT;
}

static void packet_deliver(struct rstate *rsp)
{
	struct pack1 *pp;

	if (rsp->data_len < 112 && !par.short_ok)
		return;

	pp = malloc(sizeof(struct pack1));
	if (pp == NULL)
		return;
	memset(pp, 0, sizeof(struct pack1));

	pthread_mutex_lock(&rx_mutex);
	if (pcap == NULL) {

		pp->plen = rsp->data_len / 8;
		memcpy(pp->packet, rsp->packet, pp->plen);

		if (pcnt == 0) {
			phead = pp;
			ptail = pp;
		} else {
			ptail->next = pp;
			ptail = pp;
		}
		pcnt++;

		pthread_cond_broadcast(&rx_cond);
	} else {
		free(pp);
	}
	pthread_mutex_unlock(&rx_mutex);
}

static void packet_timer(struct rstate *rsp, unsigned long n, unsigned long e)
{
	struct pack1 *pp;

	pp = malloc(sizeof(struct pack1));
	if (pp == NULL)
		return;
	memset(pp, 0, sizeof(struct pack1));

	pthread_mutex_lock(&rx_mutex);
	if (pcap == NULL) {

		pp->plen = 0;
		pp->timed_n = n;
		pp->timed_e = e;
		/*
		 * This is obviously racy, rx_callback does not lock
		 * before updating rs.smoo. But it's okay for our purpose.
		 */
		pp->avg_p = UPD_CUR(&rsp->smoo);

		if (pcnt == 0) {
			phead = pp;
			ptail = pp;
		} else {
			ptail->next = pp;
			ptail = pp;
		}
		pcnt++;

		pthread_cond_broadcast(&rx_cond);
	}
	pthread_mutex_unlock(&rx_mutex);
}

/*
 * The trigger by signal level trips at a start of the interesting capture,
 * whereas the trigger by preamble detection is trips at its end. So, we
 * basically capture all history for "-c pre".
 */
#define CAPLEN     300
#define CAPBACK_L  100
#define CAPBACK_P  240

static int capvv[CAPLEN];
static int cappv[CAPLEN];
static int capx;
static int cap_timer;

static struct cap1 *rx_get_capture(void)
{
	struct cap1 *pc;
	unsigned int len = CAPLEN;
	unsigned int *pv, *pp;

	pc = malloc(sizeof(struct cap1) + 2 * len*sizeof(int));
	if (!pc)
		return NULL;

	pc->bias = dc_bias;
	pc->len = len;

	/* Buffer is used in full. We do this only to catch calculation bugs. */
	memset(pc->buf, 0, 2 * CAPLEN*sizeof(int));

	pv = (unsigned int*) pc->buf;
	pp = pv + CAPLEN;

	memcpy(pv, capvv+capx, (CAPLEN-capx)*sizeof(int));
	memcpy(pp, cappv+capx, (CAPLEN-capx)*sizeof(int));
	pv += (CAPLEN-capx);
	pp += (CAPLEN-capx);
	if (capx != 0) {
		memcpy(pv, capvv, capx*sizeof(int));
		memcpy(pp, cappv, capx*sizeof(int));
	}
	return pc;
}

static int rx_callback_capture(airspy_transfer_t *xfer)
{
	int i;
	unsigned char *sp;
	unsigned int sample;
	int value, p;
	int match;

#if 1 /* Method Zero */
	if (bias_timer == 0) {
		if (xfer->sample_count >= BVLEN) {
			sp = xfer->samples;
			dc_bias = dc_bias_update(sp);
		}
	}
	bias_timer = (bias_timer + 1) % 10;
#endif

	sp = xfer->samples;
	for (i = 0; i < xfer->sample_count; i++) {

		sample = sp[1]<<8 | sp[0];
#if 0 /* Method B */
		dc_bias = dc_bias_update_b(sample);
#endif
		value = (int) sample - (int) dc_bias;
		p = upd_ate(&rs.smoo, abs(value));

		if (par.mode_capture == -1) {

			match = preamble_match(&rs, p);

			capvv[capx] = value;
			cappv[capx] = p;
			capx = (capx + 1) % CAPLEN;

			if (match == 1) {
				if (cap_timer == 0)
					cap_timer = CAPLEN - CAPBACK_P;
			}

		} else if (par.mode_capture != 0) {

			capvv[capx] = value;
			cappv[capx] = p;
			capx = (capx + 1) % CAPLEN;

			if (p >= par.mode_capture) {
				if (cap_timer == 0)
					cap_timer = CAPLEN - CAPBACK_L;
			}
		}

		if (cap_timer != 0 && --cap_timer == 0) {
			pthread_mutex_lock(&rx_mutex);
			if (pcap == NULL) {
				pcap = rx_get_capture();
				pthread_cond_broadcast(&rx_cond);
			}
			pthread_mutex_unlock(&rx_mutex);
		}

		sp += 2;
	}

	return 0;
}

static void parse(struct param *p, char **argv) {
	char *arg;
	long lv;

	memset(p, 0, sizeof(struct param));
	p->lna_gain = 14;
	p->mix_gain = 12;
	p->vga_gain = 10;

	argv++;
	while ((arg = *argv++) != NULL) {
		if (arg[0] == '-') {
			switch (arg[1]) {
			case 'c':
				if ((arg = *argv++) == NULL || *arg == '-') {
					fprintf(stderr,
					    TAG ": missing -c threshold\n");
					Usage();
				}
				if (strcmp(arg, "pre") == 0) {
					p->mode_capture = -1;
				} else {
					lv = strtol(arg, NULL, 10);
					if (lv <= 0) {
						fprintf(stderr, TAG
						    ": invalid -c threshold\n");
						Usage();
					}
					p->mode_capture = lv;
				}
				break;
			case 'S':
				p->short_ok = 1;
				break;
			case 'g':
				/*
				 * These gain values are interpreted by the
				 * firmware. They may not be directly written
				 * into the registers of R820T2.
				 */
				if (arg[2] == 'a') {		// RF gain, LNA
					if ((arg = *argv++) == NULL || *arg == '-') {
						fprintf(stderr, TAG ": missing -ga value\n");
						Usage();
					}
					lv = strtol(arg, NULL, 10);
					if (lv < 0 || lv >= 14) {
						fprintf(stderr, TAG ": invalid -ga value\n");
						Usage();
					}
					p->lna_gain = lv;
				} else if (arg[2] == 'm') {	// Mixer gain
					if ((arg = *argv++) == NULL || *arg == '-') {
						fprintf(stderr, TAG ": missing -gm value\n");
						Usage();
					}
					lv = strtol(arg, NULL, 10);
					if (lv < 0 || lv >= 15) {
						fprintf(stderr, TAG ": invalid -gm value\n");
						Usage();
					}
					p->mix_gain = lv;
				} else if (arg[2] == 'v') {	// IF gain, VGA
					if ((arg = *argv++) == NULL || *arg == '-') {
						fprintf(stderr, TAG ": missing -gv value\n");
						Usage();
					}
					lv = strtol(arg, NULL, 10);
					if (lv < 0 || lv >= 15) {
						fprintf(stderr, TAG ": invalid -gv value\n");
						Usage();
					}
					p->vga_gain = lv;
				} else {
					Usage();
				}
				break;
			default:
				Usage();
			}
		} else {
			Usage();
		}
	}
}

int main(int argc, char **argv) {
	int rc;
	struct airspy_device *device = NULL;
	int (*rx_cb)(airspy_transfer_t *xfer);
	int i;

	pthread_mutex_init(&rx_mutex, NULL);
	pthread_cond_init(&rx_cond, NULL);
	upd_init(&rs.smoo, AVGLEN);
#if 0 /* Method B */
	dc_bias_init_b();
#endif

	parse(&par, argv);

	rc = airspy_init();
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, TAG ": airspy_init() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_init;
	}

	// open any device, result by reference
	rc = airspy_open(&device);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, TAG ": airspy_open() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_open;
	}

	rc = airspy_set_sample_type(device, AIRSPY_SAMPLE_RAW);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr,
		    TAG ": airspy_set_sample_type() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_sample;
	}

#if 0 /* We set to 20 million, index 0, which works on all firmware levels. */
	uint32_t samplerates_count;
	uint32_t *supported_samplerates;
	airspy_get_samplerates(device, &samplerates_count, 0);
	supported_samplerates = malloc(samplerates_count * sizeof(uint32_t));
	airspy_get_samplerates(device,
	    supported_samplerates, samplerates_count);

	/* P3 */
	printf("samplerate [%d]", samplerates_count);
	for (i = 0; i < samplerates_count; i++) {
		printf(" %u", supported_samplerates[i]);
	}
	printf("\n");
	free(supported_samplerates);
#endif
	// Setting by value fails on firmware v1.0.0-rc4, so set by index.
	// rc = airspy_set_samplerate(device, 20000000);
	rc = airspy_set_samplerate(device, 0);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr,
		    TAG ": airspy_set_samplerate() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_rate;
	}

#if 1 /* This needs firmware v1.0.0-rc6 or later. */
	// Packing: 1 - 12 bits, 0 - 16 bits
	rc = airspy_set_packing(device, 0);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, TAG ": airspy_set_packing() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_packed;
	}
#endif

	// Not sure why this is not optional
	rc = airspy_set_rf_bias(device, 0);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, TAG ": airspy_set_rf_bias() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_bias;
	}

	// rc = airspy_set_mixer_agc(device, 0);
	// if (rc < 0)
	// 	return rc;
	// rc = airspy_set_lna_agc(device, 0);
	// if (rc < 0)
	// 	return rc;

	// VGA is "Variable Gain Amplifier": the exit amplifier after mixer
	// and filter in R820T.
	//
	// Default in airspy_rx is 5; rtl-sdr sets 11 (26.5 dB) FWIW.
	// We experimented a little, and leave 12 for now.
	//
	// Register address: 0x0c
	// 0x80  unused, set 1
	// 0x40  VGA power:     0 off, 1 on
	// 0x20  unused, set 1
	// 0x10  VGA mode:      0 gain control by VAGC pin,
	//                      1 gain control by code in this register
	// 0x0f  VGA gain code: 0x0 -12 dB, 0xf +40.5 dB, with -3.5dB/step
	//
	// The software only transfers the value 0..15. Firmware sets the rest.
	rc = airspy_set_vga_gain(device, par.vga_gain);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr,
		    TAG ": airspy_set_vga_gain() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
	}

	// Mixer has its own gain. Not sure how that works, perhaps relative
	// to the oscillator signal.
	//
	// Default in airspy_rx is 5; rtl-sdr does 0x10 to enable auto.
	//
	// Register address: 0x07
	// 0x80  unused, set 0
	// 0x40  Mixer power:   0 off, 1 on
	// 0x20  Mixer current: 0 max current, 1 normal current
	// 0x10  Mixer mode:    0 manual mode, 1 auto mode
	// 0x0f  manual gain level
	//
	// The software only transfers the value 0..15. Firmware sets the rest.
	rc = airspy_set_mixer_gain(device, par.mix_gain);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr,
		    TAG ": airspy_set_mixer_gain() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
	}

	// The LNA is the pre-amp at the receive frequency before mixing.
	//
	// The default in airspy_rx is 1.
	//
	// Register address: 0x05
	// 0x80  Loop through:  0 on, 1 off  -- weird, backwards
	// 0x40  unused, set 0
	// 0x20  LNA1 Power:    0 on, 1 off
	// 0x10  Auto gain:     0 auto, 1 manual
	// 0x0F  manual gain level, 0 is min gain, 15 is max gain
	//
	// The software only transfers the value 0..14. Firmware sets the rest.
	rc = airspy_set_lna_gain(device, par.lna_gain);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr,
		    TAG ": airspy_set_lna_gain() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
	}

	if (par.mode_capture) {
		rx_cb = rx_callback_capture;
	} else {
		gettimeofday(&count_last, NULL);
		rx_cb = rx_callback;
	}
	rc = airspy_start_rx(device, rx_cb, NULL);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, TAG ": airspy_start_rx() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_start;
	}

	// No idea why the frequency is set after the start of the receiving
	rc = airspy_set_freq(device, 1090*1000000);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, TAG ": airspy_set_freq() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_freq;
	}

	if (par.mode_capture) {
		while (airspy_is_streaming(device)) {
			FILE *fp = stdout;
			struct cap1 *pc;
			int *vp, *pp;

			pthread_mutex_lock(&rx_mutex);
			pc = pcap;
			pcap = NULL;
			pthread_mutex_unlock(&rx_mutex);

			if (pc != NULL) {
				fprintf(fp, "# bias %d len %d\n",
				    pc->bias, pc->len);
				vp = (int *)pc->buf;
				pp = (int *)pc->buf + pc->len;
				for (i = 0; i < pc->len; i++) {
					fprintf(fp, " %4d %6d\n", vp[i], pp[i]);
				}
				fflush(fp);
				free(pc);
			}

			pthread_mutex_lock(&rx_mutex);
			if (pcap == NULL) {
				rc = pthread_cond_wait(&rx_cond, &rx_mutex);
				if (rc != 0) {
					pthread_mutex_unlock(&rx_mutex);
					fprintf(stderr,
					   TAG "pthread_cond_wait() failed:"
					   " %d\n", rc);
					exit(1);
				}
			}
			pthread_mutex_unlock(&rx_mutex);
		}
		airspy_stop_rx(device);
		airspy_close(device);
		airspy_exit();
		exit(0);
	}

	while (airspy_is_streaming(device)) {

		pthread_mutex_lock(&rx_mutex);
		while (pcnt) {
			struct pack1 *pp;

			--pcnt;
			pp = phead;
			phead = pp->next;
			pthread_mutex_unlock(&rx_mutex);

			if (pp->plen) {
				printf("*");
				for (i = 0; i < pp->plen; i++) {
					printf("%02x", pp->packet[i]);
				}
				printf(";\n");
			} else {
				printf("# samples %lu errors %lu avg_p %d\n",
				    pp->timed_n, pp->timed_e, pp->avg_p);
			}
			free(pp);

			pthread_mutex_lock(&rx_mutex);
		}
		pthread_mutex_unlock(&rx_mutex);

		pthread_mutex_lock(&rx_mutex);
		if (pcnt == 0) {
			rc = pthread_cond_wait(&rx_cond, &rx_mutex);
			if (rc != 0) {
				pthread_mutex_unlock(&rx_mutex);
				fprintf(stderr,
				   TAG "pthread_cond_wait() failed:"
				   " %d\n", rc);
				exit(1);
			}
		}
		pthread_mutex_unlock(&rx_mutex);
	}

	airspy_stop_rx(device);

	airspy_close(device);
	airspy_exit();
	return 0;

err_freq:
	airspy_stop_rx(device);
err_start:
err_bias:
err_packed:
err_rate:
err_sample:
	airspy_close(device);
err_open:
	airspy_exit();
err_init:
	return 1;
}
