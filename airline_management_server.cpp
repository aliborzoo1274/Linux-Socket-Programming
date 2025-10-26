#include <iostream>
#include <cstring>
#include <vector>
#include <sstream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <map>

using namespace std;

#define BUFFER_SIZE 1024

enum UserType { CUSTOMER, AIRLINE };
enum SeatStatus { FREE, RESERVED };
enum ReservationStatus { CONFIRMED, TEMPORARY };

struct User
{
    string username;
    string password;
    UserType role;
};

struct Flight
{
    string flight_id;
    string origin;
    string destination;
    string time;
    vector<SeatStatus> seat_map;
};

struct Reservation
{
    string reservation_id;
    string flight_id;
    string username;
    vector<int> seats;
    ReservationStatus status;
    time_t timestamp;
};

struct ServerData
{
    vector<User> users;
    vector<Flight> flights;
    vector<Reservation> reservations;
    map<int, User*> client_users;
};

void handleClientMessage(int client_socket, ServerData& data);
string handleListFlights(const ServerData& data);
int countAvailableSeats(const Flight& flight);
string handleRegister(ServerData& data, const string& command);
string handleLogin(ServerData& data, User*& logged_in_user, const string& command);
string handleAddFlight(ServerData& data, const string& command, const User* logged_in_user);
void sendUDPBroadcast(const ServerData& data, const string& message);

int main(int argc, char* argv[])
{
    ServerData data;

    int port = stoi(argv[1]);
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_socket, 5);

    string msg = "Server listening on port " + to_string(port) + "...\n";
    write(1, msg.c_str(), msg.length());

    fd_set master_fds, read_fds;
    FD_ZERO(&master_fds);
    FD_SET(server_socket, &master_fds);
    int max_fd = server_socket;

    while (true)
    {
        read_fds = master_fds;
        
        select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        for (int fd = 0; fd <= max_fd; fd++)
        {
            if (!FD_ISSET(fd, &read_fds)) continue;

            if (fd == server_socket)
            {
                sockaddr_in client_addr{};
                socklen_t client_len = sizeof(client_addr);
                int client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_len);
                
                FD_SET(client_socket, &master_fds);
                if (client_socket > max_fd) max_fd = client_socket;
                
                data.client_users[client_socket] = nullptr;
            }

            else
            {
                handleClientMessage(fd, data);
                if (data.client_users.find(fd) == data.client_users.end())
                {
                    FD_CLR(fd, &master_fds);
                }
            }
        }
    }

    close(server_socket);
    return 0;
}

void handleClientMessage(int client_socket, ServerData& data)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));
    int bytes = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    
    if (bytes <= 0)
    {
        close(client_socket);
        data.client_users.erase(client_socket);
        return;
    }

    buffer[bytes] = '\0';
    string command(buffer);
    string response;

    if (command.find("LIST_FLIGHTS") != string::npos)
    {
        response = handleListFlights(data);
    }

    else if (command.find("REGISTER") != string::npos)
    {
        response = handleRegister(data, command);
    }

    else if (command.find("LOGIN") != string::npos)
    {
        response = handleLogin(data, data.client_users[client_socket], command);
    }

    else if (command.find("ADD_FLIGHT") != string::npos)
    {
        if (data.client_users[client_socket] == nullptr)
        {
            response = "ERROR NotLoggedIn";
        }
        else
        {
            response = handleAddFlight(data, command, data.client_users[client_socket]);
        }
    }

    else
    {
        response = "ERROR UnknownCommand";
    }
    
    send(client_socket, response.c_str(), response.length(), 0);
}

string handleListFlights(const ServerData& data)
{
    stringstream response;

    if (data.flights.empty())
    {
        response << "NO_FLIGHTS";
        return response.str();
    }

    for (const auto& flight : data.flights)
    {
        int available = countAvailableSeats(flight);
        int total = flight.seat_map.size();
        response << "FLIGHT " << flight.flight_id << " "
                 << flight.origin << " " << flight.destination << " "
                 << flight.time << " SEATS_AVAILABLE=" << available << "/" << total;
    }

    return response.str();
}

