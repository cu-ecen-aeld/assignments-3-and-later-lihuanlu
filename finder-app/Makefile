CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc

all:	writer

writer: writer.o
	$(CC) -o $@ $^

%.o: %.c
	$(CC) -c $<
	
clean:
	-rm -f *.o
	-rm -f writer

