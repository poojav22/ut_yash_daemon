all: 
	gcc -pthread server.c -o server
	gcc client.c -o client

clean:
	rm server
	rm client
 
