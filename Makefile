all: server client

server: airline_management_server.cpp
	g++ airline_management_server.cpp -o server.out

client: airline_management_client.cpp
	g++ airline_management_client.cpp -o client.out

clean:
	rm -f server.out client.out