CC=gcc
CFLAGS+=-std=c99 -Wall -I. -O3 -lrt -g

DEPS = fq.h st.h st_inline.h barser.h barser_defaults.h
OBJ1 = fq.o st.o barser.o barser_test.o
OBJ2 = fq.o st.o barser.o barser_example.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)
all: barser_test barser_example

barser_test: $(OBJ1)
	$(CC) -o $@ $^ $(CFLAGS)
barser_example: $(OBJ2)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean

clean:
	rm -rf *.o *~ core barser_test barser_example
