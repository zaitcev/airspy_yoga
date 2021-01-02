/*
 * Copyright (c) 2020 Pete Zaitcev <zaitcev@yahoo.com>
 *
 * THE PROGRAM IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. See file COPYING
 * for details.
 */
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include <airspy.h>

// #include "fec.h"
#include "upd.h"

#include "phasetab.h"

#define TAG "airspy_fm"

struct param {
	int mode_capture;
	int lna_gain;
	int mix_gain;
	int vga_gain;
	float fm_freq;	/* in MHz */
};

#define HGLEN 20
struct rx_state {
	struct upd uavg_i, uavg_q;
	unsigned long badx, bady;
	float prev_phi;
	unsigned long hgram[HGLEN];
	unsigned long hgram_e1, hgram_e2;
	float hgram_e2_save_d;
	int hgram_e2_save_x;
	int fm_cnt;
	unsigned long fm_e1, fm_e2;
};

struct rx_counts {
	unsigned long c_nocore;
	unsigned long c_bufdrop;
	unsigned long c_bufcnt;
};

struct packet {
	struct packet *next;
	int num;		// number of complex samples
	int *buf;		// XXX make these short
};

static int rx_state_init(struct rx_state *rsp, int avglen);
static void rx_state_fini(struct rx_state *rsp);
static void scan_buf(struct rx_state *rsp, struct packet *pp);
static void dump_buf(struct rx_state *rsp, struct packet *pp);
static void timer_print(
    unsigned long bufcnt, unsigned long bufdrop, unsigned long nocore,
    struct rx_state *rsp);
static void parse(struct param *p, char **argv);
static void Usage(void);
static int rx_callback(airspy_transfer_t *xfer);
static unsigned int dc_bias_update(unsigned char *sp);

static struct param par;

/*
 * We're treating the offset by 0x800 as a part of the DC bias.
 */
#define BVLEN  (128)
static unsigned int dc_bias = 0x800;
static unsigned int bias_timer;

#define AVGLEN 500	/* 20 KHz */

#define PMAX  20

static pthread_mutex_t rx_mutex;
static pthread_cond_t rx_cond;
unsigned int pcnt;
struct packet *phead, *ptail;
struct rx_counts c_stat;

int main(int argc, char **argv)
{
	struct airspy_device *device = NULL;
	int (*rx_cb)(airspy_transfer_t *xfer);
	int stop = 0;
	int cap_skip = 0;
	static struct rx_state rxstate;
	struct timeval count_last, now;
	int rc;

	parse(&par, argv);

	if (rx_state_init(&rxstate, AVGLEN) != 0) {
		fprintf(stderr, TAG ": upd_init() failed: No core\n");
		/* leaks a little bit but we're bailing anyway */
		goto err_upd;
	}

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

	rc = airspy_set_samplerate(device, 0);	// set by index, rate 20m
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr,
		    TAG ": airspy_set_samplerate() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_rate;
	}

	// Packing: 1 - 12 bits, 0 - 16 bits
	rc = airspy_set_packing(device, 0);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, TAG ": airspy_set_packing() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_packed;
	}

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

	rx_cb = rx_callback;
	rc = airspy_start_rx(device, rx_cb, NULL);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, TAG ": airspy_start_rx() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_start;
	}

	// No idea why the frequency is set after the start of receiving.
	rc = airspy_set_freq(device, par.fm_freq * 1000000.0);
	if (rc != AIRSPY_SUCCESS) {
		fprintf(stderr, TAG ": airspy_set_freq() failed: %s (%d)\n",
		    airspy_error_name(rc), rc);
		goto err_freq;
	}

	freopen(NULL, "wb", stdout);

	gettimeofday(&count_last, NULL);

	while (airspy_is_streaming(device) && !stop) {

		pthread_mutex_lock(&rx_mutex);
		while (pcnt) {
			struct packet *pp;

			--pcnt;
			pp = phead;
			phead = pp->next;
			pthread_mutex_unlock(&rx_mutex);

			if (par.mode_capture) {
				if (++cap_skip >= 30 && !stop) {
					dump_buf(&rxstate, pp);
					stop = 1;
					break;
				}
			} else {
				scan_buf(&rxstate, pp);
			}

			free(pp->buf);
			free(pp);

			pthread_mutex_lock(&rx_mutex);
			c_stat.c_bufcnt++;

			gettimeofday(&now, NULL);
			if (now.tv_sec >= count_last.tv_sec + 10) {
				unsigned long bufcnt, bufdrop, nocore;

				nocore = c_stat.c_nocore;
				bufdrop = c_stat.c_bufdrop;
				bufcnt = c_stat.c_bufcnt;
				memset(&c_stat, 0, sizeof(struct rx_counts));

				pthread_mutex_unlock(&rx_mutex);

				timer_print(bufcnt, bufdrop, nocore, &rxstate);

				count_last = now;
				pthread_mutex_lock(&rx_mutex);
			}
		}
		rc = pthread_cond_wait(&rx_cond, &rx_mutex);
		if (rc != 0) {
			pthread_mutex_unlock(&rx_mutex);
			fprintf(stderr,
			   TAG "pthread_cond_wait() failed:"
			   " %d\n", rc);
			exit(1);
		}
		pthread_mutex_unlock(&rx_mutex);
	}

	airspy_stop_rx(device);
	airspy_close(device);
	airspy_exit();

	rx_state_fini(&rxstate);
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
	rx_state_fini(&rxstate);
