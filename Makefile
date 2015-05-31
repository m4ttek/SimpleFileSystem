CC=gcc
CFLAGS= -Wall -g
LFLAGS= -lm
TFLAGS= -lcunit

OBJS=simplefs.o main.o

TEST_OBJS=tests.o

all: app tests

app: $(OBJS)
	$(CC) $(OBJS) $(LFLAGS) -o app

tests: $(TEST_OBJS)
	$(CC) $(TEST_OBJS) $(TFLAGS) -o tests

main.o: main.c
	$(CC) $(CFLAGS) -c main.c -o main.o

simplefs.o: simplefs.c
	$(CC) $(CFLAGS) -c simplefs.c -o simplefs.o

tests.o: tests.c
	$(CC) $(CFLAGS) -c tests.c -o tests.o -lcunit

clean:
	rm *.o 

