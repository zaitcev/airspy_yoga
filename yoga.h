/*
 * airspy_yoga
 * A dumping ground of global definitions
 */

// PM is the number of bits in preamble, 8.
// XXX implement "-1st" or "9th" silent bit, check if more packets come in
#define M     8

// Samples per bit is 20 (for 20 Ms/s of real samples).
#define SPB  20

// Decimation factor is 5. It's explicit for a band correlator.
#define DF    5

// APP is the number of averaged samples in the preamble - 2 samples per 1 bit.
#define APP  (M*2)

// Number of tracks is only 2, basically one lucky and one unlucky.
#define NT    2

// Yes, averaging length is larger than DF. Could be up to 10 (the half-bit).
#define AVGLEN 7

struct track {
	int ap_u;
	int t_x;
	int t_p[APP];
};

/*
 * The receiver state: the bank of tracks, the smoother, etc.
 */
enum R_state { HUNT, HALF, DATA };
struct rstate {
	struct upd smoo;	// a smoother for half-bits
	int dec;
	enum R_state state;
	unsigned int tx;	// running index 0..NT-1
	struct track tvec[NT];
	int p_half;
	unsigned int data_len;	// expected length for HALF and DATA states
	unsigned int bit_cnt;
	unsigned char packet[112/8];
};

int preamble_match(struct rstate *rsp, int p);
int bit_decode(struct rstate *rsp, int p);
