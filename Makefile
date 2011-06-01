
CC=g++
CFLAGS=-Wall

all: berdump

OBJS += berdump.o

berdump: $(OBJS)
	$(CC) $(OBJS) -o berdump

test: berdump
	@sh runtest.sh

.cc.o:
	$(CC) $(CFLAGS) -c $*.cc

clean:
	rm -f berdump *.o

