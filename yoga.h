/*
 * airspy_yoga
 * A dumping ground of global definitions
 */

#define MAXUPD 16

struct upd {
	int cur;
	unsigned int x;
	int vec[MAXUPD];
};

int avg_update(struct upd *up, unsigned int len, int p);

// PM is the number of bits in preamble, 8.
// XXX implement "-1st" or "9th" silent bit, check if more packets come in
#define M     8

// Samples per bit is 20 (for 20 Ms/s of real samples).
#define SPB  20

#define AVGLEN 3

#if AVGLEN > MAXUPD
#error "No space for AVGLEN in upd"
#endif

#if M*2 > MAXUPD
#error "No space for M*2 in upd"
#endif
/* XXX Observe that t_p and ap_u.vec 100% duplicate each other. */
struct track {
	int t_p[M*2];
	struct upd ap_u;
};

// N.B. see the comment below about NT needing to divide by M*2 (== SPB/2)
// NT == 8*20 == 160, M*2 = 8*2 = 16, SPB/2 = 20/2 = 10
#define NT  (M*SPB)	// XXX max resolution for now, will downsample later

/*
 * The receiver state: the bank of tracks, the smoother, etc.
 */
struct rstate {
	struct upd smoo;	// a smoother
	unsigned int tx;	// running index 0..NT-1
	struct track tvec[NT];
};

int preamble_match(struct rstate *rsp, int value);