err_upd:
	return 1;
}

static int rx_state_init(struct rx_state *rsp, int avglen)
{

	if (upd_init(&rsp->uavg_i, avglen) != 0)
		goto err_i;
	if (upd_init(&rsp->uavg_q, avglen) != 0)
		goto err_q;
	rsp->prev_phi = 0.0;
	return 0;

err_q:
	upd_fini(&rsp->uavg_i);
err_i:
	return -1;
}

static void rx_state_fini(struct rx_state *rsp)
{
	upd_fini(&rsp->uavg_i);
	upd_fini(&rsp->uavg_q);
}

static void scan_buf(struct rx_state *rsp, struct packet *pp)
{
	const int *p;
	int i;
	int x, y;
	int x_comp, y_comp;
	float phi;
	float delta;
	int buck_x;

	p = pp->buf;
	for (i = 0; i < pp->num; i++) {

		/* I, Q */
		/*
		 * For FM, we use averaging as essential LPF.
		 */
		x = upd_ate(&rsp->uavg_i, p[0]);
		y = upd_ate(&rsp->uavg_q, p[1]);

		if (abs(x) >= 2048) {
			rsp->badx++;
			x = 0;
		}
		if (abs(y) >= 2048) {
			rsp->bady++;
			y = 0;
		}
		x_comp = com_tab[abs(x)];
		y_comp = com_tab[abs(y)];
		switch (((y < 0) << 1) + (x < 0)) {
		default:
			phi = phi_tab[y_comp][x_comp];
			break;
		case 1:
			phi = phi_tab[x_comp][y_comp] + M_PI*0.5;
			break;
		case 3:
			phi = phi_tab[y_comp][x_comp] + M_PI;
			break;
		case 2:
			phi = phi_tab[x_comp][y_comp] + M_PI*1.5;
		}

		delta = phi - rsp->prev_phi;
		if (delta < -1*M_PI)
			delta += 2*M_PI;
		if (delta >= M_PI)
			delta -= 2*M_PI;

		if (delta < -1*M_PI || delta >= M_PI) {
			rsp->hgram_e1++;
		} else {
			buck_x = (int)(((delta + M_PI) / (2*M_PI)) * HGLEN);
			if (buck_x < 0 || buck_x >= HGLEN) {	// never happens
				rsp->hgram_e2++;
				rsp->hgram_e2_save_d = delta;
				rsp->hgram_e2_save_x = buck_x;
			} else {
				rsp->hgram[buck_x]++;
			}
		}

		if (rsp->fm_cnt == 0 ||
		    rsp->fm_cnt == 417 ||
		    rsp->fm_cnt == 833) {
			int val;
			unsigned char lebuf[2];
			/*
			 * According to the Internet, the normal
			 * deviation for broadcast FM is 65 KHz.
			 * We start with 1000 KHz, a 20% of the Fc.
			 */
			val = (int) ((delta / (M_PI * (1000.0/5000.0))) * 32768);
			if (val < -32768) {
				rsp->fm_e1++;
				val = 0x8000;
			} else if (val >= 32767) {
				rsp->fm_e2++;
				val = 0x8000;
			}
			lebuf[0] = val & 0xFF;
			lebuf[1] = (val >> 8) & 0xFF;
			fwrite(lebuf, 2, 1, stdout);
		}
		if (++rsp->fm_cnt >= 1250) {
			rsp->fm_cnt = 0;
		}

		rsp->prev_phi = phi;

		p += 2;
	}
}

static void dump_buf(struct rx_state *rsp, struct packet *pp)
{
	const int *p;
	int i;
	int lim;

	p = pp->buf;
	lim = pp->num < 1000 ? pp->num : 1000;
	for (i = 0; i < lim; i++) {
		printf("%d %d\n", p[0], p[1]);
		p += 2;
	}
}

static void timer_print(
    unsigned long bufcnt,
    unsigned long bufdrop,
    unsigned long nocore,
    struct rx_state *rsp)
{
	int i;
	int avg_i, avg_q;
	FILE *ofp;

	avg_i = UPD_CUR(&rsp->uavg_i);
	avg_q = UPD_CUR(&rsp->uavg_q);

