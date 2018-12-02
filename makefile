all:
	gcc -o server src/server.c -lpthread -g
	gcc -o database src/database.c -g
	gcc -o client src/client.c -lncurses -lm -g -lpthread

server:
	gcc -o server.o src/server.c -lpthread

database:
	gcc -o database.o src/database.c

client:
	gcc -o client src/client.c -lncurses -lm -g

clean:
	rm *.o