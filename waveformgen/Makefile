CC=clang -std=gnu11 -pthread
CFLAGS=-I/usr/local/include -I/usr/include -I$(dir $(lastword $(MAKEFILE_LIST)))
LDFLAGS=-L/usr/local/lib -L/usr/lib -lavfilter -lavformat -lavcodec -lswscale -lswresample -lavutil -lmp3lame -lm
OBJECTS=main.o waveformgen.o
.SUFFIXES: .c .o

EXECUTABLE=wf

all: $(OBJECTS) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf *.o