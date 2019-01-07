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

#define TAG "airspy_yoga"

struct param {
	int mode_capture;
	int lna_gain;
};

static struct param par;

unsigned long sample_count;
unsigned int last_count;
unsigned char last_samples[16];
unsigned int last_bias_a;
long last_anal[3];
long last_dv_anal[5];
static pthread_mutex_t rx_mutex;
static pthread_cond_t rx_cond;

// PM is the number of bits in preamble, 8.
// XXX implement "-1st" or "9th" silent bit, check if more packets come in
#define M     8

// Samples per bit is 20 (for 20 Ms/s of real samples).
#define SPB  20

#define PWRLEN 8
#define AVGLEN 3

struct upd {
	// Ouch. The lengths of averaging for power and average power are not
	// the same. But... We don't want to bother with allocation of variable
	// length structures for now. Maybe later XXX
	// int vec[max(PWRLEN, M*2)];
	int vec[M*2];
	unsigned int x;
	int cur;
};

struct track {
	int t_p[M*2];
	struct upd ap_u;
};

// N.B. see the comment below about NT needing to divide by M*2 (== SPB/2)
// NT == 8*20 == 160, M*2 = 8*2 = 16, SPB/2 = 20/2 = 10
#define NT  (M*SPB)	// XXX max resolution for now, will downsample later
unsigned int tx;		// running index 0..NT-1
struct track tvec[NT];

struct cap1 {
	int bias;
	unsigned int len;	// in samples, not bytes
	unsigned char buf[];
};

struct cap1 *pcap;

/*
 * We're treating the offset by 0x800 as a part of the DC bias.
 */
// XXX experiment with various BVLEN. Divide by 128 is faster than 100, right?
#define BVLEN  (128)
static unsigned int dc_bias = 0x800;

static const int pfun[M*2] = {
	1, 0, 	// 0 ms
	1, 0, 	// 1 ms
	0, 0,	// 2 ms
	0, 1,	// 3 ms
	0, 1,	// 4 ms
	0, 0,	// 5 ms
	0, 0,	// 6 ms
	0, 0	// 7 ms
};

static void Usage(void) {
	fprintf(stderr, "Usage: airspy_yoga [-c power_threshold]"
            " [-ga lna_gain]\n");
	exit(1);
}

// Method Zero: direct calculation of the average
#if 0
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

#if 0
// Method A:
static unsigned short bvec_a[BVLEN];
static unsigned int bvx_a;
static unsigned int dc_bias_update_a(unsigned int sample)
{
	int i;
	unsigned int sum;

	bvec_a[bvx_a] = sample;
	bvx_a = (bvx_a + 1) % BVLEN;
	sum = 0;
	for (i = 0; i < BVLEN; i++) {
		sum += bvec_a[i];
	}
	dc_bias = sub / BVLEN;
	return dc_bias;
}
#endif

#if 1
// Method B: optimized with no loop
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

static struct upd x_upd;	// a smoother

static inline int avg_update(struct upd *up, unsigned int len, int p)
{
	int sub;

	sub = up->vec[up->x];
	up->vec[up->x] = p;
	up->x = (up->x + 1) % len;

	up->cur -= sub;
	up->cur += p;
	if (up->cur < 0) {
		// XXX impossible, report this
		fprintf(stderr, TAG ": avg_update error, p %d x %d\n",
		    up->cur, up->x);
		up->cur = 0;
	}
	return up->cur;
}

// return: the discriminate value (smaller is better)
static inline int preamble_match(int value)
{
	struct track *tp;
	unsigned int tx1;
	int p;
	int avg_p;
	int dv;
	int i;

	// Calculate tracking average power over the few recent real samples.
	// The averaging distance must include a couple of periods. We're
	// doing this because we don't have I/Q. We're hoping for IF of 5 MHz
	// here, therefore at 20 Ms/s we need 8 samples. Probaby the more the
	// better... But we don't want to run much past the size of one half
	// of one bit, or our average power becomes useless to identify
	// the pulses of the preamble. See the PWRLEN.
	// XXX You know, finding the IF and doing "FS4" into I/Q
	// may just be better than this garbage.
	p = avg_update(&x_upd, PWRLEN, value*value) / PWRLEN;

	/*
	 * Save the current power into every track that's relevant, at an
	 * approprite position for its half-bit. Note that every track gets
	 * an appropriate update, as long as NT is wholly divisible by M*2.
	 */
	tx1 = tx;
	for (i = 0; i < M*2; i++) {
		tp = &tvec[tx1];
		tp->t_p[i] = p;
		tx1 = (tx1 + NT - SPB/2) % NT;
	}

	tp = &tvec[tx];
	avg_p = avg_update(&tp->ap_u, M*2, p) / M*2;
	dv = 0;
	for (i = 0; i < M*2; i++) {
		dv += tp->t_p[i] - avg_p * 4 * pfun[i];
	}

	/*
	 * Because of the intrinsic noise, stronger signals are going to have
	 * greater DV, but the lower is better. So, we normalize the DV itself
	 * too, but do it in a relative units that prevent underflow when
	 * calculating in integers.
	 */
	if (avg_p != 0) {
		dv = (dv * 10) / avg_p;
	}

	// NT is not a power of 2.
	// tx = (tx + 1) % NT;			// with fast remainders
	// tx = (tx == NT-1) ? 0 : tx+1;	// modern CPU with cmov
	if (++tx == NT) tx = 0;			// with slow divisions

	return dv;
}

