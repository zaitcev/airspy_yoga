/*
 * airspy_adsb
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <airspy.h>

#define TAG "airspy_adsb"

/* XXX take a posix mutex */
volatile unsigned long sample_count;


static void Usage(void) {
	fprintf(stderr, "Usage: airspy_adsb\n");
}

static int rx_callback(airspy_transfer_t *xfer)
{
	sample_count += xfer->sample_count;

	// We are supposed to return -1 if the buffer was not processed, but
	// we don't see how this can ever be useful. What is the library
	// going to do with this indication? Stop the streaming?
	return 0;
}

int main(int argc, char **argv) {
	int rc;
	struct airspy_device *device = NULL;
	uint32_t *supported_samplerates;
	uint32_t samplerates_count;
	unsigned long n;
	int i;

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

	// Setting by value fails with AIRSPY_ERROR_LIBUSB, on sameple code too
	// rc = airspy_set_samplerate(device, 20000000);
	rc = airspy_set_samplerate(device, 0);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_set_samplerate() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_rate;
	}

	// Optional: 1 - 12 bits, 0 - 16 bits
	// rc = airspy_set_packing(device, 0);

	// Not sure why this is not optional
	rc = airspy_set_rf_bias(device, 0);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_set_rf_bias() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_bias;
	}

	// Apparently, all of the gain settings are optional, but we don't
	// know what exacty happens if one omits them. Magic values, too.
	rc = airspy_set_vga_gain(device, 5);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_set_vga_gain() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
	}
	rc = airspy_set_mixer_gain(device, 5);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_set_mixer_gain() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
	}
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
		sleep(10);
		n = sample_count;
		sample_count = 0;
		printf("%lu\n", n);
	}

	airspy_stop_rx(device);

	free(supported_samplerates);
	airspy_close(device);
	airspy_exit();
	return 0;

err_freq:
	airspy_stop_rx(device);
err_start:
err_bias:
err_rate:
	free(supported_samplerates);
err_sample:
	airspy_close(device);
err_open:
	airspy_exit();
err_init:
	return 1;
}
