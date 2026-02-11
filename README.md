# Remote File System (RemoteFS)

A secure, multi-client remote file system implementation with support for file operations, user authentication, and resource sharing.

## Features
- **File Operations**: Upload, Download, Read, Write, Delete, Create, Move, Copy files.
- **Directory Management**: Create and remove directories.
- **User Authentication**: Secure Login/Register system.
- **File Sharing**: Share files/folders with other users with specific permissions (READ/WRITE).
- **Concurrency Control**: Robust file locking mechanism for multiple readers/single writer access.
- **GUI & CLI Clients**: Python-based GUI client and C-based CLI client.

## Components
- **Server (`server.c`)**: Multi-threaded server handling client requests, file operations, and concurrency.
- **Client (`client.c`)**: Command-line interface client for interacting with the server.
- **Modern Client (`modern_client.py`)**: User-friendly graphical interface built with `customtkinter`.
- **Server GUI (`server_gui.py`)**: Dashboard to manage the server, view logs, and monitor storage.

## How to Run locally

1. **Compile the Server**:
   ```bash
   gcc server.c -o server.exe -lws2_32
   ```

2. **Run the Server**:
   python server_gui.py
   # Or directly: ./server.exe
   ```

3. **Run the Client**:
   # GUI Client (Python)
   python modern_client.py

   # CLI Client (C) - Compile first
   gcc client.c -o client.exe -lws2_32
   ./client.exe
   ```

## ⚠️ Important: Changing IP Address for Multi-PC Setup

By default, the clients are configured to connect to `127.0.0.1` (localhost). To run the client on a different machine than the server:

1. **Find the Server's IP Address**:
   Run `ipconfig` on the server machine and note the IPv4 Address (e.g., `192.168.1.5`).

2. **Update the Python Client (`modern_client.py`)**:
   - Open `modern_client.py`.
   - Locate the line: `SERVER_IP = '127.0.0.1'`
   - Change it to the server's IP: `SERVER_IP = '192.168.1.5'`

3. **Update and Recompile the C Client (`client.c`)**:
   - Open `client.c`.
   - Locate the line: `server.sin_addr.s_addr = inet_addr("127.0.0.1");`
   - Change it to the server's IP: `server.sin_addr.s_addr = inet_addr("192.168.1.5");`
   - **Recompile the client**:
     ```bash
     gcc client.c -o client.exe -lws2_32
     ```
