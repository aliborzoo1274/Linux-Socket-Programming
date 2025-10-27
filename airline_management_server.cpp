#include <iostream>
#include <cstring>
#include <algorithm>
#include <vector>
#include <sstream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <map>
#include <ctime>
#include <signal.h>

using namespace std;

#define BUFFER_SIZE 1024
#define RESERVATION_TIMEOUT 30

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
    vector<vector<SeatStatus>> seat_map;
};

struct Reservation
{
    string reservation_id;
    string flight_id;
    string username;
    vector<string> seats;
    ReservationStatus status;
    time_t timestamp;
};

struct ServerData
{
    vector<User> users;
    vector<Flight> flights;
    vector<Reservation> reservations;
    map<int, string> client_users;
    int next_reservation_id;
    ServerData() : next_reservation_id(1) {}
};

ServerData* g_data = nullptr;

void handleClientMessage(int client_socket, ServerData& data);
string handleListFlights(const ServerData& data);
int countAvailableSeats(const Flight& flight);
string handleRegister(ServerData& data, const string& command);
User* getUserByUsername(ServerData& data, const string& username);
string handleLogin(ServerData& data, int client_socket, const string& command);
string handleAddFlight(ServerData& data, const string& command, const string& username);
void sendUDPBroadcast(ServerData& data, const string& message);
string handleReserve(ServerData& data, const string& command, int client_socket);
string handleConfirm(ServerData& data, const string& command, int client_socket);
string handleCancel(ServerData& data, const string& command, int client_socket);
int parseAndValidateSeats(const Flight& flight, const vector<string>& seat_codes, vector<string>& validated_seats);
void checkExpiredReservations(ServerData& data);

void alarm_handler(int sig)
{
    if (g_data != nullptr)
    {
        checkExpiredReservations(*g_data);
        alarm(5);
    }
}

int main(int argc, char* argv[])
{
    ServerData data;
    g_data = &data;
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = alarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);

    alarm(5);

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
        
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        
        if (activity < 0)
        {
            if (errno == EINTR) continue;
            break;
        }

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
                
                data.client_users[client_socket] = "";
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
        response = handleLogin(data, client_socket, command);
    }

    else if (command.find("ADD_FLIGHT") != string::npos)
    {
        string username = data.client_users[client_socket];

        if (username.empty())
        {
            response = "ERROR NotLoggedIn";
        }

        else
        {
            response = handleAddFlight(data, command, username);
        }
    }

    else if (command.find("RESERVE") != string::npos)
    {
        response = handleReserve(data, command, client_socket);
    }

    else if (command.find("CONFIRM") != string::npos)
    {
        response = handleConfirm(data, command, client_socket);
    }

    else if (command.find("CANCEL") != string::npos)
    {
        response = handleCancel(data, command, client_socket);
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
        int total = flight.seat_map.size() * (flight.seat_map.empty() ? 0 : flight.seat_map[0].size());
        response << "FLIGHT " << flight.flight_id << " "
                 << flight.origin << " " << flight.destination << " "
                 << flight.time << " SEATS_AVAILABLE=" << available << "/" << total;
    }

    return response.str();
}

