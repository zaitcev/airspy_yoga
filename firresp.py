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
        #: Plot instead of output
        self.plot = False
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
                elif arg == "-p":
                    self.plot = True
                else:
                    raise ParamError("Unknown parameter " + arg)
            else:
                raise ParamError("Positional parameter supplied")

## Number of filter coefficients
#M = 9

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
# So, substituting w and splitting the complex exponent:
#    H(f) = sigma[m=0,M-1]( h(m)* exp(-j * 2*pi*(f/fs) * m)) =
#      = sigma[m=0,M-1]( h(m) * (cos(2*pi*(f/fs)*m) + -j * sin(2*pi*(f/fs)*m)))

def H(mvec, f, fs):
    M = len(mvec)
    Hi = 0.0
    Hq = 0.0
    for m in range(M):
        Hi += mvec[m] * math.cos(2*math.pi*(f/fs)*float(m))
        Hq += mvec[m] * math.sin(2*math.pi*(f/fs)*float(m))
    return math.sqrt(Hi*Hi + Hq*Hq)

def do_main(outfp, fs):

    # Lyons example with a pre-calculated result. Verified to match.
    #mvec = [0.2, 0.4, 0.4, 0.2]

    # A sample from https://docs.scipy.org/doc/scipy/reference/
    #
    # >>> from scipy import signal
    # >>> f1, f2 = 0.1, 0.2
    # >>> signal.firwin(9, [f1, f2], pass_zero=False)
    # array([-0.00838022,  0.01172673,  0.11313301,  0.27822046,  0.36236348,
    #         0.27822046,  0.11313301,  0.01172673, -0.00838022])
    #
    #mvec = [-0.00838022,  0.01172673,  0.11313301,  0.27822046,  0.36236348,
    #        0.27822046,  0.11313301,  0.01172673, -0.00838022]
    # -- nice but this is no bandpass? pass_zero didn't work?

    # >>> signal.firwin(9, [0.1, 0.2], pass_zero=True)
    # array([ 0.00339023, -0.00474407, -0.04576816, -0.11255459,  1.31935318,
    #        -0.11255459, -0.04576816, -0.00474407,  0.00339023])
    #mvec = [ 0.00339023, -0.00474407, -0.04576816, -0.11255459,  1.31935318,
    #   -0.11255459, -0.04576816, -0.00474407,  0.00339023]
    # -- yeah, pass_zero seems inverted and f1 and f2 mean something else
    # >>> signal.firwin(9, [0.3, 0.4], pass_zero=False)
    #mvec = [-0.01080607, -0.09547216, -0.14588197,  0.18279623,  0.46725795,
    #    0.18279623, -0.14588197, -0.09547216, -0.01080607]
    # >>> signal.firwin(9, [0.3, 0.4], pass_zero=True)
    #mvec = [ 0.00248756,  0.02197769,  0.03358202, -0.04207968,  0.96806483,
    #     -0.04207968,  0.03358202,  0.02197769,  0.00248756]
    # -- well this one has zero pass at 1.0, so...
    # >>> signal.firwin(9, [0.18, 0.33], pass_zero=False)
    mvec = [-0.03138033, -0.06681294, -0.00748198,  0.27316121,  0.45786652,
            0.27316121, -0.00748198, -0.06681294, -0.03138033]

    #mvec = [0.2, 0.4, 0.8, 0.8, 0.4, 0.2, 0.3, 0.5, 0.2]
    # -- ugly and lowpass
    #mvec = [0.9, -0.7, 0.6, -0.4, 0.4, -0.2, 0.2, -0.1, 0.1]
    # -- finally DC pass is not great, but still not what we want
    #mvec = [0.9, 0.1, -0.8, -0.1, 0.7, 0.1, -0.3, -0.1, 0.1]
    # -- yay bandpass, peak at 0.25*fs

    #mvec = [0.99, 0.1, 0.1, 0.1, 0.1]

    outx = []
    outy = []
    fn = 200   # number of plot points
    ## P3
    # fn = 10
    for n in range(fn):
        f = (fs / fn) * n
        h = H(mvec, f, fs)
        if outfp is None:
            outx.append(f)
            outy.append(h)
        else:
            outfp.write("%f %f\n" % (f, h))

    if outfp is None:

        import matplotlib
        import pylab

        # The gtk3 window does not look any different from the default Qt, but
        # it does not spew a bunch of dumb messages to the stderr like Qt does.
        # (MPLBACKEND=GTK3Agg python firresp.py -p)
        matplotlib.use('GTK3Agg')

        pylab.figure(1)
        pylab.title("H(f)")
        pylab.grid(True)
        pylab.plot(outx, outy)
        pylab.show()


def main(args):

    try:
        par = Param(args)
    except ParamError as e:
        print(TAG+": %s" % e, file=sys.stderr)
        print("Usage:", TAG+" [-p] [-o outfile] [-s Fs]", file=sys.stderr)
        return 1

    if par.plot:
        do_main(None, par.fs)
    else:
        if par.outname:
            with open(par.outname, 'w') as outfp:
                do_main(outfp, par.fs)
        else:
            do_main(sys.stdout, par.fs)

    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