	fprintf(stderr, "# bufs %lu nocore %lu drop %lu"
	    " badx %lu bady %lu avg I %d Q %d"
	    " fme1 %lu fme2 %lu\n",
	    bufcnt, nocore, bufdrop, rsp->badx, rsp->bady,
	    avg_i, avg_q, rsp->fm_e1, rsp->fm_e2);

	rsp->badx = 0;
	rsp->bady = 0;
	rsp->fm_e1 = 0;
	rsp->fm_e2 = 0;

	/*
	 * The multi-line output is easy to dump into gnuplot for analysis.
	 * The gnuplot even ignores lines starting with '#'.
	 *
	 * The histogram is present, but goes to standard error,
	 * because the main output of FM receiver is the sound stream.
	 */
	ofp = stderr;
	fprintf(ofp, "# e1 %lu e2 %lu (d %f x %d)\n",
	    rsp->hgram_e1, rsp->hgram_e2,
	    rsp->hgram_e2_save_d, rsp->hgram_e2_save_x);
	for (i = 0; i < HGLEN; i++) {
		fprintf(ofp, " %f %lu\n",
		    (i + 0.5) * ((2*M_PI)/HGLEN), rsp->hgram[i]);
	}

	for (i = 0; i < HGLEN; i++)
		rsp->hgram[i] = 0;
	rsp->hgram_e1 = 0;
	rsp->hgram_e2 = 0;

	fflush(stderr);
}

static void parse(struct param *p, char **argv)
{
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
				/* if (strcmp(arg, "pre") == 0) */
				p->mode_capture = -1;
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
			p->fm_freq = strtof(arg, NULL);
			if (p->fm_freq < 60.0 || p->fm_freq >= 500.0) {
				fprintf(stderr,
				    TAG ": frequency  is out of range"
				    " (60.0 <= f < 500.0)\n");
				Usage();
			}
		}
	}
}

static void Usage(void)
{
	fprintf(stderr, "Usage: " TAG " [-c NNNN]"
            " [-ga lna_gain] [-gm mix_gain] [-gv vga_gain] 93.7\n");
	exit(1);
}

static int rx_callback(airspy_transfer_t *xfer)
{
	int i;
	unsigned char *sp;
	struct packet *pp;
	int *bp;

	if (bias_timer == 0) {
		if (xfer->sample_count >= BVLEN) {
			sp = xfer->samples;
			dc_bias = dc_bias_update(sp);
		}
	}
	bias_timer = (bias_timer + 1) % 10;

	/*
	 * Premature optimization is the root of all evil. -- D. Knuth
	 */
	bp = malloc(xfer->sample_count * sizeof(int));
	if (bp == NULL) {
		pthread_mutex_lock(&rx_mutex);
		c_stat.c_nocore++;
		pthread_mutex_unlock(&rx_mutex);
		return 0;
	}

	sp = xfer->samples;
	for (i = 0; i < xfer->sample_count; i += 4) {
		unsigned int sample;
		int value;

		/*
		 * This also decimates by two. We can re-insert the
		 * zeroes right back in if it's necessary later.
		 */

		sample = sp[1]<<8 | sp[0];
		value = (int) sample - (int) dc_bias;
		bp[i+0] = value;	// I(0) = cos(0 *pi/2) * x(0)

		sample = sp[3]<<8 | sp[2];
		value = (int) sample - (int) dc_bias;
		bp[i+1] = value * -1;	// Q(0) = -j*sin(1 * pi/2) * x(1)

		sample = sp[5]<<8 | sp[4];
		value = (int) sample - (int) dc_bias;
		bp[i+2] = value * -1;	// I(1) = cos(2 * pi/2) * x(2)

		sample = sp[7]<<8 | sp[6];
		value = (int) sample - (int) dc_bias;
		bp[i+3] = value;	// Q(1) = -j*sin(3 * pi/2) * x(3)

		sp += 8;
	}

	pp = malloc(sizeof(struct packet));
	if (pp == NULL) {
		free(bp);
		pthread_mutex_lock(&rx_mutex);
		c_stat.c_nocore++;
		pthread_mutex_unlock(&rx_mutex);
		return 0;
	}
	memset(pp, 0, sizeof(struct packet));
	pp->num = xfer->sample_count / 2;
	pp->buf = bp;

	pthread_mutex_lock(&rx_mutex);
	if (pcnt >= PMAX) {
		c_stat.c_bufdrop++;
		pthread_mutex_unlock(&rx_mutex);
		free(pp->buf);
		free(pp);
		return 0;
	}

	if (pcnt == 0) {
		phead = pp;
		ptail = pp;
	} else {
		ptail->next = pp;
		ptail = pp;
	}
	pcnt++;
	pthread_cond_broadcast(&rx_cond);
	pthread_mutex_unlock(&rx_mutex);

	return 0;
}

// Method Zero: direct calculation of the average (the fastest, strangely)
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
