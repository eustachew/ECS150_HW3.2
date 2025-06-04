# Target library
lib := libfs.a
objs := fs.o disk.o

CC := gcc
CFLAGS := -Wall -Wextra
CFLAGS += -Werror
# CFLAGS += -g

all: $(lib)

$(lib): $(objs)
	ar rcs $@ $^

%.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(objs) $(lib)
