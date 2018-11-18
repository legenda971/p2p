all: server.o client.o database.o
	gcc -o server src/server.c -lpthread
	gcc -o database src/database.c
	gcc -o client src/client.c -lncurses -lm
	make clean

server.o: database.o
	gcc -o server.o src/server.c -lpthread

database.o:
	gcc -o database.o src/database.c

client.o:
	gcc -o client.o src/client.c -lncurses

clean:
	rm *.o