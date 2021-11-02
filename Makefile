CC=gcc
CFLAGS=-Wall -Werror -O2 -g
OBJ= oss.o user.o
DEPS= oss.c user.c

all: oss user

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<
oss: $(OBJ)
	$(CC) $(CFLAGS) -lm -o $@ $@.o

user: $(OBJ)
	$(CC) $(CFLAGS) -lm -o $@ $@.o
clean:
	rm -rf oss user *.log *.o 
