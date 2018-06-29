/*
 * airspy_adsb
 */

#include <stdio.h>

#include <airspy.h>

#define TAG "airspy_adsb"

void Usage(void) {
	fprintf(stderr, "Usage: airspy_adsb\n");
}

int main(int argc, char **argv) {
	int rc;
	struct airspy_device *device = NULL;

	if (argc != 1)
		Usage();

	rc = airspy_init();
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_init() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		return 1;
	}

	// open any device, result by reference
	rc = airspy_open(&device);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_open() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		return 1;
	}

	return 0;
}
