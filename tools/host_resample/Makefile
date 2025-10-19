CC = gcc
CFLAGS = -O2 -Wall
all: host_resample

host_resample: host_resample.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f host_resample
