/*
 * airspy_yoga
 * Preamble matching correlator
 */

#include <stdlib.h>

#include "yoga.h"

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

// value: the sample value with DC bias already subtracted
// return: the correlation
int preamble_match(struct rstate *rs, int value)
{
	struct track *tp;
	unsigned int tx1;
	int p;
	int avg_p;
	int cor;
	int i;

	/*
	 * Pass through a smoother and use abs() to make compatible with
	 * the ideal function.
	 */
	p = avg_update(&rs->smoo, AVGLEN, abs(value)) / AVGLEN;

	/*
	 * Save the current product into every track that's relevant, at an
	 * approprite position for its half-bit. Note that every track gets
	 * an appropriate update, as long as NT is wholly divisible by M*2.
	 */
	tx1 = rs->tx;
	for (i = 0; i < M*2; i++) {
		tp = &rs->tvec[tx1];
		tp->t_p[i] = p;
		tx1 = (tx1 + NT - SPB/2) % NT;
	}

	tp = &rs->tvec[rs->tx];
	avg_p = avg_update(&tp->ap_u, M*2, p) / M*2;

	/*
	 * Compute the correlator.
	 *
	 * Our ideal function is non-negative, with meaningful chunnks of
	 * zeroes. If we just compute Sigma(signal(t)*ideal(t)), it's not going
	 * to do us any good, because a constant signal is indistringuishable
	 * of impulses then. To make correlator distinguish anything, we offset
	 * everything by the average value.
	 */
	cor = 0;
	for (i = 0; i < M*2; i++) {
		int norm_pfun = pfun[i] * avg_p * 4 - avg_p;
		int norm_t_p = tp->t_p[i] - avg_p;
		cor += norm_t_p * norm_pfun;
	}

	// NT is not a power of 2.
	// tx = (tx + 1) % NT;			// with fast remainders
	// tx = (tx == NT-1) ? 0 : tx+1;	// modern CPU with cmov
	if (++rs->tx == NT) rs->tx = 0;		// with slow divisions

	return cor;
}
