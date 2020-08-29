/*
 * airspy_yoga
 * Preamble matching correlator
 */

#include <stdlib.h>

#define DEBUG  0

#if DEBUG
#include <stdio.h>
#include <string.h>
#endif

#include "yoga.h"

static const int pfun[APP] = {
	1, 0, 	// 0 ms
	1, 0, 	// 1 ms
	0, 0,	// 2 ms
	0, 1,	// 3 ms
	0, 1,	// 4 ms
	0, 0,	// 5 ms
	0, 0,	// 6 ms
	0, 0	// 7 ms
};

// value: the sample value with DC bias already subtracted
// return: the correlation (boolean for match or and -1 if decimated)
int preamble_match(struct rstate *rs, int value)
{
	struct track *tp;
	int p, sub, avg_p, thr_0, thr_1;
	int /* bool */ cor, step;
	int i, n;
#if DEBUG
	int v[APP], r[APP];
	int tx_saved;

	memset(v, 0, APP*sizeof(int));
	memset(r, 0, APP*sizeof(int));
#endif

	/*
	 * Pass through a smoother and use abs() to make compatible with
	 * the ideal function.
	 */
	p = avg_update(&rs->smoo, abs(value));

	if (++rs->dec < DF)
		return -1;
	rs->dec = 0;

#if DEBUG
	tx_saved = rs->tx;
#endif
	tp = &rs->tvec[rs->tx];
	rs->tx = (rs->tx + 1) % NT;

	sub = tp->t_p[tp->t_x];
	tp->t_p[tp->t_x] = p;
	tp->t_x = (tp->t_x + 1) % APP;
	AVG_UPD_P(&tp->ap_u, sub, p);
	avg_p = tp->ap_u / APP;

	/*
	 * Compute the correlator.
	 *
	 * Basically we reject anything that falls into a dead zone
	 * between two levels (thresholds) for pfun[i] == 0 and 1.
	 * Note that the avg(pfun[0:APP-1]) = 0.25 in our case.
	 *
	 * So, the upper threshold is 0.75 or avg_p*3 and the lower
	 * threshold happens to be equal to avg_p.
	 * For the upper threshold of 0.55 it is (avg_p*11)/5, and
	 * for lower threshold of 0.45 it is (avg_p*9)/5.
	 */

	// thr_0 = avg_p;	// 25%
	thr_0 = (avg_p*8)/5;	// 40%
	// thr_1 = 3*avg_p;	// 75%
	thr_1 = (avg_p*10)/5;	// 50%

	cor = 1;
	n = tp->t_x;
	for (i = 0; i < APP; i++) {
		p = tp->t_p[n];
#if DEBUG
		v[i] = p;
#endif

		if (pfun[i]) {
			step = (p >= thr_1);
		} else {
			step = (p < thr_0);
		}
#if DEBUG
		r[i] = step;
		cor &= step;
#else
		if (!step) {
			cor = 0;
			break;
		}
#endif

		n = (n + 1) % APP;
	}

#if DEBUG
	printf("avg %d band [%d:%d) tx %d", avg_p, thr_0, thr_1, tx_saved);
	for (i = 0; i < APP; i++) {
		printf(" %4d:%d", v[i], r[i]);
	}
	printf("\n");
#endif

	return cor;
}
