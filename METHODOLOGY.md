# Methodology: Multi-threaded Remote File System (RemoteFS)

## 1. Overview
The **RemoteFS** project is a distributed file system simulation designed to allow multiple users to store, retrieve, and share files on a central server. The system employs a **Client-Server Architecture** utilizing **TCP/IP Sockets** for reliable communication. It focuses on concurrency, data integrity, and user-friendly interaction through a modern Graphical User Interface (GUI).

## 2. System Architecture

The system consists of three main components:

1.  **The Central Server (C Application)**:
    *   Acts as the central repository for all files.
    *   Manages user authentication and storage directories.
    *   Handles concurrent client connections using **Multi-threading**.
    *   Enforces **Synchronization** (Reader-Writer locks) to ensure data consistency.

2.  **The Client (Python/CustomTkinter)**:
    *   Provides a user-friendly interface for file operations.
    *   Translates user actions (clicks) into network commands.
    *   Visualizes server responses (e.g., file content, directory trees).

3.  **The Protocol**:
    *   A custom string-based command protocol (e.g., `LOGIN user pass`, `READ file.txt`) exchanged over TCP port **8080**.

---

## 3. Server-Side Methodology

### 3.1. Socket Initialization & Connection Handling
The server initializes a **Winsock TCP socket** bound to a specific IP and Port (8080). It enters a listening state (`listen()`), waiting for incoming connections.
*   **Accept Loop**: The main thread runs an infinite loop calling `accept()`.
*   **Thread Creation**: Upon a new connection, the server spawns a dedicated **Client Thread** (`CreateThread`) to handle that specific user session independently. This ensures that one client's activity does not block others.

### 3.2. File Organization & "Jail" System
To ensure security and organization, the server implements a directory jail system:
*   **Root Storage**: All files are stored under a `storage/` directory.
*   **User Isolation**: Each registered user is assigned a dedicated subdirectory (e.g., `storage/alice/`).
*   **Path Resolution**: All file requests are routed through a `resolve_path()` function. This function prevents directory traversal attacks (e.g., `../`) and maps logical paths (e.g., `docs/file.txt`) to physical paths (e.g., `storage/alice/docs/file.txt`).

### 3.3. Concurrency Control (File Locking)
To address the **Reader-Writer Problem**, the server implements a custom locking mechanism using Windows Critical Sections (`CRITICAL_SECTION`) and a `FileLock` structure:
*   **Shared Read Locks**: Multiple users can `READ` a file simultaneously. The lock counter (`readers`) increments, preventing writers but allowing other readers.
*   **Exclusive Write Locks**: A `WRITE` operation requests exclusive access. It waits until there are **zero** active readers or writers before proceeding.
*   **Deadlock Prevention**: Locks are granular (per-file) and released immediately after the operation completes.

### 3.4. Shared Folder System
The system supports sharing resources between users without duplicating data.
*   **Metadata Table**: Shared permissions are stored in a memory structure (`shared_table`) loaded from `shared_folders.txt`.
*   **Virtual Mapping**: If a user attempts to access a shared path (e.g., `SHARED/bob/project`), the `resolve_path` function dynamically re-routes this request to the owner's physical directory (`storage/bob/project`) *if* the requester has the appropriate permissions (READ/WRITE).

---

## 4. Client-Side Methodology

### 4.1. Graphical User Interface (GUI)
The client requires no command-line knowledge. It is built using **Python** and **CustomTkinter** for a modern, dark-mode aesthetic.
*   **Tabbed Layout**: Features are organized into "Files", "Folders", and "Account" tabs.
*   **Real-time Feedback**: A console log within the GUI displays server responses (e.g., "Upload Complete", "Access Denied").

### 4.2. Network Communication
*   **Blocking Sockets**: The client establishes a continuous TCP connection upon startup.
*   **Command Wrapping**: Helper functions (e.g., `cmd_cb()`) wrap user inputs into protocol-compliant strings (e.g., `WRITE filename content`) and send them to the server.
*   **Large Data Handling**: For operations like `DOWNLOAD` or `LSR` (List Recursive), the client implements buffering loops to receive data in chunks (1024 bytes) until the transmission is complete.

---

## 5. Key Algorithms

### 5.1. Recursive Directory Listing (LSR)
To visualize nested directories, the server implements a **Depth-First Search (DFS)** algorithm:
1.  Open the target directory using `opendir()`.
2.  Iterate through all entries (`readdir()`).
3.  If an entry is a directory, recursively call the list function.
4.  If an entry is a file, verify its lock status and append it to the buffer.
5.  Return the formatted tree string to the client.

### 5.2. File Locking Logic (Pseudocode)
```c
function AcquireWriteLock(file):
    lock_manager.lock()
    if file.readers > 0 or file.writers > 0:
        wait() // Block thread
    file.writers++
    lock_manager.unlock()

function ReleaseWriteLock(file):
    lock_manager.lock()
    file.writers--
    lock_manager.unlock()
```

## 6. Tools & Technologies
*   **Language**: C (Server), Python (Client)
*   **Libraries**: `winsock2.h`, `windows.h`, `customtkinter`
*   **Communication**: TCP/IP Sockets
*   **OS**: Windows
