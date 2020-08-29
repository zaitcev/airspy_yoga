/*
 * airspy_yoga
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <airspy.h>

#include "yoga.h"

#define TAG "airspy_yoga"

struct param {
	int mode_capture;
	int lna_gain;
};

static struct param par;

static pthread_mutex_t rx_mutex;
static pthread_cond_t rx_cond;

unsigned long sample_count;
unsigned int last_count;
unsigned int last_bias_a;
long last_dv_anal[5];

/* Only accessed by the receiving thread, not locked. */
static struct rstate rs;

struct cap1 {
	int bias;
	unsigned int len;	// in samples, not bytes
	unsigned char buf[];
};

struct cap1 *pcap;

/*
 * We're treating the offset by 0x800 as a part of the DC bias.
 */
#define BVLEN  (128)
static unsigned int dc_bias = 0x800;

static void Usage(void) {
	fprintf(stderr, "Usage: airspy_yoga [-c pre|NNNN]"
            " [-ga lna_gain]\n");
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
	int i;
	unsigned char *sp;
	unsigned int sample;
	int value;
	int dv;		// "discriminant value" - just a random name
	long dv_anal[5];

#if 1 /* Method Zero */
	if (bias_timer == 0) {
		if (xfer->sample_count >= BVLEN) {
			sp = xfer->samples;
			dc_bias = dc_bias_update(sp);
		}
	}
	bias_timer = (bias_timer + 1) % 10;
#endif

	memset(dv_anal, 0, sizeof(dv_anal));

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
		dv = preamble_match(&rs, value);
		if (dv < 0) {
			dv_anal[0]++;
		} else if (dv == 0) {
			dv_anal[1]++;
		} else if (dv == 1) {
			dv_anal[2]++;
		} else {
			dv_anal[3]++;
		}

		sp += 2;
	}

	pthread_mutex_lock(&rx_mutex);
	sample_count += xfer->sample_count;
	last_count = xfer->sample_count;
	last_bias_a = dc_bias;
	for (i = 0; i < 5; i++)
		last_dv_anal[i] += dv_anal[i];
	pthread_mutex_unlock(&rx_mutex);

	// We are supposed to return -1 if the buffer was not processed, but
	// we don't see how this can ever be useful. What is the library
	// going to do with this indication? Stop the streaming?
	return 0;
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
	int value;
	int p;

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

		if (par.mode_capture == -1) {

			p = preamble_match(&rs, value);

			capvv[capx] = value;
			cappv[capx] = p;
			capx = (capx + 1) % CAPLEN;

			if (p == 1) {
				if (cap_timer == 0)
					cap_timer = CAPLEN - CAPBACK_P;
			}

		} else if (par.mode_capture != 0) {

			p = avg_update(&rs.smoo, abs(value));

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
	p->lna_gain = 1;

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
			case 'g':
				if (arg[2] == 'a') {
					if ((arg = *argv++) == NULL || *arg == '-') {
						fprintf(stderr, TAG ": missing -ga value\n");
						Usage();
					}
					lv = strtol(arg, NULL, 10);
					if (lv < 0 || lv >= 16) {
						fprintf(stderr, TAG ": invalid -ga value\n");
						Usage();
					}
					p->lna_gain = lv | 0x10;
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
	unsigned long n;
	int i;

	pthread_mutex_init(&rx_mutex, NULL);
	pthread_cond_init(&rx_cond, NULL);
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

	// VGA is "Variable Gain Amplifier": the exit amplifier after mixer
	// and filter in R820T.
	//
	// default in airspy_rx is 5; rtl-sdr sets 11 (26.5 dB) FWIW.
	// We experimented a little, and leave 12 for now.
	//
	// 0x80  unused
	// 0x40  VGA power:     0 off, 1 on
	// 0x20  unused
	// 0x10  VGA mode:      0 gain control by VAGC pin,
	//                      1 gain control by code in this register
	// 0x0f  VGA gain code: 0x0 -12 dB, 0xf +40.5 dB
	rc = airspy_set_vga_gain(device, 12);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr,
		    TAG ": airspy_set_vga_gain() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
	}

	// airspy_rx default is 5; rtl-sdr does 0x10 to enable auto.
	//
	// 0x80  unused
	// 0x40  Mixer power:   0 off, 1 on
	// 0x20  Mixer current: 0 max current, 1 normal current
	// 0x10  Mixer mode:    0 manual mode, 1 auto mode
	// 0x0f  manual gain level
	rc = airspy_set_mixer_gain(device, 5);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr,
		    TAG ": airspy_set_mixer_gain() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
	}

	// airspy_rx default is 1
	//
	// 0x80  Loop through:  0 on, 1 off  -- weird, backwards
	// 0x40  unused
	// 0x20  LNA1 Power:    0 on, 1 off
	// 0x10  Auto gain:     0 auto, 1 manual
	// 0x0F  manual gain level
	rc = airspy_set_lna_gain(device, par.lna_gain);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr,
		    TAG ": airspy_set_lna_gain() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
	}
	// These two are alternative to the direct gain settings
	// rc = airspy_set_linearity_gain(device, linearity_gain_val);
	// rc = airspy_set_sensitivity_gain(device, sensitivity_gain_val);

	if (par.mode_capture) {
		rx_cb = rx_callback_capture;
	} else {
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

		sleep(10);

		pthread_mutex_lock(&rx_mutex);
		n = sample_count;
		sample_count = 0;
		pthread_mutex_unlock(&rx_mutex);

		printf("\n");  // for visibility
		printf("samples %lu/10 bias %u\n", n, last_bias_a);

		printf("dv analysis: skip %ld bad %ld good %ld error %ld\n",
		    last_dv_anal[0], last_dv_anal[1], last_dv_anal[2],
		    last_dv_anal[3]);
		pthread_mutex_lock(&rx_mutex);
		memset(last_dv_anal, 0, sizeof(last_dv_anal));
		pthread_mutex_unlock(&rx_mutex);

		printf("last [%u]\n", last_count);
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
