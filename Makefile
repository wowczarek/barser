CC=gcc
CFLAGS+=-std=c99 -Wall -I. -O3 -lrt

DEPS = rbt/fq.h rbt/st.h rbt/st_inline.h itoa.h barser.h barser_defaults.h
OBJ1 = rbt/fq.o rbt/st.o itoa.o barser.o barser_test.o
OBJ2 = rbt/fq.o rbt/st.o itoa.o barser.o barser_example.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)
all: barser_test barser_example

barser_test: $(OBJ1)
	$(CC) -o $@ $^ $(CFLAGS)
barser_example: $(OBJ2)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean

clean:
	rm -rf *.o *~ core barser_test barser_example rbt/*.o
