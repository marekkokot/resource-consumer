CC=g++
CFLAGS = -O3 -std=c++20 -pthread -I spdlog/include  -I .

bin/resource-consumer: resource-consumer/main.cpp
	mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^
clean:
	rm -rf bin