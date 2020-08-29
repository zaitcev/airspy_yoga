/*
 * airspy_yoga
 * The "update thing": a device used to keep an average
 */

#include "yoga.h"

/*
 * The avg_update essentially has one more parameter: the length of AVGLEN.
 * We made it a constant in a desperate attempt at optimization according
 * to the results of profiling with gprof.
 */
int avg_update(struct upd *up, int p)
{
	int sub;
	unsigned int x;

	x = up->x;
	sub = up->vec[x];
	up->vec[x] = p;
	up->x = (x + 1) % AVGLEN;

	// AVG_UPD_P(&up->cur, sub, p);
	up->cur -= sub;
	up->cur += p;

	return up->cur / AVGLEN;
}
