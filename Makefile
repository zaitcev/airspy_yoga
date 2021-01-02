##
## airspy_yoga and friends
##

CFLAGS = -Wall -O2
LDFLAGS =
#CFLAGS = -Wall -g -pg
#LDFLAGS = -pg

# For some reason the explicit libm and libpthread are not needed yet again.
# They don't seem to hurt anything either, but let's leave the out for now.
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

all: airspy_fm airspy_yoga test_cor

airspy_fm: airspy_fm.o upd.o
	${CC} ${LDFLAGS} -o airspy_fm airspy_fm.o upd.o ${LIBS_A}
airspy_yoga: main.o pre.o upd.o
	${CC} ${LDFLAGS} -o airspy_yoga main.o pre.o upd.o ${LIBS_A}
test_cor: testcor.o  pre.o upd.o
	${CC} -o test_cor testcor.o pre.o upd.o

airspy_fm.o: airspy_fm.c upd.h phasetab.h
main.o: main.c yoga.h
pre.o: pre.c yoga.h
upd.o: upd.c upd.h

phasetab.h:
	python3 phasegen.py -o phasetab.h

clean:
	rm -f airspy_fm airspy_yoga test_cor *.o
