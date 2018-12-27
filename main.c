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

unsigned long sample_count;
unsigned int last_count;
unsigned char last_samples[16];
unsigned int last_bias_a;
long last_anal[3];
static pthread_mutex_t rx_mutex;
// static pthread_cond_t rx_cond;

// PM is the number of bits in preamble, 8.
// XXX implement "-1st" or "9th" silent bit, check if more packets come in
#define M     8

// Samples per bit is 10 (for 20 Ms/S).
#define SPB  10

// XXX
#if 0
struct track {
	long sum;		// running sum
};

unsigned int tx;		// running index 0..M*SPB-1
struct track tvec[M*SPB];
#endif

/*
 * We're treating the offset by 0x800 as a part of the DC bias.
 */
#define BVLEN  (SPB*5)
// #define BVLEN 64
#if 0
// Method A:
static unsigned short bvec_a[BVLEN];
static unsigned int bvx_a;
#endif
static unsigned int dc_bias = 0x800;
static unsigned int bias_timer;

static const int pfun[M*SPB] = {
	1, 1, 1, 1, 1, 0, 0, 0, 0, 0,	// 0
	1, 1, 1, 1, 1, 0, 0, 0, 0, 0,	// 1
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	// 2
	0, 0, 0, 0, 0, 1, 1, 1, 1, 1,	// 3
	0, 0, 0, 0, 0, 1, 1, 1, 1, 1,	// 4
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	// 5
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	// 6
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0 	// 7
};

static void Usage(void) {
	fprintf(stderr, "Usage: airspy_yoga\n");
}

static void dc_bias_update(unsigned char *sp)
{
	int i;
	unsigned int sum;

	sum = 0;
	for (i = 0; i < BVLEN; i++) {
		sum += ((unsigned int) sp[1])<<8 | sp[0];
		sp += 2;
	}
	dc_bias = sum / BVLEN;
}

#if 0
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

/*
 * We aren't using the optimized method, because we only update the bias
 * occasionally now, so the improvement is a wash.
 */
#if 0
static unsigned short bvec_b[BVLEN];
static unsigned int bvx_b;
static unsigned int bcur;
static unsigned int dc_bias_update_b(unsigned int sample)
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
#endif

static int rx_callback(airspy_transfer_t *xfer)
{
	int i;
	unsigned char *sp;
	unsigned int sample;
	// int value;
	long anal[3];

	if (bias_timer == 0) {
		if (xfer->sample_count >= BVLEN) {
			sp = xfer->samples;
			dc_bias_update(sp);
		}
	}
	bias_timer = (bias_timer + 1) % 10;

	for (i = 0; i < 3; i++)
		anal[i] = 0;

	sp = xfer->samples;
	for (i = 0; i < xfer->sample_count; i++) {

		// You'll never believe it, but loading shorts like this
		// is not at all faster than the facilities of <endian.h>.
		// #include <endian.h>
		// unsigned short int sp;
		// sample = le16toh(*sp);
		sample = sp[1]<<8 | sp[0];

		// Something is not right, let's analyze the data a bit.
		if (sample == 0) {
			anal[0]++;
		} else if (sample >= 0x1000) {
			anal[2]++;
		} else {
			anal[1]++;
		}

		// value = (int) (sample - dc_bias);

		sp += 2;
	}

	pthread_mutex_lock(&rx_mutex);
	sample_count += xfer->sample_count;

	last_count = xfer->sample_count;
	memcpy(last_samples, xfer->samples, 16);

	last_bias_a = dc_bias;

	for (i = 0; i < 3; i++)
		last_anal[i] += anal[i];

	pthread_mutex_unlock(&rx_mutex);

	// We are supposed to return -1 if the buffer was not processed, but
	// we don't see how this can ever be useful. What is the library
	// going to do with this indication? Stop the streaming?
	return 0;
}

int main(int argc, char **argv) {
	int rc;
	struct airspy_device *device = NULL;
	unsigned long n;
	int i;

	pthread_mutex_init(&rx_mutex, NULL);
	// pthread_cond_init(&rx_cond, NULL);

	if (argc != 1)
		Usage();

	rc = airspy_init();
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_init() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_init;
	}

	// open any device, result by reference
	rc = airspy_open(&device);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_open() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_open;
	}

	rc = airspy_set_sample_type(device, AIRSPY_SAMPLE_RAW);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_set_sample_type() failed: %s (%d)\n",
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
		fprintf(stderr, "airspy_set_samplerate() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_rate;
	}

#if 1 /* This needs firmware v1.0.0-rc6 or later. */
	// Packing: 1 - 12 bits, 0 - 16 bits
	rc = airspy_set_packing(device, 0);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_set_packing() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_packed;
	}
#endif

	// Not sure why this is not optional
	rc = airspy_set_rf_bias(device, 0);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_set_rf_bias() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_bias;
	}

	// The gain values are those for r820t. No other amplifiers, it seems.
	//
	// default in airspy_rx is 5; rtl-sdr sets 11 (26.5 dB) FWIW.
	// rc = airspy_set_vga_gain(device, 10); // 0806 0807 0808 0805
	// rc = airspy_set_vga_gain(device, 11); // 0807 0809 07ff 0807
	rc = airspy_set_vga_gain(device, 12); // 080b 07fd 0803 0807
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_set_vga_gain() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
	}
	// airspy_rx default is 5; rtl-sdr does... 0x10? but it's masked, so...
	rc = airspy_set_mixer_gain(device, 5);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_set_mixer_gain() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
	}
	// airspy_rx default is 1
	rc = airspy_set_lna_gain(device, 1);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_set_lna_gain() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
	}
	// These two are alternative to the direct gain settings
	// rc = airspy_set_linearity_gain(device, linearity_gain_val);
	// rc = airspy_set_sensitivity_gain(device, sensitivity_gain_val);

	rc = airspy_start_rx(device, rx_callback, NULL);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_start_rx() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_start;
	}

	// No idea why the frequency is set after the start of the receiving
	rc = airspy_set_freq(device, 1090*1000000);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_set_freq() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_freq;
	}

	while (airspy_is_streaming(device)) {
		unsigned char samples[16];
		sleep(10);
		pthread_mutex_lock(&rx_mutex);
		n = sample_count;
		sample_count = 0;
		memcpy(samples, last_samples, 16);
		pthread_mutex_unlock(&rx_mutex);
		printf("samples %lu/10 bias %u\n", n, last_bias_a);
		printf("anal zero %ld other %ld over %ld\n",
		    last_anal[0], last_anal[1], last_anal[2]);
		for (i = 0; i < 3; i++)
			last_anal[i] = 0;
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
