#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define BUFFER 1024

/* Helper functions for file transfer */
void upload_file(SOCKET sock, const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf("Error: File not found locally\n");
        return;
    }
    
    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    // Send upload header
    char cmd[BUFFER];
    sprintf(cmd, "UPLOAD %s %ld", filename, filesize);
    send(sock, cmd, strlen(cmd), 0);
    
    // Wait for server ready
    char ack[BUFFER];
    recv(sock, ack, BUFFER, 0);
    if (strstr(ack, "READY")) {
        printf("Uploading %ld bytes...\n", filesize);
        char *fbuf = malloc(BUFFER);
        long sent = 0;
        while (sent < filesize) {
            int n = fread(fbuf, 1, BUFFER, fp);
            if (n > 0) {
                send(sock, fbuf, n, 0);
                sent += n;
            } else break;
        }
        free(fbuf);
        
        // Wait for final ack
        memset(ack, 0, BUFFER);
        recv(sock, ack, BUFFER, 0);
        printf("Server: %s", ack);
    } else {
        printf("Server Error: %s", ack);
    }
    fclose(fp);
}

void download_file(SOCKET sock, const char *filename) {
    char cmd[BUFFER];
    sprintf(cmd, "DOWNLOAD %s", filename);
    send(sock, cmd, strlen(cmd), 0);
    
    char resp[BUFFER];
    int n = recv(sock, resp, BUFFER, 0);
    resp[n] = 0;
    
    long filesize = 0;
    if (sscanf(resp, "SIZE %ld", &filesize) == 1) {
        send(sock, "READY", 5, 0); // Send ACK
        
        FILE *fp = fopen(filename, "wb");
        if (!fp) {
            printf("Error: Cannot create local file\n");
            return;
        }
        
        printf("Downloading %ld bytes...\n", filesize);
        char *fbuf = malloc(BUFFER);
        long rcvd = 0;
        while (rcvd < filesize) {
            int to_read = (filesize - rcvd < BUFFER) ? (filesize - rcvd) : BUFFER;
            int r = recv(sock, fbuf, to_read, 0);
            if (r <= 0) break;
            fwrite(fbuf, 1, r, fp);
            rcvd += r;
        }
        free(fbuf);
        fclose(fp);
        printf("Download Complete\n");
    } else {
        printf("Server Response: %s", resp);
    }
}



void show_help() {
    printf("\n==================== COMMAND LIST ====================\n");
    printf("%-10s : %-35s | %s\n", "COMMAND", "DESCRIPTION", "SYNTAX");
    printf("--------------------------------------------------------------------------\n");
    printf("%-10s : %-35s | %s\n", "REGISTER", "Create new user account", "REGISTER <user> <pass>");
    printf("%-10s : %-35s | %s\n", "LOGIN", "Login to account", "LOGIN <user> <pass>");
    printf("%-10s : %-35s | %s\n", "LOGOUT", "Logout user", "LOGOUT");
    printf("%-10s : %-35s | %s\n", "LS", "List all files/dirs", "LS");
    printf("%-10s : %-35s | %s\n", "LSR", "Recursive directory listing", "LSR");
    printf("%-10s : %-35s | %s\n", "CHPASS", "Change password", "CHPASS <old> <new>");
    printf("%-10s : %-35s | %s\n", "MKDIR", "Create directory", "MKDIR <dirname>");
    printf("%-10s : %-35s | %s\n", "RMDIR", "Remove empty directory", "RMDIR <dirname>");
    printf("%-10s : %-35s | %s\n", "TOUCH", "Create empty file", "TOUCH <filename>");
    printf("%-10s : %-35s | %s\n", "WRITE", "Write text to file", "WRITE <filename> <text>");
    printf("%-10s : %-35s | %s\n", "READ", "Read file content", "READ <filename>");
    printf("%-10s : %-35s | %s\n", "DELETE", "Delete file", "DELETE <filename>");
    printf("%-10s : %-35s | %s\n", "STAT", "Show file details", "STAT <filename>");
    printf("%-10s : %-35s | %s\n", "COPY", "Copy file", "COPY <src> <dest>");
    printf("%-10s : %-35s | %s\n", "MOVE", "Move/Rename file", "MOVE <src> <dest>");
    printf("%-10s : %-35s | %s\n", "PUTFILE", "Move file into directory", "PUTFILE <file> <dir>");
    printf("%-10s : %-35s | %s\n", "UPLOAD", "Upload local file to server", "UPLOAD <filename>");
    printf("%-10s : %-35s | %s\n", "DOWNLOAD", "Download server file", "DOWNLOAD <filename>");
    printf("==========================================================================\n");
}

int main() {
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in server;
    char buffer[BUFFER];
    int bytes;

    /* Initialize Winsock */
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }

    /* Create socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        printf("Socket creation failed\n");
        WSACleanup();
        return 1;
    }

    /* Server details */
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");  
    // 🔁 Change to server IP if on different PC

    /* Connect */
    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        printf("Connection to server failed\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("Connected to Remote File System Server\n");
    show_help();

    /* Communication loop */
    while (1) {
        printf("\nEnter command: ");
        memset(buffer, 0, BUFFER);
        fgets(buffer, BUFFER, stdin);

        /* Parse command locally to intercept UPLOAD/DOWNLOAD */
        char temp_cmd[BUFFER], arg1[BUFFER];
        strcpy(temp_cmd, buffer);
        char *token = strtok(temp_cmd, " \n");

        if (token && strcmp(token, "UPLOAD") == 0) {
            token = strtok(NULL, " \n");
            if (token) {
                upload_file(sock, token);
            } else {
                printf("Usage: UPLOAD <filename>\n");
            }
            continue;
        } 
        else if (token && strcmp(token, "DOWNLOAD") == 0) {
            token = strtok(NULL, " \n");
            if (token) {
                download_file(sock, token);
            } else {
                printf("Usage: DOWNLOAD <filename>\n");
            }
            continue;
        }

        /* Send standard command */
        send(sock, buffer, strlen(buffer), 0);

        /* Receive response */
        memset(buffer, 0, BUFFER);
        bytes = recv(sock, buffer, BUFFER - 1, 0);

        if (bytes <= 0) {
            printf("Server disconnected\n");
            break;
        }

        buffer[bytes] = '\0';
        printf("Server: %s", buffer);
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
