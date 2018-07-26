#
# pythagorean triangle generator
#

from __future__ import print_function

import math
import sys

import six

TAG="pythagen"

# Param

class ParamError(Exception):
    pass

class Param:
    def __init__(self, argv):
        skip = 0;  # Do skip=1 for full argv.
        #: Number of bits between 12 and 8
        self.bits = 12
        #: Mode (0 - default, 1 - naked sqrt())
        self.mode = 0
        for i in range(len(argv)):
            if skip:
                skip = 0
                continue
            arg = argv[i]
            if len(arg) != 0 and arg[0] == '-':
                if arg == "-b":
                    if i+1 == len(argv):
                        raise ParamError("Parameter -b needs an argument")
                    try:
                        self.bits = int(argv[i+1])
                    except ValueError:
                        raise ParamError("Invalid argument for -b (%s)" %
                                         argv[i+1])
                    skip = 1;
                elif arg == "-s":
                    self.mode = 1
                else:
                    raise ParamError("Unknown parameter " + arg)
            else:
                raise ParamError("Positional parameter supplied")
                self.repo = arg
        if not (7 <= self.bits <= 12):
            raise ParamError("Number of bits (-b) must be 7 .. 12")


def pyth(x, y):
    """
    :param x: an integer - not in the AirSpy format, just a normal integer
    :param y: ditto
    :returns: sqrt(x^2 + y^2), a float
    """
    return math.sqrt(float(x*x + y*y))

def apyth(nbits, i, j):
    """
    :param nbits: number of bits in the unsigned integer (location of the zero)
    :param i: an unsigned integer that is actually signed and offset by 2^nbits
    :param j: ditto
    """
    return pyth(i - (1 << (nbits-1)), j - (1 << (nbits-1)))

def main(args):
    try:
        par = Param(args)
    except ParamError as e:
        print(TAG+": ", e, file=sys.stderr)
        print("Usage:", TAG+" [-b nbits] [-s]", file=sys.stderr)
        return 1

    print("{")
    if par.mode == 0:
        n = 1 << par.bits
        for i in six.moves.range(n):
            print("  // %d" % i)
            for j in six.moves.range(n):
                k = int(apyth(par.bits, i, j))
                comma = "" if i == n-1 and j == n-1 else ","
                print("  %d%s" % (k, comma))
    else:
        n = 1 << par.bits
        for i in six.moves.range(n):
            k = int(math.sqrt(float(i)))
            comma = "" if i == n-1 else ","
            print("  %d%s" % (k, comma))

    print("}")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
