##
## airspy_yoga and friends
##

CFLAGS = -Wall -O2
LDFLAGS =
#CFLAGS = -Wall -g -pg
#LDFLAGS = -pg
LIBS =

# The default location for pkg is /usr/lib64/pkgconfig and /usr/share/pkgconfig
# but the airspy's cmak-ed stuff ends in /usr/local/lib/pkgconfig. So whatever.
# The libraries similarly go to a debianesque /usr/local/lib.
CFLAGS += -I/usr/local/include/libairspy
LDFLAGS += -L/usr/local/lib
LIBS_A = $(LIBS) -lairspy

all: airspy_yoga test_cor

airspy_yoga: main.o pre.o upd.o
	${CC} ${LDFLAGS} -o airspy_yoga main.o pre.o upd.o ${LIBS_A}
test_cor: testcor.o  pre.o upd.o
	${CC} -o test_cor testcor.o pre.o upd.o

main.o: main.c yoga.h
pre.o: pre.c yoga.h
upd.o: upd.c yoga.h

clean:
	rm -f airspy_yoga test_cor *.o
