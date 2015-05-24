CC=gcc
CFLAGS= -Wall -g
LFLAGS= -lm

OBJS=simplefs.o main.o

app: $(OBJS)
	$(CC) $(OBJS) $(LFLAGS) -o app

main.o: main.c
	$(CC) $(CFLAGS) -c main.c -o main.o

simplefs.o: simplefs.c
	$(CC) $(CFLAGS) -c simplefs.c -o simplefs.o

clean:
	rm *.o 

