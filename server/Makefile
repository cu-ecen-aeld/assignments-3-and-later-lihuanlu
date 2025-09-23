CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc
CFLAGS= -O0 -g -Wall -Werror

# Default target
all:	aesdsocket

aesdsocket: aesdsocket.o
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $<
	
clean:
	rm -f *.o
	rm -f aesdsocket

