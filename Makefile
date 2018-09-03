#
# airspy_yoga
#

CFLAGS = -Wall -O2
LDFLAGS = -lpthread

# The default location for pkg is /usr/lib64/pkgconfig and /usr/share/pkgconfig
# but the airspy's cmak-ed stuff ends in /usr/local/lib/pkgconfig. So whatever.
# The libraries similarly go to a debianesque /usr/local/lib.
CFLAGS += -I/usr/local/include/libairspy
LIBS += -lairspy -L/usr/local/lib

airspy_yoga: main.o
	${CC} -o airspy_yoga main.o ${LIBS}

main.o: main.c
	${CC} ${CFLAGS} -c -o main.o main.c

clean:
	rm -f airspy_yoga *.o
