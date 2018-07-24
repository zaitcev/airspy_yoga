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
static pthread_mutex_t rx_mutex;
// static pthread_cond_t rx_cond;

static void Usage(void) {
	fprintf(stderr, "Usage: airspy_yoga\n");
}

static int rx_callback(airspy_transfer_t *xfer)
{
	pthread_mutex_lock(&rx_mutex);
	sample_count += xfer->sample_count;
	// P3
	last_count = xfer->sample_count;
	memcpy(last_samples, xfer->samples, 16);
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
		printf("samples %lu/10\n", n);
		printf("last [%u]", last_count);
		for (i = 0; i < 16/2; i += 2) {
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
