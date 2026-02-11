# Remote File System (RemoteFS) - Project Report

## 1. Introduction

**RemoteFS** is a robust, multi-threaded distributed file system application that enables users to store, retrieve, manage, and share files on a central server from remote locations. It mimics the core functionality of cloud storage services (like Google Drive or Dropbox) but is built from the ground up using low-level socket programming and system calls.

The system features a **Client-Server Architecture** where a highly concurrent C-based server manages storage and permissions, while a modern Python-based Graphical User Interface (GUI) provides an intuitive experience for end-users. Key features include user authentication, real-time file locking for data consistency, recursive directory listing, and a flexible permission-based file sharing system.

## 2. Problem Definition

In efficient computing environments, users need a way to access their files from multiple devices and share data with collaborators without physically transferring storage media (like USB drives).

**Key Challenges Addressed:**
*   **Centralization**: How to store files in a single location accessible via a network?
*   **Concurrency**: How to handle multiple users reading/writing files simultaneously without data corruption? (The Reader-Writer Problem)
*   **Security**: How to ensure users only access their own files or files explicitly shared with them?
*   **Usability**: How to make complex network file operations easy for non-technical users?

## 3. Tools and Techniques

The project leverages a mix of low-level system programming and high-level GUI development:

### **Server-Side (Backend)**
*   **Language**: C (Standard C99)
*   **Networking**: TCP/IP Sockets using `<winsock2.h>` (Windows Socket API).
*   **Concurrency**: Multi-threading using Windows API (`CreateThread`) to handle multiple clients efficiently.
*   **Synchronization**: Semaphores and Critical Sections (`CRITICAL_SECTION`) to implement Reader-Writer locks.
*   **File System**: Low-level I/O (`fopen`, `fread`, `fwrite`) and directory traversal (`dirent.h`).

### **Client-Side (Frontend)**
*   **Language**: Python 3
*   **GUI Framework**: **CustomTkinter** (Modern wrapper around Tkinter) for a responsive, dark-mode interface.
*   **Networking**: Python `socket` library.
*   **Threading**: Python `threading` module to keep the GUI responsive while waiting for network data.

---

## 4. Methodology

The system is designed around three core pillars: **Isolation**, **Synchronization**, and **Communication**.

### **A. System Architecture**
1.  **Connection Phase**: The server listens on Port **8080**. When a client connects, the server spawns a dedicated **Client Thread**. This ensures the main listener is never blocked.
2.  **Session Management**: The client performs a handshake (Authentication). Upon success, the server identifies the user and "jails" them to their specific storage directory (`storage/<username>/`).
3.  **Command Processing**: The client sends text-based commands (e.g., `WRITE file.txt content`). The server parses these commands, executes the file operation, and sends back the result or data.

### **B. Path Resolution & Security**
A custom `resolve_path()` function is the security gatekeeper.
*   **Local Access**: Maps `my_folder/file.txt` -> `storage/current_user/my_folder/file.txt`.
*   **Shared Access**: Detects the `SHARED/` prefix. If User A tries to access `SHARED/UserB/docs`, the server checks the `shared_table` to verify if User B granted User A permission. If valid, it redirects the path to User B's physical storage.

### **C. Concurrency Control (Reader-Writer Locks)**
To prevent race conditions (e.g., two users writing to the same file at once), the server attaches a `FileLock` structure to every active file:
*   **Active Readers**: Multiple users can read a file simultaneously.
*   **Active Writer**: Only one user can write at a time. Writing is blocked if *any* readers or other writers are active.

---

## 5. How It Works (Operational Flow)

### **Scenario: Uploading a File**
1.  **User Action**: User clicks "Upload File" in the Client GUI and selects `report.pdf`.
2.  **Client Logic**:
    *   Sends command: `UPLOAD report.pdf`.
    *   Sends file size.
    *   Waits for server acknowledgement ("READY").
    *   Transmits binary data in 1024-byte chunks.
3.  **Server Logic**:
    *   Receives `UPLOAD` command.
    *   Creates the file in the user's directory.
    *   Receives and writes data chunks until the expected size is reached.
    *   Sends "Success" confirmation.
4.  **Result**: The file appears in the server's storage and is visible in the user's list.

### **Scenario: Accessing a Shared File**
1.  **User A**: Selects their `project` folder and clicks "Share Item" -> enters `user2 READ`.
2.  **Server**: Updates `shared_folders.txt` with the permission rule.
3.  **User B**:
    *   Clicks "Shared With Me" to see available shares.
    *   Enters path `SHARED/user1/project` in "Explore Path".
4.  **Server**: `resolve_path` validates valid permission -> lists files from User A's directory.

---

## 6. Results & Features

The project successfully implements a fully functional remote file system with the following results:

1.  **Robust Networking**: Stable TCP communication tested on Local Area Network (LAN).
2.  **Recursive Directory Listing (`LSR`)**: Users can visualize deep directory structures and file hierarchies in a single view.
3.  **Real-Time Locking**: If a user is writing to a file, other users see `(LOCKED)` status in the file list immediately.
4.  **Granular Sharing**: Users can share specific files or entire folders with Read-only or Read-Write permissions.
5.  **Modern UI**: A polished, responsive GUI that abstracts away the complexity of command-line operations.
6.  **Auto-Refresh**: The client interface automatically updates the file list after any modification, ensuring the view is always current.
