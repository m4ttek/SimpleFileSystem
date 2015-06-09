CC=gcc
CFLAGS= -Wall -g -w
LFLAGS= -lm
TFLAGS= -lcunit -lm

OBJS=simplefs.o main.o

TEST_OBJS=tests.o simplefs.o

all: app tests concurrent_write_fork concurrent_write_init concurrent_write_unlink_fork concurrent_creat_fork



concurrent_creat_fork: concurrent_creat_fork.o simplefs.o
	$(CC) concurrent_creat_fork.o simplefs.o $(LFLAGS) -o ccfork
concurrent_write_fork: concurrent_write_fork.o simplefs.o
	$(CC) concurrent_write_fork.o simplefs.o $(LFLAGS) -o cwfork
concurrent_write_unlink_fork: concurrent_write_unlink_fork.o simplefs.o
	$(CC) concurrent_write_unlink_fork.o simplefs.o $(LFLAGS) -o cwunfork
concurrent_write_init: concurrent_write_init.o simplefs.o
	$(CC) concurrent_write_init.o simplefs.o $(LFLAGS) -o cwinit

concurrent_creat_fork.o: concurrent_creat_fork.c
	$(CC) $(CFLAGS) -c concurrent_creat_fork.c -o concurrent_creat_fork.o

concurrent_write_unlink_fork.o: concurrent_write_unlink_fork.c
	$(CC) $(CFLAGS) -c concurrent_write_unlink_fork.c -o concurrent_write_unlink_fork.o

concurrent_write_fork.o: concurrent_write_fork.c
	$(CC) $(CFLAGS) -c concurrent_write_fork.c -o concurrent_write_fork.o

concurrent_write_init.o: concurrent_write_init.c
	$(CC) $(CFLAGS) -c concurrent_write_init.c -o concurrent_write_init.o



app: $(OBJS)
	$(CC) $(OBJS) $(LFLAGS) -o app

tests: $(TEST_OBJS)
	$(CC) $(TEST_OBJS) $(TFLAGS) -o tests

main.o: main.c
	$(CC) $(CFLAGS) -c main.c -o main.o

simplefs.o: simplefs.c
	$(CC) $(CFLAGS) -c simplefs.c -o simplefs.o

tests.o: tests.c
	$(CC) $(CFLAGS) -c tests.c -o tests.o

clean:
	rm *.o tests app

