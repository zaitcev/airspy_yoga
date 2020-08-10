/*
 * airspy_yoga
 * A dumping ground of global definitions
 */

#define MAXUPD 32

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

// Decimation factor is 5. It's explicit for a band correlator.
#define DF    5

// APP is the number of averaged samples in the preamble.
#define APP  ((SPB/DF)*M)

// XXX experiment with both
// #define AVGLEN 3
#define AVGLEN 5

#if AVGLEN > MAXUPD
#error "No space for AVGLEN in upd"
#endif

#if APP > MAXUPD
#error "No space for M*2 in upd"
#endif
/* XXX Observe that t_p and ap_u.vec 100% duplicate each other. */
struct track {
	int tx;
	int t_p[APP];
	struct upd ap_u;
};

/*
 * The receiver state: the bank of tracks, the smoother, etc.
 */
struct rstate {
	struct upd smoo;	// a smoother
	struct track trk;	// only 1 track in the band correlator
	int dec;
};

int preamble_match(struct rstate *rsp, int value);
