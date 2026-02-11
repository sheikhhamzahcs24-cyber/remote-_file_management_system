#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <direct.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>

#include <process.h> /* For threads if using _beginthreadex, though we used CreateThread which is in windows.h */

#ifndef _WINDOWS_
#include <windows.h>
#endif

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define BUF 1024

/* ---------- USER DATABASE ---------- */
int user_exists(const char *u) {
    FILE *fp = fopen("users.txt", "r");
    char user[50], pass[50];
    if (!fp) return 0;
    while (fscanf(fp, "%s %s", user, pass) != EOF) {
        if (strcmp(user, u) == 0) {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

int authenticate(const char *u, const char *p) {
    FILE *fp = fopen("users.txt", "r");
    char user[50], pass[50];
    if (!fp) return 0;
    while (fscanf(fp, "%s %s", user, pass) != EOF) {
        if (strcmp(user, u) == 0 && strcmp(pass, p) == 0) {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}


int change_password_file(const char *u, const char *old_p, const char *new_p) {
    FILE *fp = fopen("users.txt", "r");
    FILE *tmp = fopen("users_temp.txt", "w");
    char user[50], pass[50];
    int found = 0;

    if (!fp || !tmp) return 0;

    while (fscanf(fp, "%s %s", user, pass) != EOF) {
        if (strcmp(user, u) == 0 && strcmp(pass, old_p) == 0) {
            fprintf(tmp, "%s %s\n", user, new_p);
            found = 1;
        } else {
            fprintf(tmp, "%s %s\n", user, pass);
        }
    }
    
    fclose(fp);
    fclose(tmp);

    if (found) {
        remove("users.txt");
        rename("users_temp.txt", "users.txt");
        return 1;
    } else {
        remove("users_temp.txt");
        return 0;
    }
}

void log_command(const char *user, const char *command) {
    FILE *fp = fopen("server_log.txt", "a");
    time_t now = time(NULL);
    char *t = ctime(&now);
    if(t) t[strlen(t) - 1] = '\0'; // remove newline
    
    if (user && strlen(user) > 0)
        fprintf(fp, "[%s] %s : %s\n", t, user, command);
    else
        fprintf(fp, "[%s] Unknown : %s\n", t, command);
    
    fclose(fp);
}

/* ---------- FILE LOCKING SYSTEM ---------- */
CRITICAL_SECTION file_locks_cs;

typedef struct FileLock {
    char filepath[512];
    int readers;
    int writers;
    struct FileLock *next;
} FileLock;

FileLock *head_lock = NULL;

FileLock* get_file_lock(const char *path) {
    EnterCriticalSection(&file_locks_cs);
    FileLock *curr = head_lock;
    while(curr) {
        if(strcmp(curr->filepath, path) == 0) {
            LeaveCriticalSection(&file_locks_cs);
            return curr;
        }
        curr = curr->next;
    }
    FileLock *new_lock = (FileLock*)malloc(sizeof(FileLock));
    strcpy(new_lock->filepath, path);
    new_lock->readers = 0;
    new_lock->writers = 0;
    new_lock->next = head_lock;
    head_lock = new_lock;
    LeaveCriticalSection(&file_locks_cs);
    return new_lock;
}

void acquire_write_lock(FileLock *l, SOCKET c) {
    int notified = 0;
    while(1) {
        EnterCriticalSection(&file_locks_cs);
        if (l->writers == 0 && l->readers == 0) {
            l->writers = 1;
            LeaveCriticalSection(&file_locks_cs);
            break;
        }
        LeaveCriticalSection(&file_locks_cs);
        
        if (!notified) {
            char *msg = "Server: File is currently locked for writing by another user. Please wait.\n";
            send(c, msg, strlen(msg), 0);
            notified = 1;
        }
        Sleep(200);
    }
    char *msg = "Server: WRITE_LOCK_GRANTED. You may write now.\n";
    send(c, msg, strlen(msg), 0);
}

void release_write_lock(FileLock *l, SOCKET c) {
    EnterCriticalSection(&file_locks_cs);
    l->writers = 0;
    LeaveCriticalSection(&file_locks_cs);
    char *msg = "Server: WRITE_COMPLETED. File unlocked.\n";
    send(c, msg, strlen(msg), 0);
}

void acquire_read_lock(FileLock *l, SOCKET c) {
    int notified = 0;
    while(1) {
        EnterCriticalSection(&file_locks_cs);
        if (l->writers == 0) {
            l->readers++;
            int r_count = l->readers;
            LeaveCriticalSection(&file_locks_cs);
            
            if (r_count > 1) {
                char *msg = "Server: Multiple readers permitted. No writer active.\n";
                send(c, msg, strlen(msg), 0);
            } else {
                char *msg = "Server: READ_LOCK_GRANTED. You may read now.\n";
                send(c, msg, strlen(msg), 0);
            }
            break;
        }
        LeaveCriticalSection(&file_locks_cs);

        if (!notified) {
            char *msg = "Server: File is being modified. Your read request is queued.\n";
            send(c, msg, strlen(msg), 0);
            notified = 1;
        }
        Sleep(200);
    }
}

void release_read_lock(FileLock *l, SOCKET c) {
    EnterCriticalSection(&file_locks_cs);
    l->readers--;
    LeaveCriticalSection(&file_locks_cs);
    // No specific release message requested for READ, but we can be silent or redundant.
}

/* ---------- CLIENT SESSION ---------- */
/* ---------- SHARED FOLDERS SYSTEM ---------- */
#define MAX_SHARES 100

typedef struct {
    char owner[50];
    char folder_name[50];
    char shared_with[50];
    char permission[10]; // "READ", "WRITE", "READ_WRITE"
} SharedFolder;

SharedFolder shared_table[MAX_SHARES];
int shared_count = 0;

void load_shares() {
    FILE *fp = fopen("shared_folders.txt", "r");
    if (!fp) return;
    shared_count = 0;
    while (fscanf(fp, "%s %s %s %s", 
           shared_table[shared_count].owner,
           shared_table[shared_count].folder_name,
           shared_table[shared_count].shared_with,
           shared_table[shared_count].permission) != EOF) {
        shared_count++;
        if (shared_count >= MAX_SHARES) break;
    }
    fclose(fp);
}

void save_share(const char *owner, const char *folder, const char *target, const char *perm) {
    FILE *fp = fopen("shared_folders.txt", "a");
    fprintf(fp, "%s %s %s %s\n", owner, folder, target, perm);
    fclose(fp);
    
    // Update in-memory
    if (shared_count < MAX_SHARES) {
        strcpy(shared_table[shared_count].owner, owner);
        strcpy(shared_table[shared_count].folder_name, folder);
        strcpy(shared_table[shared_count].shared_with, target);
        strcpy(shared_table[shared_count].permission, perm);
        shared_count++;
    }
}

/* RECURSIVE LISTING (LSR) */
void list_recursive(const char *base_path, const char *rel_path, const char *display_prefix, char *buffer, int *buf_len) {
    char full_path[512];
    struct dirent *entry;
    DIR *dp;

    if (strlen(rel_path) == 0) sprintf(full_path, "%s", base_path);
    else sprintf(full_path, "%s/%s", base_path, rel_path);

    dp = opendir(full_path);
    if (dp == NULL) return;

    while ((entry = readdir(dp))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char new_rel_path[512];
        if (strlen(rel_path) == 0) sprintf(new_rel_path, "%s", entry->d_name);
        else sprintf(new_rel_path, "%s/%s", rel_path, entry->d_name);

        struct stat st;
        char stat_path[512];
        sprintf(stat_path, "%s/%s", full_path, entry->d_name);
        stat(stat_path, &st);

        // Check if directory
        if (S_ISDIR(st.st_mode)) {
            // Append Directory Header with Display Prefix
            char line[200];
            if (strlen(display_prefix) > 0)
                sprintf(line, "\n%s/%s/:\n", display_prefix, new_rel_path);
            else
                sprintf(line, "\n%s/:\n", new_rel_path);

            if (*buf_len + strlen(line) < BUF-100) {
                strcat(buffer, line);
                *buf_len += strlen(line);
            }
            // Recurse
            list_recursive(base_path, new_rel_path, display_prefix, buffer, buf_len);
        } else {
            // Check Lock
            char line[200];
            FileLock *l = get_file_lock(stat_path);
            /* Check if strictly locked for writing */
            char lock_status[30] = "";
            EnterCriticalSection(&file_locks_cs);
            if (l->writers > 0) strcpy(lock_status, " (LOCKED)");
            LeaveCriticalSection(&file_locks_cs);

            sprintf(line, "  |-- %s%s\n", entry->d_name, lock_status);
            
            if (*buf_len + strlen(line) < BUF-50) {
                strcat(buffer, line);
                *buf_len += strlen(line);
            }
        }
    }
    closedir(dp);
}

/* 
 * resolve_path:
 * Resolves input_path to a physical path on disk.
 * Handles:
 * 1. Local files: "storage/<current_user>/<input_path>"
 * 2. Shared files (legacy implicit): "storage/<owner>/<input_path>"
 * 3. Shared files (explicit): "SHARED/<owner>/<path>"
 */
int resolve_path(const char *current_user, const char *input_path, char *final_path, const char *required_perm) {
    // 1. Check for explicit "SHARED/" prefix
    if (strncmp(input_path, "SHARED/", 7) == 0) {
         char temp[256];
         strcpy(temp, input_path + 7); // Strip "SHARED/"
         
         // Format: user1/folder/file...
         char owner[50], remainder[200];
         char *slash = strchr(temp, '/');
         if (!slash) return 0; // Invalid format
         
         int len = slash - temp;
         strncpy(owner, temp, len);
         owner[len] = '\0';
         strcpy(remainder, slash + 1);
         
         // remainder is now folder/file...
         // We need to match folder name against shared_table
         char folder[50], subpath[200] = "";
         slash = strchr(remainder, '/');
         if (slash) {
             len = slash - remainder;
             strncpy(folder, remainder, len);
             folder[len] = '\0';
             strcpy(subpath, slash + 1);
         } else {
             strcpy(folder, remainder);
         }
         
         // Check permissions in table
         for (int k = 0; k < shared_count; k++) {
             if (strcmp(shared_table[k].owner, owner) == 0 &&
                 strcmp(shared_table[k].folder_name, folder) == 0 &&
                 strcmp(shared_table[k].shared_with, current_user) == 0) {
                 
                 // Check perm
                 int has_write = (strstr(shared_table[k].permission, "WRITE") != NULL);
                 int is_write_req = (strcmp(required_perm, "WRITE") == 0);
                 if (is_write_req && !has_write) return 0;
                 
                 // Build Path
                 if (strlen(subpath) > 0)
                     sprintf(final_path, "storage/%s/%s/%s", owner, folder, subpath);
                 else
                     sprintf(final_path, "storage/%s/%s", owner, folder);
                 return 1;
             }
         }
         return 0; // Not found in shares
    }

    // 2. Original Logic (Implicit Shared or Local)
    char first_component[100];
    char remainder[256] = "";
    
    int i = 0;
    while(input_path[i] != '/' && input_path[i] != '\0' && i < 99) {
        first_component[i] = input_path[i];
        i++;
    }
    first_component[i] = '\0';
    if (input_path[i] == '/') strcpy(remainder, input_path + i + 1);

    // Check shared table (legacy implicit support)
    for (int k = 0; k < shared_count; k++) {
        if (strcmp(shared_table[k].shared_with, current_user) == 0 &&
            strcmp(shared_table[k].folder_name, first_component) == 0) {
            
            int has_write = (strstr(shared_table[k].permission, "WRITE") != NULL);
            int is_write_req = (strcmp(required_perm, "WRITE") == 0);
            if (is_write_req && !has_write) return 0;

            if (strlen(remainder) > 0)
                sprintf(final_path, "storage/%s/%s/%s", shared_table[k].owner, first_component, remainder);
            else
                sprintf(final_path, "storage/%s/%s", shared_table[k].owner, first_component);
            return 1;
        }
    }

    sprintf(final_path, "storage/%s/%s", current_user, input_path);
    return 1;
}

/* ---------- CLIENT SESSION ---------- */
void handle_client(SOCKET c) {
    char buf[BUF];
    char cmd[20], a1[256], a2[256], a3[20];
    char current_user[50] = "";
    char path1[512], path2[512];
    struct stat st;
    
    // Ensure shares are loaded (simple approach: reload every connection or on start)
    // Ideally use a mutex, but for this lab valid enough.
    load_shares(); 

    while (1) {
        memset(buf, 0, BUF);
        int r = recv(c, buf, BUF, 0);
        if (r <= 0) break;
        
        printf("[DEBUG] Raw Received: '%s'\n", buf);

        sscanf(buf, "%s", cmd);

        /* LOGGING */
        char log_buf[BUF];
        strcpy(log_buf, buf);
        log_buf[strcspn(log_buf, "\r\n")] = 0;
        log_command(current_user, log_buf);

        /* LS */
        if (strcmp(cmd, "LS") == 0) {
            if (strlen(current_user) == 0) {
                send(c, "Please login first\n", 19, 0);
            } else {
                WIN32_FIND_DATA fd;
                HANDLE hFind;
                char search_path[512], file_list[BUF];
                
                // 1. List Local Files
                sprintf(search_path, "storage/%s/*", current_user);
                memset(file_list, 0, BUF);
                
                hFind = FindFirstFile(search_path, &fd);
                if (hFind != INVALID_HANDLE_VALUE) {
                    do {
                        if (strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
                            if (strlen(file_list) + strlen(fd.cFileName) + 20 < BUF) {
                                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                                    strcat(file_list, "[DIR] ");
                                strcat(file_list, fd.cFileName);
                                strcat(file_list, "\n");
                            }
                        }
                    } while (FindNextFile(hFind, &fd));
                    FindClose(hFind);
                }
                
                // 2. Append Shared Folders
                for(int k=0; k<shared_count; k++) {
                     if(strcmp(shared_table[k].shared_with, current_user) == 0) {
                         char line[100];
                         sprintf(line, "[SHARED] %s (from %s)\n", shared_table[k].folder_name, shared_table[k].owner);
                         if (strlen(file_list) + strlen(line) < BUF)
                             strcat(file_list, line);
                     }
                }
                
                if (strlen(file_list) == 0) strcpy(file_list, "Empty directory\n");
                send(c, file_list, strlen(file_list), 0);
            }
        }

        /* LSR [path] */
        else if (strcmp(cmd, "LSR") == 0) {
             if (strlen(current_user) == 0) {
                 send(c, "Please login first\n", 19, 0);
             } else {
                 char extra[256] = "";
                 sscanf(buf, "%*s %s", extra);
                 
                 char base[512];
                 int allowed = 1;
                 char *ls_buf = malloc(BUF * 8); // Dynamic buffer

                 if (!ls_buf) {
                     send(c, "Server memory error\n", 20, 0);
                     continue;
                 }
                 memset(ls_buf, 0, BUF * 8);
                 int len = 0;
                 char display_prefix[256] = "";

                 if (strlen(extra) > 0) {
                     // User specified a path
                     if (strncmp(extra, "SHARED/", 7) == 0) {
                         strcpy(display_prefix, extra);
                     }
                     
                     if (!resolve_path(current_user, extra, base, "READ")) {
                         sprintf(ls_buf, "Error: Access denied or path not found: %s\n", extra);
                         allowed = 0;
                     } else {
                         sprintf(ls_buf, " Listing for %s:\n", extra);
                         struct stat s;
                         if(stat(base, &s)==0 && !S_ISDIR(s.st_mode)) {
                             strcat(ls_buf, " (Is a file)\n");
                             allowed = 0; 
                         }
                     }
                 } else {
                     // Default: Local root
                     sprintf(base, "storage/%s", current_user);
                     strcat(ls_buf, " Directory tree for user:\n");
                 }
                 len = strlen(ls_buf);

                 if (allowed) {
                     list_recursive(base, "", display_prefix, ls_buf, &len);
                     
                     // If Root listing (no extra), append Shared Headers
                     if (strlen(extra) == 0) {
                         if (len + 100 < BUF*8) strcat(ls_buf, "\n [Shared Resources]:\n");
                         
                         for(int k=0; k<shared_count; k++) {
                             if(strcmp(shared_table[k].shared_with, current_user) == 0) {
                                 char shared_disp[200];
                                 sprintf(shared_disp, "SHARED/%s/%s", shared_table[k].owner, shared_table[k].folder_name);
                                 
                                 char phys_path[512];
                                 sprintf(phys_path, "storage/%s/%s", shared_table[k].owner, shared_table[k].folder_name);
                                 
                                 struct stat s;
                                 if(stat(phys_path, &s)==0) {
                                     if (S_ISDIR(s.st_mode)) {
                                         // Print Header using display path
                                         char line[300];
                                         sprintf(line, "\n%s/:\n", shared_disp);
                                         if((len + strlen(line)) < BUF*8) { 
                                             strcat(ls_buf, line); 
                                             len += strlen(line);
                                         }
                                         
                                         // Recurse
                                         list_recursive(phys_path, "", shared_disp, ls_buf, &len);
                                     } else {
                                         // File
                                         char line[300];
                                         sprintf(line, " |- %s (Shared File)\n", shared_disp);
                                         if((len + strlen(line)) < BUF*8) { 
                                             strcat(ls_buf, line); 
                                             len += strlen(line);
                                         }
                                     }
                                 }
                             }
                         }
                     }
                     
                     if (len == strlen(" Directory tree for user:\n") && strlen(extra)==0) 
                         strcat(ls_buf, " (Empty)\n");
                 }
                 
                 send(c, ls_buf, strlen(ls_buf), 0);
                 free(ls_buf);
             }
        }

        /* REGISTER */
        else if (strcmp(cmd, "REGISTER") == 0) {
            sscanf(buf, "%*s %s %s", a1, a2);
            if (user_exists(a1)) {
                send(c, "User already exists\n", 21, 0);
            } else {
                FILE *fp = fopen("users.txt", "a");
                fprintf(fp, "%s %s\n", a1, a2);
                fclose(fp);

                _mkdir("storage");
                sprintf(path1, "storage/%s", a1);
                _mkdir(path1);

                send(c, "Registration successful\n", 24, 0);
            }
        }

        /* LOGIN */
        else if (strcmp(cmd, "LOGIN") == 0) {
            sscanf(buf, "%*s %s %s", a1, a2);
            if (authenticate(a1, a2)) {
                strcpy(current_user, a1);
                send(c, "Login successful\n", 18, 0);
            } else {
                send(c, "Invalid credentials\n", 20, 0);
            }
        }
        
        /* LOGOUT */
        else if (strcmp(cmd, "LOGOUT") == 0) {
            current_user[0] = '\0';
            send(c, "Logged out\n", 11, 0);
        }

        /* BLOCK IF NOT LOGGED IN (Except Auth) */
        else if (strlen(current_user) == 0) {
            send(c, "Please login first\n", 19, 0);
        }

        /* SHARE <folder> WITH <user> <perm> */
        else if (strcmp(cmd, "SHARE") == 0) {
            // Buf: SHARE docs WITH user2 READ
            // sscanf format slightly tricky with "WITH"
            char target[50], perm[20];
            sscanf(buf, "%*s %s %*s %s %s", a1, target, perm); // a1=folder
            
            // Verify file/folder exists locally
            sprintf(path1, "storage/%s/%s", current_user, a1);
            struct stat st;
            if (stat(path1, &st) != 0) {
                send(c, "Error: File/Folder not found\n", 29, 0);
            } else if (!user_exists(target)) {
                send(c, "Error: Target user not found\n", 29, 0);
            } else {
                save_share(current_user, a1, target, perm);
                send(c, "Shared successfully\n", 20, 0);
            }
        }

        /* SHARED_WITH_ME */
        else if (strcmp(cmd, "SHARED_WITH_ME") == 0) {
            char list[BUF] = "Folders/Files shared with you:\n";
            int found = 0;
            for(int k=0; k<shared_count; k++) {
                if(strcmp(shared_table[k].shared_with, current_user) == 0) {
                    char line[200];
                    sprintf(line, "- %s (Use Path: SHARED/%s/%s) [%s]\n", 
                        shared_table[k].folder_name, shared_table[k].owner, shared_table[k].folder_name, shared_table[k].permission);
                    if (strlen(list) + strlen(line) < BUF)
                        strcat(list, line);
                    found = 1;
                }
            }
            if (!found) strcat(list, "(None)\n");
            send(c, list, strlen(list), 0);
        }
        
        /* MKDIR */
        else if (strcmp(cmd, "MKDIR") == 0) {
            sscanf(buf, "%*s %s", a1);
            if (resolve_path(current_user, a1, path1, "WRITE")) {
                _mkdir(path1);
                send(c, "Directory created\n", 18, 0);
            } else {
                 send(c, "Access Denied (Write)\n", 22, 0);
            }
        }

        /* RMDIR */
        else if (strcmp(cmd, "RMDIR") == 0) {
            sscanf(buf, "%*s %s", a1);
            if (resolve_path(current_user, a1, path1, "WRITE")) {
                if (_rmdir(path1) == 0)
                    send(c, "Directory removed\n", 18, 0);
                else
                    send(c, "Directory not empty or in use\n", 30, 0);
            } else {
                send(c, "Access Denied (Write)\n", 22, 0);
            }
        }

        /* TOUCH */
        else if (strcmp(cmd, "TOUCH") == 0) {
            sscanf(buf, "%*s %s", a1);
            if (resolve_path(current_user, a1, path1, "WRITE")) {
                FILE *fp = fopen(path1, "w");
                if (!fp) send(c, "File creation failed\n", 21, 0);
                else {
                    fclose(fp);
                    send(c, "Empty file created\n", 19, 0);
                }
            } else {
                send(c, "Access Denied (Write)\n", 22, 0);
            }
        }

        /* WRITE */
        else if (strcmp(cmd, "WRITE") == 0) {
            char data[512];
            sscanf(buf, "%*s %s %[^\n]", a1, data);
            
            if (resolve_path(current_user, a1, path1, "WRITE")) {
                FileLock *l = get_file_lock(path1);
                acquire_write_lock(l, c);
                
                FILE *fp = fopen(path1, "a");
                if (!fp)
                    send(c, "File not found\n", 15, 0);
                else {
                    fprintf(fp, "%s", data);
                    fclose(fp);
                    send(c, "Write successful\n", 17, 0);
                }
                release_write_lock(l, c);
            } else {
                send(c, "Access Denied (Write)\n", 22, 0);
            }
        }

        /* READ */
        else if (strcmp(cmd, "READ") == 0) {
            sscanf(buf, "%*s %s", a1);
            
            if (resolve_path(current_user, a1, path1, "READ")) {
                FileLock *l = get_file_lock(path1);
                acquire_read_lock(l, c);
                
                FILE *fp = fopen(path1, "r");
                if (!fp)
                    send(c, "File not found\n", 15, 0);
                else {
                    fseek(fp, 0, SEEK_END);
                    long fsize = ftell(fp);
                    fseek(fp, 0, SEEK_SET);
                    
                    if (fsize >= BUF) {
                        char *file_buf = malloc(fsize + 1);
                        if (file_buf) {
                            fread(file_buf, 1, fsize, fp);
                            file_buf[fsize] = 0;
                            send(c, file_buf, fsize, 0);
                            free(file_buf);
                        } else {
                            send(c, "File too large\n", 15, 0);
                        }
                    } else {
                        char *file_buf = malloc(fsize + 1);
                        fread(file_buf, 1, fsize, fp);
                        file_buf[fsize] = 0;
                        send(c, file_buf, fsize, 0);
                        free(file_buf);
                    }
                    fclose(fp);
                }
                release_read_lock(l, c);
            } else {
                send(c, "Access Denied (Read)\n", 21, 0);
            }
        }
        
        /* UPLOAD */
        else if (strcmp(cmd, "UPLOAD") == 0) {
             long filesize = 0;
             sscanf(buf, "%*s %s %ld", a1, &filesize);
             
             if (resolve_path(current_user, a1, path1, "WRITE")) {
                if (filesize > 0) {
                    send(c, "READY", 5, 0);
                    FILE *fp = fopen(path1, "wb");
                    if (fp) {
                        char *img_buf = malloc(BUF);
                        long total_rcvd = 0;
                        int r;
                        while (total_rcvd < filesize) {
                            int to_read = (filesize - total_rcvd < BUF) ? (filesize - total_rcvd) : BUF;
                            r = recv(c, img_buf, to_read, 0);
                            if (r <= 0) break;
                            fwrite(img_buf, 1, r, fp);
                            total_rcvd += r;
                        }
                        free(img_buf);
                        fclose(fp);
                        send(c, "Upload Complete\n", 16, 0);
                    } else {
                        send(c, "Server Error\n", 13, 0);
                    }
                } else send(c, "Invalid Size\n", 13, 0);
             } else {
                 send(c, "Access Denied (Write)\n", 22, 0);
             }
        }

        /* DELETE */
        else if (strcmp(cmd, "DELETE") == 0) {
            sscanf(buf, "%*s %s", a1);
            if (resolve_path(current_user, a1, path1, "WRITE")) {
                if (remove(path1) == 0)
                    send(c, "File deleted\n", 13, 0);
                else
                    send(c, "Delete failed\n", 14, 0);
            } else {
                send(c, "Access Denied (Write)\n", 22, 0);
            }
        }

        /* DOWNLOAD */
        else if (strcmp(cmd, "DOWNLOAD") == 0) {
            sscanf(buf, "%*s %s", a1);
            if (resolve_path(current_user, a1, path1, "READ")) { // Using new resolve_path
                FILE *fp = fopen(path1, "rb");
                if (fp) {
                    fseek(fp, 0, SEEK_END);
                    long fsize = ftell(fp);
                    fseek(fp, 0, SEEK_SET);

                    char size_msg[50];
                    sprintf(size_msg, "SIZE %ld", fsize);
                    send(c, size_msg, strlen(size_msg), 0);

                    // Wait for client to be ready
                    char ack[20];
                    memset(ack, 0, 20);
                    recv(c, ack, 20, 0); 
                    if (strstr(ack, "READY")) {
                        char *fbuf = malloc(BUF);
                        size_t n;
                        while ((n = fread(fbuf, 1, BUF, fp)) > 0) {
                            send(c, fbuf, n, 0);
                        }
                        free(fbuf);
                    }
                    fclose(fp);
                } else {
                    send(c, "File not found\n", 15, 0);
                }
            } else {
                send(c, "Access Denied (Read)\n", 21, 0);
            }
        }
        
    /* STAT */ 
    else if (strcmp(cmd, "STAT") == 0) {
            sscanf(buf, "%*s %s", a1);
            if (resolve_path(current_user, a1, path1, "READ")) {
                struct stat fileStat;
                if (stat(path1, &fileStat) == 0) {
                    char detailBuf[BUF];
                    sprintf(detailBuf, "Size: %ld bytes\nMode: %o\n", fileStat.st_size, fileStat.st_mode);
                    send(c, detailBuf, strlen(detailBuf), 0);
                } else {
                    send(c, "File not found\n", 15, 0);
                }
            } else {
                send(c, "Access Denied (Read)\n", 21, 0);
            }
        }
        
        /* PUTFILE <file> <dir> */
        else if (strcmp(cmd, "PUTFILE") == 0) {
            sscanf(buf, "%*s %s %s", a1, a2);
            char src_path[512], dest_dir_path[512], final_path[512];
            
            int src_ok = resolve_path(current_user, a1, src_path, "WRITE");
            int dest_ok = resolve_path(current_user, a2, dest_dir_path, "WRITE");
            
            if (src_ok && dest_ok) {
                struct stat st_check;
                // Check source exists
                if (stat(src_path, &st_check) != 0) {
                     send(c, "Source file not found\n", 23, 0); 
                }
                // Check dest is dir
                else if (stat(dest_dir_path, &st_check) != 0 || !(st_check.st_mode & S_IFDIR)) {
                     send(c, "Destination not a folder\n", 26, 0); 
                }
                else {
                    // Extract filename
                    char *fname = strrchr(a1, '/');
                    if (!fname) fname = strrchr(a1, '\\');
                    if (!fname) fname = a1; else fname++;
                    
                    sprintf(final_path, "%s/%s", dest_dir_path, fname);
                    
                    if (rename(src_path, final_path) == 0)
                        send(c, "File moved successfully\n", 24, 0);
                    else
                        send(c, "Move failed\n", 12, 0);
                }
            } else {
                send(c, "Access Denied\n", 14, 0);
            }
        }

        /* MOVE <src> <dest> */
        else if (strcmp(cmd, "MOVE") == 0) {
             sscanf(buf, "%*s %s %s", a1, a2);
             if (resolve_path(current_user, a1, path1, "WRITE") && 
                 resolve_path(current_user, a2, path2, "WRITE")) {
                 if (rename(path1, path2) == 0)
                     send(c, "Moved successfully\n", 19, 0);
                 else
                     send(c, "Move failed\n", 12, 0);
             } else {
                 send(c, "Access Denied\n", 14, 0);
             }
        }

        /* COPY <src> <dest> */
        else if (strcmp(cmd, "COPY") == 0) {
             sscanf(buf, "%*s %s %s", a1, a2);
             if (resolve_path(current_user, a1, path1, "READ") && 
                 resolve_path(current_user, a2, path2, "WRITE")) {
                 
                 FILE *src = fopen(path1, "rb");
                 FILE *dst = fopen(path2, "wb");
                 if (src && dst) {
                     char buf[1024];
                     size_t n;
                     while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
                         fwrite(buf, 1, n, dst);
                     }
                     fclose(src);
                     fclose(dst);
                     send(c, "Copy successful\n", 16, 0);
                 } else {
                     if(src) fclose(src);
                     if(dst) fclose(dst);
                     send(c, "Copy failed\n", 12, 0);
                 }
             } else {
                 send(c, "Access Denied\n", 14, 0);
             }
        }

        else if (strcmp(cmd, "CHPASS") == 0) {
             // Existing logic ok (only affects users.txt)
             sscanf(buf, "%*s %s %s", a1, a2);
             if (change_password_file(current_user, a1, a2)) send(c, "Password changed\n", 17, 0);
             else send(c, "Change failed\n", 14, 0);
        }
        else {
            send(c, "Invalid command\n", 16, 0);
        }
    }
}

/* ---------- THREAD WRAPPER ---------- */
DWORD WINAPI ClientThread(LPVOID lpParam) {
    SOCKET client = (SOCKET)lpParam;
    handle_client(client);
    closesocket(client);
    return 0;
}

/* ---------- MAIN ---------- */
int main() {
    setbuf(stdout, NULL);
    WSADATA wsa;
    SOCKET server, client;
    struct sockaddr_in addr;
    int size = sizeof(addr);

    WSAStartup(MAKEWORD(2,2), &wsa);
    InitializeCriticalSection(&file_locks_cs);

    server = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    int bind_res = bind(server, (struct sockaddr*)&addr, sizeof(addr));
    if (bind_res == SOCKET_ERROR) {
        printf("Bind failed. Error: %d\n", WSAGetLastError());
        return 1;
    }
    
    listen(server, 5);

    printf("Server running on port %d...\n", PORT);

    while (1) {
        client = accept(server, (struct sockaddr*)&addr, &size);
        if (client == INVALID_SOCKET) {
             int err = WSAGetLastError();
             printf("Accept failed. Error: %d\n", err);
             // If socket is invalid or other fatal error, stop looping
             if (err == WSAEINVAL || err == WSAENOTSOCK || err == WSAEWOULDBLOCK) {
                 Sleep(100); // Prevent tight loop if temporary
             } else {
                 break; // Fatal
             }
             continue;
        }
        
        printf("New client connected: %d\n", (int)client);
        
        /* Create thread for the new client */
        HANDLE hThread = CreateThread(NULL, 0, ClientThread, (LPVOID)client, 0, NULL);
        if (hThread == NULL) {
            printf("Thread creation failed\n");
            closesocket(client);
        } else {
            CloseHandle(hThread); // Detach thread (we don't need to join)
        }
    }
}
