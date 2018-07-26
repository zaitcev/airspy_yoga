#
# segmented-precision square root generator
#
# This is hard-coded for a 24-bit positve number.
# In our case, we only need 23 bits, but technically we still allow the
# number 0x8000000 or 8388608, when both and I and Q equal -2048.
# (probably should prohibit it when parsing the buffer, actually XXX)
#

from __future__ import print_function

import math
import sys

import six

TAG="sqrtgen"

# This is a test function that does the same thing our C code does,
# using the tables that we generated.
#
# The formula is:
#   sqrt = v3 + m3*(v2 + m2*(v1))
#
# We use masking instead of multiplication, and or instead of addition,
# because it's more fun.
#
def asqrt(a3, a2, a1, x):
    x3 = (x >> 16) & 0xff
    x2 = (x >> 8) & 0x1ff
    x1 = x & 0x1ff
    v3, m3 = a3[x3]
    v2, m2 = a2[x2]
    v1, m1 = a1[x1]
    return v3 | (m3 & (v2 | (m2 & v1)))

def main(args):

    # We divide a 24 bit binary number into 3 segments of 8 bits each.
    # Then, we overlap by 1 bit (see the code).
    nb = 8

    # A great thing about our algorithm is, the segment tables are
    # very small: only 512 entries (256 for a3). So, we don't need
    # to screw with array types.
    a3 = []

    for i in six.moves.range(1 << nb):
        k = int(math.sqrt(float(i << nb*2)))
        a3.append((k, 0))
    for i in range(2):
        # The mask can be anything, as long as it masks.
        a3[i] = (0, 0xffff)

    a2 = []
    # For a2 and a1, there's an overlap by 1 bit.
    for i in six.moves.range(1 << (nb+1)):
        k = int(math.sqrt(float(i << nb*1)))
        a2.append((k, 0))
    for i in range(2):
        a2[i] = (0, 0xffff)

    a1 = []
    for i in six.moves.range(1 << (nb+1)):
        k = int(math.sqrt(float(i)))
        # The mask of a1 is not used.
        a1.append((k, 0))

    # sampler
    for a in a3:
        print("%d %d" % (a[0], a[1]))
    for a in a2:
        print("%d %d" % (a[0], a[1]))
    for a in a1:
        print("%d" % (a[0],))
    for x in [5, 50, 500, 5000, 50000, 500000, 5000000]:
        k0 = int(math.sqrt(float(x)))
        k = asqrt(a3, a2, a1, x)
        print("%d (%d %f)" % (k, k0, float(k)/float(k0)))

    # now the test
    largest_fraction = 1.0
    largest_num = None
    largest_sqrt = None
    smallest_fraction = 1.0
    smallest_num = None
    smallest_sqrt = None
    for i in six.moves.range(1 << 24):
        k0 = int(math.sqrt(float(i)))
        k = asqrt(a3, a2, a1, i)
        if k0 == 0:
            if k != 0:
                f = 1000000.0
            else:
                f = 1.0
        else:
            f = float(k)/float(k0)
        if f > largest_fraction:
            largest_fraction = f
            largest_num = i
            largest_sqrt = k
        if f < smallest_fraction:
            smallest_fraction = f
            smallest_num = i
            smallest_sqrt = k

    print("smallest fraction %s asqrt(%s)=%s" % (
           smallest_fraction, smallest_num, smallest_sqrt))
    print("largest fraction %s asqrt(%s)=%s" %
          (largest_fraction, largest_num, largest_sqrt))

    # comma = "" if i == n-1 else ","
    # print("  %d%s" % (k, comma))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
