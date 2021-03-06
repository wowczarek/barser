CC=gcc
CFLAGS+=-std=c99 -Wall -I. -O3
LIBNAME = libbarser.a

LIBDEPS = rbt/fq.h rbt/st.h rbt/st_inline.h rbt/rbt.h xxh.h itoa.h linked_list.h  barser_index.h barser.h barser_defaults.h

LIBOBJ = rbt/fq.o rbt/st.o rbt/rbt.o itoa.o linked_list.o xxh.o barser_index_rbt.o barser.o

OBJ1 = barser_test.o
OBJ2 = barser_example.o
OBJ1_DEPLIBS = -lrt
OBJ2_DEPLIBS =

%.o: %.c $(LIBDEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(LIBNAME) barser_test barser_example

$(LIBNAME): $(LIBOBJ)
	ar rc $@ $^
	ranlib $@

barser_test: $(OBJ1) $(LIBNAME)
	$(CC) -o $@ $^ $(OBJ1_DEPLIBS) $(CFLAGS)

barser_example: $(OBJ2) $(LIBNAME)
	$(CC) -o $@ $^ $(OBJ2_DEPLIBS) $(CFLAGS)

.PHONY: fast
fast:
	$(MAKE) -j8 $(LIBNAME)
	$(MAKE) -j8 all

.PHONY: clean
clean:
	rm -rf *.o *~ core barser_test barser_example rbt/*.o $(LIBNAME)

remake: clean all

refast: clean fast

colldebug: CFLAGS += -DCOLL_DEBUG
colldebug: all

recolldebug: CFLAGS += -DCOLL_DEBUG
recolldebug: clean all

debug: CFLAGS += -g
debug: all

redebug: CFLAGS += -g
redebug: clean all