static int rx_callback(airspy_transfer_t *xfer)
{
	int i;
	unsigned char *sp;
	unsigned int sample;
	int value;
	int dv;
	long anal[3];
	long dv_anal[5];

#if 0 /* Method Zero */
	if (bias_timer == 0) {
		if (xfer->sample_count >= BVLEN) {
			sp = xfer->samples;
			dc_bias = dc_bias_update(sp);
		}
	}
	bias_timer = (bias_timer + 1) % 10;
#endif

	memset(anal, 0, sizeof(anal));
	memset(dv_anal, 0, sizeof(dv_anal));

	sp = xfer->samples;
	for (i = 0; i < xfer->sample_count; i++) {

		// You'll never believe it, but loading shorts like this
		// is not at all faster than the facilities of <endian.h>.
		// #include <endian.h>
		// unsigned short int sp;
		// sample = le16toh(*sp);
		sample = sp[1]<<8 | sp[0];
#if 1 /* Method B */
		dc_bias = dc_bias_update_b(sample);
#endif

		// Something is not right, let's analyze the data a bit.
		if (sample == 0) {
			anal[0]++;
		} else if (sample < 0x1000) {
			anal[1]++;
		} else {
			anal[2]++;
		}

		value = (int) sample - (int) dc_bias;
		dv = preamble_match(value);

		if (dv < -100) {
			dv_anal[0]++;
		} else if (dv < 0) {
			dv_anal[1]++;
		} else if (dv == 0) {
			dv_anal[2]++;
		} else if (dv < 100) {
			dv_anal[3]++;
		} else {
			dv_anal[4]++;
		}

		sp += 2;
	}

	pthread_mutex_lock(&rx_mutex);
	sample_count += xfer->sample_count;

	last_count = xfer->sample_count;
	memcpy(last_samples, xfer->samples, 16);

	last_bias_a = dc_bias;

	for (i = 0; i < 3; i++)
		last_anal[i] += anal[i];
	for (i = 0; i < 5; i++)
		last_dv_anal[i] += dv_anal[i];

	pthread_mutex_unlock(&rx_mutex);

	// We are supposed to return -1 if the buffer was not processed, but
	// we don't see how this can ever be useful. What is the library
	// going to do with this indication? Stop the streaming?
	return 0;
}

#define CAPLEN   300
#define CAPBACK  100

static int capvv[CAPLEN];
static int cappv[CAPLEN];
static int capx;
static int cap_timer;

static int rx_callback_capture(airspy_transfer_t *xfer)
{
	int i;
	unsigned char *sp;
	unsigned int sample;
	int value;
	int p;

	sp = xfer->samples;
	for (i = 0; i < xfer->sample_count; i++) {

		sample = sp[1]<<8 | sp[0];
		dc_bias = dc_bias_update_b(sample);
		value = (int) sample - (int) dc_bias;
		p = avg_update(&x_upd, AVGLEN, abs(value)) / AVGLEN;

		capvv[capx] = value;
		cappv[capx] = p;
		capx = (capx + 1) % CAPLEN;

		if (p >= par.mode_capture) {
			if (cap_timer == 0)
				cap_timer = CAPLEN - CAPBACK;
		}

		if (cap_timer != 0 && --cap_timer == 0) {
			pthread_mutex_lock(&rx_mutex);
			if (pcap == NULL) {
				struct cap1 *pc;
				unsigned int len = CAPLEN;
				pc = malloc(sizeof(struct cap1) +
				     2 * len*sizeof(int));
				pc->bias = dc_bias;
				pc->len = len;
				memcpy(pc->buf, capvv, len*sizeof(int));
				memcpy(pc->buf + len*4, cappv, len*sizeof(int));
				pcap = pc;
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
				lv = strtol(arg, NULL, 10);
				if (lv <= 0) {
					fprintf(stderr,
					    TAG ": invalid -c threshold\n");
					Usage();
				}
				p->mode_capture = lv;
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
#if 1 /* Method B */
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
				fprintf(fp, "bias %d len %d\n",
				    pc->bias, pc->len);
				vp = (int *)pc->buf;
				pp = (int *)pc->buf + pc->len;
				for (i = 0; i < pc->len; i++) {
					printf(" %4d %6d\n", vp[i], pp[i]);
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
		unsigned char samples[16];

		sleep(10);

		pthread_mutex_lock(&rx_mutex);
		n = sample_count;
		sample_count = 0;
		memcpy(samples, last_samples, 16);
		pthread_mutex_unlock(&rx_mutex);

		printf("\n");  // for visibility
		printf("samples %lu/10 bias %u power %d\n", n, last_bias_a,
		    x_upd.cur);
		printf("s. analysis: zero %ld inrange %ld over %ld\n",
		    last_anal[0], last_anal[1], last_anal[2]);
		printf("dv analysis: -100 %ld neg %ld zero %ld pos %ld +100 %ld\n",
		    last_dv_anal[0], last_dv_anal[1], last_dv_anal[2],
		    last_dv_anal[3], last_dv_anal[4]);
		pthread_mutex_lock(&rx_mutex);
		memset(last_anal, 0, sizeof(last_anal));
		memset(last_dv_anal, 0, sizeof(last_dv_anal));
		pthread_mutex_unlock(&rx_mutex);

		printf("last [%u]", last_count);
		for (i = 0; i < 16; i += 2) {
			printf(" %04x", samples[i+1]<<8 | samples[i]);
		}
		printf("\n");

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
