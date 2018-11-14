all: server.o
	gcc -o server src/server.c
	gcc -o database src/database.c

server.o: metadata.o database.o
	gcc -o server.o src/server.c

database.o: metadata.o
	gcc -o database.o src/database.c

metadata.o:
	gcc -o metadata.o src/metadata.c

clean:
	rm *.o server database