int countAvailableSeats(const Flight& flight)
{
    int available = 0;
    for (const auto& seat : flight.seat_map)
    {
        if (seat == FREE) available++;
    }
    return available;
}

string handleRegister(ServerData& data, const string& command)
{
    stringstream ss(command);
    string cmd, role_str, username, password;
    ss >> cmd >> role_str >> username >> password;
    
    for (const auto& user : data.users)
    {
        if (user.username == username)
        {
            return "ERROR UsernameAlreadyExists";
        }
    }
    
    User new_user;
    new_user.username = username;
    new_user.password = password;
    new_user.role = (role_str == "AIRLINE") ? AIRLINE : CUSTOMER;
    
    data.users.push_back(new_user);
    
    string broadcast_msg = "BROADCAST NEW_USER " + username + " " + role_str;
    sendUDPBroadcast(data, broadcast_msg);
    
    return "REGISTERED OK";
}

string handleLogin(ServerData& data, User*& logged_in_user, const string& command)
{
    stringstream ss(command);
    string cmd, username, password;
    ss >> cmd >> username >> password;

    for (auto& user : data.users)
    {
        if (user.username == username)
        {
            if (user.password == password)
            {
                logged_in_user = &user;
                return "LOGIN OK";
            }

            else
            {
                return "ERROR InvalidPassword";
            }
        }
    }
    
    return "ERROR UserNotFound";
}

string handleAddFlight(ServerData& data, const string& command, const User* logged_in_user)
{
    if (logged_in_user->role != AIRLINE)
    {
        return "ERROR PermissionDenied";
    }
    
    stringstream ss(command);
    string cmd, flight_id, origin, destination, time;
    int column_count, row_count;
    ss >> cmd >> flight_id >> origin >> destination >> time >> column_count >> row_count;
    
    for (const auto& flight : data.flights)
    {
        if (flight.flight_id == flight_id)
        {
            return "ERROR DuplicateFlightID";
        }
    }
    
    Flight new_flight;
    new_flight.flight_id = flight_id;
    new_flight.origin = origin;
    new_flight.destination = destination;
    new_flight.time = time;
    
    int total_seats = column_count * row_count;
    new_flight.seat_map = vector<SeatStatus>(total_seats, FREE);
    
    data.flights.push_back(new_flight);
    
    string broadcast_msg = "BROADCAST NEW_FLIGHT " + flight_id + " " + origin + " " + destination + " " + time;
    sendUDPBroadcast(data, broadcast_msg);
    
    return "FLIGHT_ADDED OK";
}

void sendUDPBroadcast(const ServerData& data, const string& message)
{
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    
    int broadcast_enable = 1;
    setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));
    
    sockaddr_in broadcast_addr{};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");
    
    bool is_new_user = (message.find("NEW_USER") != string::npos);
    bool is_new_flight = (message.find("NEW_FLIGHT") != string::npos);
    
    for (const auto& [client_socket, user_ptr] : data.client_users)
    {
        if (user_ptr == nullptr) continue;
        
        if (is_new_user && user_ptr->role == AIRLINE)
        {
            sockaddr_in client_addr{};
            socklen_t addr_len = sizeof(client_addr);
            getpeername(client_socket, (sockaddr*)&client_addr, &addr_len);
            
            broadcast_addr.sin_port = htons(ntohs(client_addr.sin_port) + 1);
            
            sendto(udp_socket, message.c_str(), message.length(), 0, 
                   (sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
        }
        
        else if (is_new_flight && user_ptr->role == CUSTOMER)
        {
            sockaddr_in client_addr{};
            socklen_t addr_len = sizeof(client_addr);
            getpeername(client_socket, (sockaddr*)&client_addr, &addr_len);
            
            broadcast_addr.sin_port = htons(ntohs(client_addr.sin_port) + 1);
            
            sendto(udp_socket, message.c_str(), message.length(), 0, 
                   (sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
        }
    }
    
    close(udp_socket);
}