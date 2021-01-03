/*
 * Copyright (c) 2021 Pete Zaitcev <zaitcev@yahoo.com>
 *
 * THE PROGRAM IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. See file COPYING
 * for details.
 */

#include <math.h>
#include <stdlib.h>

#include "phasetab.h"
#include "xyphi.h"

double xy_phi_f(int x, int y)
{
	int x_abs, y_abs;
	int x_comp, y_comp;
	double phi;

	x_abs = abs(x);
	if (x_abs >= 2048)
		x_abs = 2047;
	y_abs = abs(y);
	if (y_abs >= 2048)
		y_abs = 2047;
	x_comp = com_tab[x_abs];
	y_comp = com_tab[y_abs];
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
	return phi;
}
