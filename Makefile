#
# airspy_yoga
#

CFLAGS = -Wall -O2
LDFLAGS =
#CFLAGS = -Wall -g -pg
#LDFLAGS = -pg

# The default location for pkg is /usr/lib64/pkgconfig and /usr/share/pkgconfig
# but the airspy's cmak-ed stuff ends in /usr/local/lib/pkgconfig. So whatever.
# The libraries similarly go to a debianesque /usr/local/lib.
CFLAGS += -I/usr/local/include/libairspy
LIBS += -lairspy -L/usr/local/lib

all: airspy_yoga test_cor

airspy_yoga: main.o pre.o upd.o
	${CC} ${LDFLAGS} -o airspy_yoga main.o pre.o upd.o ${LIBS}
test_cor: testcor.o  pre.o upd.o
	${CC} -o test_cor testcor.o pre.o upd.o

main.o: main.c yoga.h
	${CC} ${CFLAGS} -c -o main.o main.c
pre.o: pre.c yoga.h
	${CC} ${CFLAGS} -c -o pre.o pre.c
upd.o: upd.c yoga.h
	${CC} ${CFLAGS} -c -o upd.o upd.c

clean:
	rm -f airspy_yoga *.o
