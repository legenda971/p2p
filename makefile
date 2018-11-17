all: server.o
	gcc -o server src/server.c -lpthread
	gcc -o database src/database.c

server.o: database.o
	gcc -o server.o src/server.c -lpthread

database.o:
	gcc -o database.o src/database.c

clean:
	rm *.o server database