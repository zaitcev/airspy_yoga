##
## airspy_yoga and friends
##

CFLAGS = -Wall -O2
LDFLAGS =
#CFLAGS = -Wall -g -pg
#LDFLAGS = -pg

# The libm and libpthread are not needed in the main binaries.
# The libpthread is implicit in glibc. We can add it if we ever run on BSD.
# The libm is only needed if math functions are used, which is typically
# by accident, so we leave it out to catch errors, and add it explicitly.
#LIBS = -lm -lpthread
LIBS =

# The default location for pkg is /usr/lib64/pkgconfig and /usr/share/pkgconfig
# but the airspy's cmak-ed stuff ends in /usr/local/lib/pkgconfig. So whatever.
# The libraries similarly go to a debianesque /usr/local/lib.
CFLAGS += -I/usr/local/include/libairspy
LDFLAGS += -L/usr/local/lib
LIBS_A = $(LIBS) -lairspy

# The phasetab.h rule is not atomic.
.DELETE_ON_ERROR:

all: airspy_fm airspy_yoga test_phi test_cor

airspy_fm: airspy_fm.o upd.o xyphi.o
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS_A}
airspy_yoga: main.o pre.o upd.o
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS_A}
test_phi: testphi.o xyphi.o
	${CC} -o $@ -g $^ -lm
test_cor: testcor.o  pre.o upd.o
	${CC} -o $@ $^

airspy_fm.o: airspy_fm.c upd.h xyphi.h
	${CC} ${CFLAGS} -c $<
main.o: main.c yoga.h
	${CC} ${CFLAGS} -c $<
pre.o: pre.c yoga.h
	${CC} ${CFLAGS} -c $<
upd.o: upd.c upd.h
	${CC} ${CFLAGS} -c $<
xyphi.o: xyphi.c xyphi.h phasetab.h
	${CC} ${CFLAGS} -c $<

phasetab.h:
	python3 phasegen.py -o phasetab.h

clean:
	rm -f airspy_fm airspy_yoga test_cor *.o
