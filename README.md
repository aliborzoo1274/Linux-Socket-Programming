# Linux Socket Programming — Airline Management (TCP + UDP)

A compact client–server application written in C++ that demonstrates Linux socket programming with:
- A multi-client TCP server using `select()` for I/O multiplexing
- A simple, text-based command protocol for airline-style operations (register/login, add flight, list flights, reserve/confirm/cancel seats)
- UDP one-way notifications from server to clients (e.g., reservation updates)
- Temporary reservations with timeout and periodic expiration checks via `SIGALRM`

This project is primarily C++ with a small Makefile for building targets.

- Repository language composition (approx.): C++ ~98.8%, Makefile ~1.2%
- Key files:
  - [Makefile](https://github.com/aliborzoo1274/Linux-Socket-Programming/blob/cf433a96d6e8ff2325f048bc99c7b9eb225da95c/Makefile)
  - [airline_management_server.cpp](https://github.com/aliborzoo1274/Linux-Socket-Programming/blob/cf433a96d6e8ff2325f048bc99c7b9eb225da95c/airline_management_server.cpp)
  - [airline_management_client.cpp](https://github.com/aliborzoo1274/Linux-Socket-Programming/blob/cf433a96d6e8ff2325f048bc99c7b9eb225da95c/airline_management_client.cpp)

## Features

- Multi-client TCP server using `select()` to handle concurrent clients
- In-memory models for:
  - Users (roles: CUSTOMER, AIRLINE)
  - Flights with seat maps
  - Reservations (temporary or confirmed)
- Reservation timeout logic
  - Temporary reservations expire automatically
  - Periodic checks driven by `SIGALRM`
- UDP notifications
  - Clients listen on a dedicated UDP port and print server notifications
- Simple command-driven protocol (text lines over TCP):
  - Register, login
  - Add flights (airline role)
  - List flights
  - Reserve seats, confirm reservations, cancel

## Build

Requirements:
- Linux (POSIX sockets)
- `g++` and `make`

Build everything:
```bash
make
```

Build targets (from the [Makefile](https://github.com/aliborzoo1274/Linux-Socket-Programming/blob/cf433a96d6e8ff2325f048bc99c7b9eb225da95c/Makefile)):
- Server: `server.out` (from `airline_management_server.cpp`)
- Client: `client.out` (from `airline_management_client.cpp`)

Clean artifacts:
```bash
make clean
```

## Run

Start the server (provide a TCP port):
```bash
./server.out 5000
```
You should see a message similar to:
```
Server listening on port 5000...
```

Start a client (provide server IP and port):
```bash
./client.out 127.0.0.1 5000
```

You can start multiple clients in separate terminals.

## How it works

- Server
  - File: [airline_management_server.cpp](https://github.com/aliborzoo1274/Linux-Socket-Programming/blob/cf433a96d6e8ff2325f048bc99c7b9eb225da95c/airline_management_server.cpp)
  - Uses `socket`, `bind`, `listen`, `accept`, and `select` to multiplex clients
  - Keeps all state in memory (users, flights, reservations)
  - Periodically runs expiration checks for temporary reservations using `alarm(5)` + `SIGALRM`
  - Implements handlers (by command string) such as:
    - `handleRegister`, `handleLogin`
    - `handleAddFlight`
    - `handleListFlights`
    - `handleReserve`, `handleConfirm`, `handleCancel`

- Client
  - File: [airline_management_client.cpp](https://github.com/aliborzoo1274/Linux-Socket-Programming/blob/cf433a96d6e8ff2325f048bc99c7b9eb225da95c/airline_management_client.cpp)
  - Connects to the server over TCP
  - Spawns a thread to listen for UDP messages and prints them
  - UDP port strategy: the client listens on `udp_port = <local_tcp_port> + 1`
  - Reads commands from STDIN and sends them to the server; prints server responses

## Commands

The server parses line-oriented commands sent by the client. Based on the server code, the following commands are implemented:

- Registration and authentication
  - `REGISTER ...`
  - `LOGIN ...`
- Flight operations
  - `ADD_FLIGHT ...` (typically for airline role)
  - `LIST_FLIGHTS`
- Reservation operations
  - `RESERVE ...`
  - `CONFIRM ...`
  - `CANCEL ...`

Notes:
- Exact argument formats and response messages are defined in the server logic. Use the client interactively to discover accepted formats and server feedback.
- Reservations can be temporary and will expire automatically if not confirmed within the configured timeout.

## Example session (illustrative)

In a client terminal, try commands like:
```
REGISTER <username> <password>
LOGIN <username> <password>

ADD_FLIGHT <flight_id> <origin> <destination> <time> <rows>x<cols>
LIST_FLIGHTS

RESERVE <flight_id> <seat1,seat2,...>
CONFIRM <reservation_id>
CANCEL <reservation_id>
```

The server will respond with status messages; the client will also print any UDP notifications emitted by the server.

## Configuration and constants

Some key constants visible in the source:
- `BUFFER_SIZE = 1024` (I/O buffers)
- `RESERVATION_TIMEOUT = 30` seconds (temporary reservation TTL)
- Alarm interval: `alarm(5)` to periodically check expiration

Adjust as needed in the source files and rebuild.

## Troubleshooting

- Ensure the server port is open and not in use.
- If using firewalls, allow:
  - TCP to the server port (e.g., 5000) for client connections
  - UDP on the client's computed port (`local_tcp_port + 1`) for notifications
- If `git` or compilers aren’t found, install build-essential tools for your distro (e.g., `sudo apt-get install build-essential` on Debian/Ubuntu).

## Project structure

Top-level files:
- [Makefile](https://github.com/aliborzoo1274/Linux-Socket-Programming/blob/cf433a96d6e8ff2325f048bc99c7b9eb225da95c/Makefile) — builds `server.out` and `client.out`
- [airline_management_server.cpp](https://github.com/aliborzoo1274/Linux-Socket-Programming/blob/cf433a96d6e8ff2325f048bc99c7b9eb225da95c/airline_management_server.cpp) — server implementation (TCP + UDP notifications, reservation logic)
- [airline_management_client.cpp](https://github.com/aliborzoo1274/Linux-Socket-Programming/blob/cf433a96d6e8ff2325f048bc99c7b9eb225da95c/airline_management_client.cpp) — interactive client with UDP listener thread

---

## 👤 Author

**Ali Borzoozadeh** ([@aliborzoo1274](https://github.com/aliborzoo1274))