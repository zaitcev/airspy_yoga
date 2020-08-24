/*
 * airspy_yoga
 * The "update thing": a device used to keep an average
 */

#include "yoga.h"

int avg_update(struct upd *up, unsigned int len, int p)
{
	int sub;
	unsigned int x;

	x = up->x;
	sub = up->vec[x];
	up->vec[x] = p;
	up->x = (x + 1) % len;

	up->cur -= sub;
	up->cur += p;
#if 0
	if (up->cur < 0) {	// we never use negative values, so...
		fprintf(stderr, TAG ": avg_update error, p %d x %d\n",
		    up->cur, up->x);
		up->cur = 0;
	}
#endif
	return up->cur;
}
