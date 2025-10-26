#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <thread>

using namespace std;

#define BUFFER_SIZE 1024

void udpBroadcastListener(int udp_port)
{
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    
    sockaddr_in udp_addr{};
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons(udp_port);
    udp_addr.sin_addr.s_addr = INADDR_ANY;
    
    bind(udp_socket, (sockaddr*)&udp_addr, sizeof(udp_addr));
    
    char buffer[BUFFER_SIZE];
    while (true)
    {
        memset(buffer, 0, sizeof(buffer));
        sockaddr_in sender_addr{};
        socklen_t sender_len = sizeof(sender_addr);
        
        int bytes = recvfrom(udp_socket, buffer, BUFFER_SIZE - 1, 0, 
                            (sockaddr*)&sender_addr, &sender_len);
        
        if (bytes > 0)
        {
            buffer[bytes] = '\0';
            string msg = string(buffer) + "\n";
            write(1, msg.c_str(), msg.length());
        }
    }
    
    close(udp_socket);
}

int main(int argc, char* argv[])
{
    string serverIP = argv[1];
    int port = stoi(argv[2]);

    int client_socket = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, serverIP.c_str(), &server_addr.sin_addr);

    connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr));
    
    sockaddr_in local_addr{};
    socklen_t addr_len = sizeof(local_addr);
    getsockname(client_socket, (sockaddr*)&local_addr, &addr_len);
    int tcp_port = ntohs(local_addr.sin_port);
    int udp_port = tcp_port + 1;
    
    thread udp_thread(udpBroadcastListener, udp_port);
    udp_thread.detach();

    char buffer[BUFFER_SIZE];
    while (true)
    {
        memset(buffer, 0, sizeof(buffer));
        int bytes_read = read(0, buffer, BUFFER_SIZE - 1);
        if (bytes_read <= 0) break;
        if (buffer[bytes_read - 1] == '\n')
        {
            buffer[bytes_read - 1] = '\0';
            bytes_read--;
        }

        send(client_socket, buffer, bytes_read, 0);

        int bytes = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        string msg = string(buffer) + "\n";
        write(1, msg.c_str(), msg.length());
    }

    close(client_socket);
    return 0;
}