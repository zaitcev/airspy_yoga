#
# segmented-precision square root generator
#
# This is hard-coded for a 23-bit positve number. So, the case of
# -2048:-2048 is prohibited. Conveniently, its representation is 0:0.
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
# We divide a 23 bit binary number into 3 segments of 9 bits each: #3, 2, 1.
# Then, we overlap them by two bits, for accuracy.
#
n3 = 9
b3 = 14
n2 = 9
b2 = 7
n1 = 9
b1 = 0
#
def asqrt(a3, a2, a1, x):
    x3 = (x >> b3) & ((1<<n3)-1)
    x2 = (x >> b2) & ((1<<n2)-1)
    x1 =        x  & ((1<<n1)-1)
    v3, m3 = a3[x3]
    v2, m2 = a2[x2]
    v1, m1 = a1[x1]
    return v3 | (m3 & (v2 | (m2 & v1)))

def main(args):

    # A great thing about our algorithm is, the segment tables are
    # very small: only 512 entries. So, we don't need to screw with
    # array types in Python.
    a3 = []

    for i in six.moves.range(1 << n3):
        k = int(math.sqrt(float(i << b3)))
        a3.append((k, 0))
    # For a3 and a2, there's an overlap by 1 bit.
    # The mask can be anything, as long as it masks.
    a3[0] = (0, 0xffff)
    a3[1] = (0, 0xffff)
    a3[2] = (0, 0xffff)
    a3[3] = (0, 0xffff)

    a2 = []
    for i in six.moves.range(1 << n2):
        k = int(math.sqrt(float(i << b2)))
        a2.append((k, 0))
    # For a2 and a1, there's an overlap by 2 bits.
    a2[0] = (0, 0xffff)
    a2[1] = (0, 0xffff)
    a2[2] = (0, 0xffff)
    a2[3] = (0, 0xffff)

    a1 = []
    for i in six.moves.range(1 << n1):
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
        print("%d(0x%x=[%x,%x,%x]): %d (%d %f)" %
              (x, x,
               x>>b3 & ((1<<n3)-1),
               x>>b2 & ((1<<n2)-1),
               x>>b1 & ((1<<n1)-1),
               k, k0, float(k)/float(k0)))

    # now the test
    smallest_fraction = 1.0
    smallest_num = None
    smallest_sqrt = None
    smallest_sqrt0 = None
    largest_fraction = 1.0
    largest_num = None
    largest_sqrt = None
    largest_sqrt0 = None
    for i in six.moves.range(1 << 23):
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
            largest_sqrt0 = k0
        if f < smallest_fraction:
            smallest_fraction = f
            smallest_num = i
            smallest_sqrt = k
            smallest_sqrt0 = k0

    print("smallest fraction %s asqrt(%s)=%s vs %s" % (
           smallest_fraction, smallest_num, smallest_sqrt, smallest_sqrt0))
    print("largest fraction %s asqrt(%s)=%s" %
          (largest_fraction, largest_num, largest_sqrt))

    # comma = "" if i == n-1 else ","
    # print("  %d%s" % (k, comma))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