int countAvailableSeats(const Flight& flight)
{
    int available = 0;
    for (const auto& row : flight.seat_map)
    {
        for (const auto& seat : row)
        {
            if (seat == FREE) available++;
        }
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

User* getUserByUsername(ServerData& data, const string& username)
{
    for (auto& user : data.users)
    {
        if (user.username == username)
        {
            return &user;
        }
    }

    return nullptr;
}

string handleLogin(ServerData& data, int client_socket, const string& command)
{
    stringstream ss(command);
    string cmd, username, password;
    ss >> cmd >> username >> password;

    User* user = getUserByUsername(data, username);
    
    if (user == nullptr)
    {
        return "ERROR UserNotFound";
    }
    
    if (user->password != password)
    {
        return "ERROR InvalidPassword";
    }
    
    for (const auto& [socket, logged_username] : data.client_users)
    {
        if (socket != client_socket && logged_username == username)
        {
            return "ERROR UserAlreadyLoggedIn";
        }
    }
    
    data.client_users[client_socket] = username;

    return "LOGIN OK";
}

string handleAddFlight(ServerData& data, const string& command, const string& username)
{
    User* user = getUserByUsername(data, username);
    
    if (user == nullptr || user->role != AIRLINE)
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
    
    new_flight.seat_map = vector<vector<SeatStatus>>(row_count, vector<SeatStatus>(column_count, FREE));
    
    data.flights.push_back(new_flight);
    
    string broadcast_msg = "BROADCAST NEW_FLIGHT " + flight_id + " " + origin + " " + destination + " " + time;
    sendUDPBroadcast(data, broadcast_msg);
    
    return "FLIGHT_ADDED OK";
}

void sendUDPBroadcast(ServerData& data, const string& message)
{
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    
    int broadcast_enable = 1;
    setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));
    
    sockaddr_in broadcast_addr{};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");
    
    bool is_new_user = (message.find("NEW_USER") != string::npos);
    bool is_new_flight = (message.find("NEW_FLIGHT") != string::npos);
    
    for (const auto& [client_socket, username] : data.client_users)
    {
        User* user_ptr = getUserByUsername(data, username);

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

string handleReserve(ServerData& data, const string& command, int client_socket)
{
    string username = data.client_users[client_socket];

    if (username.empty())
    {
        return "ERROR NotLoggedIn";
    }
    
    User* user = getUserByUsername(data, username);

    if (user == nullptr || user->role != CUSTOMER)
    {
        return "ERROR PermissionDenied";
    }
    
    stringstream ss(command);
    string cmd, flight_id;
    ss >> cmd >> flight_id;
    
    Flight* target_flight = nullptr;
    for (auto& flight : data.flights)
    {
        if (flight.flight_id == flight_id)
        {
            target_flight = &flight;
            break;
        }
    }
    
    if (target_flight == nullptr)
    {
        return "ERROR FlightNotFound";
    }
    
    vector<string> seat_codes;
    string seat_code;
    while (ss >> seat_code)
    {
        seat_codes.push_back(seat_code);
    }
    
    if (seat_codes.empty())
    {
        return "ERROR NoSeatsSpecified";
    }
    
    vector<string> validated_seats;
    int result = parseAndValidateSeats(*target_flight, seat_codes, validated_seats);
    
    if (result == -1) return "ERROR InvalidSeatFormat";
    if (result == -2) return "ERROR SeatNotAvailable";
    
    for (const string& seat : validated_seats)
    {
        char column = seat[0];
        int row = stoi(seat.substr(1)) - 1;
        int col = column - 'A';
        target_flight->seat_map[row][col] = RESERVED;
    }
    
    Reservation reservation;
    reservation.reservation_id = "R" + to_string(data.next_reservation_id++);
    reservation.flight_id = flight_id;
    reservation.username = username;
    reservation.seats = validated_seats;
    reservation.status = TEMPORARY;
    reservation.timestamp = time(nullptr);
    
    data.reservations.push_back(reservation);
    
    return "RESERVED TEMP " + reservation.reservation_id + " EXPIRES_IN 30";
}

string handleConfirm(ServerData& data, const string& command, int client_socket)
{
    string username = data.client_users[client_socket];

    if (username.empty())
    {
        return "ERROR NotLoggedIn";
    }
    
    stringstream ss(command);
    string cmd, reservation_id;
    ss >> cmd >> reservation_id;
    
    for (auto& reservation : data.reservations)
    {
        if (reservation.reservation_id == reservation_id)
        {
            if (reservation.username != username)
            {
                return "ERROR NotYourReservation";
            }
            
            time_t now = time(nullptr);

            if (difftime(now, reservation.timestamp) > RESERVATION_TIMEOUT)
            {
                return "ERROR ReservationExpired";
            }
            
            reservation.status = CONFIRMED;
            return "CONFIRMATION OK";
        }
    }
    
    return "ERROR ReservationNotFound";
}

string handleCancel(ServerData& data, const string& command, int client_socket)
{
    string username = data.client_users[client_socket];

    if (username.empty())
    {
        return "ERROR NotLoggedIn";
    }
    
    stringstream ss(command);
    string cmd, reservation_id;
    ss >> cmd >> reservation_id;
    
    for (size_t i = 0; i < data.reservations.size(); i++)
    {
        if (data.reservations[i].reservation_id == reservation_id)
        {
            if (data.reservations[i].username != username)
            {
                return "ERROR NotYourReservation";
            }
            
            for (auto& flight : data.flights)
            {
                if (flight.flight_id == data.reservations[i].flight_id)
                {
                    for (const string& seat : data.reservations[i].seats)
                    {
                        char column = seat[0];
                        int row = stoi(seat.substr(1)) - 1;
                        int col = column - 'A';
                        flight.seat_map[row][col] = FREE;
                    }
                    break;
                }
            }
            
            data.reservations.erase(data.reservations.begin() + i);
            return "CANCELED OK";
        }
    }
    
    return "ERROR ReservationNotFound";
}

int parseAndValidateSeats(const Flight& flight, const vector<string>& seat_codes, vector<string>& validated_seats)
{
    int row_count = flight.seat_map.size();
    int column_count = flight.seat_map.empty() ? 0 : flight.seat_map[0].size();
    
    for (const string& code : seat_codes)
    {
        if (code.length() < 2) return -1;
        
        char column = code[0];
        if (column < 'A' || column > 'Z') return -1;
        
        int row = stoi(code.substr(1)) - 1;
        int col = column - 'A';
        
        if (row < 0 || row >= row_count || col < 0 || col >= column_count) return -1;
        
        if (flight.seat_map[row][col] != FREE) return -2;
        
        validated_seats.push_back(code);
    }
    
    return 0;
}

void checkExpiredReservations(ServerData& data)
{
    time_t now = time(nullptr);
    
    for (size_t i = 0; i < data.reservations.size(); )
    {
        if (data.reservations[i].status == TEMPORARY &&
            difftime(now, data.reservations[i].timestamp) > RESERVATION_TIMEOUT)
        {
            for (auto& flight : data.flights)
            {
                if (flight.flight_id == data.reservations[i].flight_id)
                {
                    for (const string& seat : data.reservations[i].seats)
                    {
                        char column = seat[0];
                        int row = stoi(seat.substr(1)) - 1;
                        int col = column - 'A';
                        flight.seat_map[row][col] = FREE;
                    }
                    break;
                }
            }
            
            data.reservations.erase(data.reservations.begin() + i);
        }
        
        else
        {
            i++;
        }
    }
}