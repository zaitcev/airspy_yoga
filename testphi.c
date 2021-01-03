/*
 * Test of the arc-tangent
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "testphi"

#include "xyphi.h"

#if 0
struct xy {
	int x;
	int y;
};

struct xy samples[] = {
	{     0,     0 },
	{     1,     1 },
	{  2047,  2047 }
};
#endif

struct result {
	struct result *next, *prev;
	int x, y;
	double phi_lib, phi_test;
	double err; // == fabs(phi_test - phi_lib)
};

enum { RMAX = 20 };
unsigned int rcnt;
struct result *rhead, *rtail;

static void res_trim(void);

static void res_insert(struct result *r)
{
	struct result *p;

	for (p = rhead; p != NULL; p = p->next) {
		if (p->err < r->err) {
			if (p->prev != NULL) {
				p->prev->next = r;
				r->prev = p->prev;
			} else {
				rhead = r;
			}
			p->prev = r;
			r->next = p;
			if (++rcnt >= RMAX)
				res_trim();
			return;
		}
	}
	if (rcnt != 0) {
		p = rtail;
		p->next = r;
		r->prev = p;
		rtail = r;
	} else {
		rhead = r;
		rtail = r;
	}
	if (++rcnt >= RMAX)
		res_trim();
}

static void res_trim()
{
	struct result *p = rtail;

	if (rcnt <= 0)
		abort();
	--rcnt;
	if (rcnt == 0) {
		if (rhead != p)
			abort();
		if (rtail != p)
			abort();
		rtail = NULL;
		rhead = NULL;
	} else {
		if (p == NULL)
			abort();
		rtail = p->prev;
		rtail->next = NULL;
	}
	free(p);
}

static double xy_lib(int x, int y)
{
	/* XXX only two quadrants for now! */
	double frac;

	/*
	 * Usually, comparisons to equal are bugs in floating point math.
	 * But in this case we just want the test not to crash.
	 */
	if (x == 0.0) {
		if (y == 0.0)
			return 0.0;
		return (y < 0) ? M_PI*1.5 : M_PI*0.5;
	}

	if (x >= 0 && y >= 0) {
		frac = (double)y / (double)x;
		return atan(frac);
	} else if (x < 0 && y >= 0) {
		frac = (double)y / ((double)x * -1);
		return M_PI - atan(frac);
	} else if (x < 0 && y < 0) {
		frac = (double)y / (double)x;
		return M_PI + atan(frac);
	} else { /* x >= 0 && y < 0 */
		frac = ((double)y * -1) / (double)x;
		return 2.0*M_PI - atan(frac);
	}
}

int main(int argc, char **argv)
{
	int i;
	int x, y;
	double phi_test, phi_lib, phi_err;
	struct result *r;

	/*
	 * This tests both the "library" above and the lookup code,
	 * in case xy_lib() has a stitching issue.
	 */
	printf("Circle\n");
	printf(
	  "    X,    Y:    origin              library             computed\n");
	printf(
	  "-----,-----: --------- -------------------- --------------------\n");
	for (i = 0; i < 360; i += 30) {
		double phi = (2*M_PI * (double)i) / 360;
		x = (int) (cos(phi) * 1000);
		y = (int) (sin(phi) * 1000);
		phi_lib = xy_lib(x, y);
		phi_test = xy_phi_f(x, y);
		printf("%5d,%5d: %9f %9f (%f) %9f (%f)\n",
		    x, y, phi,
		    phi_lib, fabs(phi_lib - phi),
		    phi_test, fabs(phi_test - phi));
	}
	/*
	 */
	printf("Exhaustive\n");
	printf("    X,    Y:   library  computed\n");
	printf("-----,-----: --------- ---------\n");
#if 0
	for (i = 0; i < sizeof(samples)/sizeof(struct xy); i++) {
		x = samples[i].x;
		y = samples[i].y;
		phi_lib = xy_lib(x, y);
		phi_test = xy_phi_f(x, y);
		printf("%5d,%5d: %9f %9f\n", x, y, phi_lib, phi_test);
	}
#endif
	for (x = -2047; x < 2047; x++) {
		for (y = -2047; y < 2047; y++) {
			phi_lib = xy_lib(x, y);
			phi_test = xy_phi_f(x, y);
			phi_err = fabs(phi_test - phi_lib);
			if (rtail == NULL || rtail->err < phi_err) {
				r = malloc(sizeof(struct result));
				memset(r, 0, sizeof(struct result));
				r->x = x;
				r->y = y;
				r->phi_lib = phi_lib;
				r->phi_test = phi_test;
				r->err = phi_err;
				res_insert(r);
			}
		}
	}
	for (r = rhead; r != NULL; r = r->next) {
		printf("%5d,%5d: %9f %9f (%f)\n",
		    r->x, r->y, r->phi_lib, r->phi_test, r->err);
	}
	return 0;
}
