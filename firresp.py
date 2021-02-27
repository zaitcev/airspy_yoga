#!/usr/bin/python3
# 
# Print a frequency response for a given FIR
#

import math
import sys

TAG="firresp"

class ParamError(Exception):
    pass

class Param:
    def __init__(self, argv):
        skip = 1;  # Do skip=1 for full argv.
        #: Output name, stdout if not given
        self.outname = None
        #: Sampling frequency in Hz (1e6 for RTL-SDR, 1e7 or 2e7 for AirSpy)
        self.fs = 1.0e6
        for i in range(len(argv)):
            if skip:
                skip = 0
                continue
            arg = argv[i]
            if len(arg) != 0 and arg[0] == '-':
                if arg == "-o":
                    if i+1 == len(argv):
                        raise ParamError("Parameter -o needs an argument")
                    self.outname = argv[i+1]
                    skip = 1;
                elif arg == "-s":
                    if i+1 == len(argv):
                        raise ParamError("Parameter -s needs an argument")
                    try:
                        self.fs = float(argv[i+1])
                    except ValueError:
                        raise ParamError("Invalid float argument for -s")
                    skip = 1;
                else:
                    raise ParamError("Unknown parameter " + arg)
            else:
                raise ParamError("Positional parameter supplied")

# Number of filter coefficients
M = 9

# Per Rob Frohne, KL7NA
#    h = [.... coefficients .....]
#    M = number of taps = len(h)
#    fs = sampling frequency
#    modH(f) = sigma[m=1,M/2]( 2*h[m] * sin((2*pi*m*f)/fs) )
# This is when using M symmetric coefficient from -M/2 to M/2.

# Per Richard Lyons, for complex signal and m in [0,M-1]:
#    H(w) = sigma[m=0,M-1]( h(m)*exp(-j*m*w) )
# where
#    w = [0, 2*pi / sample] or [0, 2*pi * (f/fs)]
# Assuming quadrature-only signal:
#    H(f) = sigma[m=0,M-1]( h(m)* sin(2*pi*(m/M)*(f/fs)) )
# Note that this is not modH(f). However, a sine is positive for
# f less than half of fs.

def H(mvec, f, fs):
    ## P3
    # print("f %f fs %f" % (f, fs))
    retH = 0.0
    for m in range(M):
        ## P3
        # print(" m %d sin %f" % (m, math.sin(2*math.pi*(float(m)/M)*(f/fs)),))
        retH += mvec[m] * math.sin(2*math.pi*(float(m)/M)*(f/fs))
    return retH

def do_main(outfp, fs):

    # mvec = [0.0]*M
    # Gauss with tip at 44% or 440 kHz
    #mvec = [0.2, 0.4, 0.8, 0.8, 0.4, 0.2, 0.3, 0.5, 0.2]
    # Gauss with tip at 35% or 370 kHz
    #mvec = [0.9, 0.9, 0.9, 0.9, 0.1, 0.1, 0.9, 0.9, 0.9]
    # Looks like an exponentially decreasing sine
    #mvec = [0.9, -0.7, 0.6, -0.4, 0.4, -0.2, 0.2, -0.1, 0.1]

    # all negaitve, with a linear segment near zero
    #mvec = [0.9, 0.1, -0.8, -0.1, 0.7, 0.1, -0.3, -0.1, 0.1]
    # The saddle is smaller than with -0.2
    #mvec = [0.9, 0.1, -0.8, -0.1, 0.7, 0.1, -0.3, -0.15, 0.1]
    # Whoosh... (all negateive)
    #mvec = [0.9, 0.1, -0.8, -0.1, 0.7, 0.1, -0.3, -0.2, 0.1]
    # sine!
    #mvec = [0.9, 0.1, -0.8, -0.1, 0.7, 0.1, -0.3, -0.3, 0.1]
    # sine!
    #mvec = [0.9, 0.1, -0.8, -0.1, 0.7, 0.1, -0.3, -0.4, 0.1]

    # Little positive near zero?
    #mvec = [0.9, 0.1, -0.8, -0.1, 0.7, 0.1, -0.3, -0.15, 0.2]
    # No difference at all! maybe zeroth coefficient does nothing.
    #mvec = [0.5, 0.1, -0.8, -0.1, 0.7, 0.1, -0.3, -0.15, 0.2]

    # Positive with a saddle ++
    #mvec = [99.99, 0.4, -0.5, -0.1, 0.7, 0.1, -0.3, -0.15, 0.2]
    # Wow, a big jump at the 1MHz side
    #mvec = [99.99, 0.4, -0.5, -0.1, 0.5, 0.1, -0.3, -0.15, 0.2]
    # Still flat, barely, but very wide
    #mvec = [99.99, 0.4, -0.5, -0.1, 0.67, 0.1, -0.3, -0.15, 0.2]
    # sags at the high
    #mvec = [99.99, 0.4, -0.5, -0.1, 0.67, 0.2, -0.3, -0.15, 0.2]
    # WTF, saddle went way down!
    #mvec = [99.99, 0.4, -0.5, -0.1, 0.67, -0.1, -0.3, -0.15, 0.2]
    # that's just worse than we started
    #mvec = [99.99, 0.4, -0.5, -0.1, 0.67, 0.15, -0.3, -0.15, 0.2]
    #
    #mvec = [99.99, 0.4, -0.5, -0.1, 0.67, 0.1, -0.3, -0.15, 0.2]
    # sags at the high again
    #mvec = [99.99, 0.4, -0.5, -0.1, 0.67, 0.1, -0.2, -0.15, 0.2]

    # Almost y = 0.8*x
    #mvec = [99.99, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    # sine max = 570 KHz
    #mvec = [99.99, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0]
    # yep that's a sine
    #mvec = [99.99, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0]
    # totally a sine, shortest period
    #mvec = [99.99, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0]
    #
    # sine(x) + 0.54*sine(2x)  -> sine with peak at 300 KHz
    #mvec = [99.99, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.54]

    # sine around 500 KHz +
    #mvec = [99.99, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0]

    # mvec = [99.99, -0.3, 0.0, 0.0, 1.0, 1.0, 0.0, 0.3, -0.3]

    # Sample from https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.firwin.html#scipy.signal.firwin
    mvec = [99.99,  0.06301614,  0.88770441,  0.06301614, 0.0, 0.0, 0.0, 0.0, 0.0]

    fn = 200   # number of plot points
    ## P3
    # fn = 10
    for n in range(fn):
        # We only go up to fs/2 because we'll smooth it all out in leu of LPF.
        # f = ((fs / 2) / fn) * n
        f = (fs / fn) * n
        h = H(mvec, f, fs)
        outfp.write("%f %f\n" % (f, h))

def main(args):

    try:
        par = Param(args)
    except ParamError as e:
        print(TAG+": %s" % e, file=sys.stderr)
        print("Usage:", TAG+" [-o outfile] [-s Fs]", file=sys.stderr)
        return 1

    if par.outname:
        with open(par.outname, 'w') as outfp:
            do_main(outfp, par.fs)
    else:
        do_main(sys.stdout, par.fs)

    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
