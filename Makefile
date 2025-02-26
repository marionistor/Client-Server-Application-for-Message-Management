CC=g++ -Wall -g -Wextra -std=c++17

build: server subscriber

server: server.o utils.o
	$(CC) $^ -o $@

subscriber: subscriber.o utils.o
	$(CC) $^ -o $@

server.o: server.cpp
	$(CC) $^ -c

subscriber.o: subscriber.cpp
	$(CC) $^ -c

utils.o: utils.cpp
	$(CC) $^ -c

clean:
	rm -rf *.o server subscriber
