/*
 * airspy_yoga
 * Preamble matching correlator
 */

#include <stdlib.h>

#include "yoga.h"

#define M_2    (M*2)

static const int pfun[M_2] = {
	1, 0, 	// 0 ms
	1, 0, 	// 1 ms
	0, 0,	// 2 ms
	0, 1,	// 3 ms
	0, 1,	// 4 ms
	0, 0,	// 5 ms
	0, 0,	// 6 ms
	0, 0	// 7 ms
};

/*
 * Scale factor for pfun. It is chosen arbitrarily, so we have some
 * room for fixed point computations. F0(t) = AF4*pfun[t].
 * Note that the avg(pfun[0:M*2]) = AF0 = AF4/4.0 in our case.
 */
#define AF0     256
#define AF4  (AF0*4)

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
	 *
	 * XXX Since we only have 16 half-bit samples per track, each of
	 * tracks already has characteristics of decimation. We're only using
	 * 160 tracks for research purposes.
	 */
	tx1 = rs->tx;
	for (i = 0; i < M_2; i++) {
		tp = &rs->tvec[tx1];
		tp->t_p[i] = p;
		avg_update(&tp->ap_u, M_2, p);
		tx1 = (tx1 + NT - SPB/2) % NT;
	}

	/*
	 * Compute the correlator.
	 * We select the track where the just-arrived sample is the last
	 * to complete the line-up for the ideal function.
	 */
	tp = &rs->tvec[(rs->tx + SPB/2) % NT];
	avg_p = tp->ap_u.cur / M_2;
	if (avg_p == 0) {
		cor = 0;
	} else {
		cor = 0;
		for (i = 0; i < M_2; i++) {
			int norm_pfun = pfun[i] * AF4;
			int norm_t_p = tp->t_p[i] * AF0 / avg_p;
			if (norm_pfun < 0) {
				/* Add an AF4 value to avoid a good match. */
				cor += AF4;
				continue;
			}
			cor += abs(norm_t_p - norm_pfun);
		}
	}

	// NT is not a power of 2.
	// tx = (tx + 1) % NT;			// with fast remainders
	// tx = (tx == NT-1) ? 0 : tx+1;	// modern CPU with cmov
	if (++rs->tx == NT) rs->tx = 0;		// with slow divisions

	return cor;
}
