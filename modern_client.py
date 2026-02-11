import customtkinter as ctk
import socket
import threading
import os
from tkinter import filedialog, messagebox

# --- Configuration ---
SERVER_IP = '127.0.0.1'
SERVER_PORT = 8080
BUFFER_SIZE = 1024

# --- Theme Setup ---
ctk.set_appearance_mode("Dark")
ctk.set_default_color_theme("blue")

class ModernClient(ctk.CTk):
    def __init__(self):
        super().__init__()

        # Window Setup
        self.title("RemoteFS | Modern Client")
        self.geometry("950x650")
        self.resizable(True, True)

        # Network State
        self.sock = None
        self.username = None
        self.is_connected = False

        # Grid Configuration
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(0, weight=1)

        # Initialize Frames
        self.login_frame = LoginFrame(self, self.attempt_login, self.attempt_register)
        self.main_frame = MainFrame(self, self.send_command, self.logout, self.upload_file)
        self.loading_frame = LoadingFrame(self, self.connect_to_server)

        # Start
        self.show_frame("loading")
        self.after(500, self.connect_to_server)

    def show_frame(self, name):
        self.login_frame.grid_forget()
        self.main_frame.grid_forget()
        self.loading_frame.grid_forget()

        if name == "login":
            self.login_frame.grid(row=0, column=0, sticky="nsew", padx=20, pady=20)
        elif name == "main":
            self.main_frame.grid(row=0, column=0, sticky="nsew")
        elif name == "loading":
            self.loading_frame.grid(row=0, column=0, sticky="nsew")

    def connect_to_server(self):
        try:
            if self.sock: self.sock.close()
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(5)
            self.sock.connect((SERVER_IP, SERVER_PORT))
            self.sock.settimeout(None)
            self.is_connected = True
            print(f"Connected to {SERVER_IP}:{SERVER_PORT}")
            self.show_frame("login")
            return True
        except Exception as e:
            print(f"Connection failed: {e}")
            self.loading_frame.set_error(f"Could not reach server.\nEnsure 'server.exe' is running.\n({e})")
            return False

    def send_command(self, cmd, receive_response=True):
        if not self.sock:
            return "Error: Not connected"
        try:
            full_cmd = cmd + "\n"
            self.sock.send(full_cmd.encode())
            if receive_response:
                response = self.sock.recv(BUFFER_SIZE).decode('utf-8', errors='ignore').strip()
                return response
            return "Sent"
        except Exception as e:
            self.handle_disconnect(e)
            return f"Error: {e}"

    def handle_disconnect(self, e):
        print(f"Connection lost: {e}")
        self.is_connected = False
        self.sock = None
        self.show_frame("loading")
        self.loading_frame.set_error("Connection Lost. Reconnecting...")
        self.connect_to_server()

    # --- Authentication ---
    def attempt_login(self, user, pwd):
        resp = self.send_command(f"LOGIN {user} {pwd}")
        if "successful" in resp.lower():
            self.username = user
            self.show_frame("main")
            self.main_frame.update_user(user)
            self.main_frame.refresh_files()
            return True, resp
        return False, resp

    def attempt_register(self, user, pwd):
        resp = self.send_command(f"REGISTER {user} {pwd}")
        return "successful" in resp.lower(), resp

    def logout(self):
        self.send_command("LOGOUT")
        self.username = None
        self.login_frame.clear_inputs()
        self.show_frame("login")

    # --- File Transfer Protocols (Matching client.c) ---
    def upload_file(self):
        # 1. Select File
        filepath = filedialog.askopenfilename()
        if not filepath: return
        
        filename = os.path.basename(filepath)
        filesize = os.path.getsize(filepath)

        # 2. Send Command: UPLOAD <filename> <size>
        resp = self.send_command(f"UPLOAD {filename} {filesize}")
        
        # 3. Check Packet Approval
        if "READY" in resp:
            self.main_frame.log_output(f"Allocating stream for {filename} ({filesize} bytes)...")
            try:
                with open(filepath, 'rb') as f:
                    bytes_sent = 0
                    while True:
                        chunk = f.read(BUFFER_SIZE)
                        if not chunk: break
                        self.sock.send(chunk)
                        bytes_sent += len(chunk)
                
                # 4. Wait for final confirmation
                final_resp = self.sock.recv(BUFFER_SIZE).decode().strip()
                self.main_frame.log_output(f"Upload Status: {final_resp}")
                self.main_frame.refresh_files()
                
            except Exception as e:
                self.main_frame.log_output(f"Upload failed: {e}")
        else:
            self.main_frame.log_output(f"Server rejected upload: {resp}")




# --- GUI Components ---

class LoadingFrame(ctk.CTkFrame):
    def __init__(self, master, retry_callback):
        super().__init__(master)
        self.retry_callback = retry_callback
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(0, weight=1)

        self.center_box = ctk.CTkFrame(self, fg_color="transparent")
        self.center_box.grid(row=0, column=0)

        self.label = ctk.CTkLabel(self.center_box, text="Connecting to RemoteFS...", font=("Roboto", 24))
        self.label.pack(pady=20)

        self.spinner = ctk.CTkProgressBar(self.center_box, width=200, mode="indeterminate")
        self.spinner.pack(pady=10)
        self.spinner.start()

        self.retry_btn = ctk.CTkButton(self.center_box, text="Retry", command=self.retry, fg_color="#F39C12")

    def set_error(self, msg):
        self.spinner.stop()
        self.spinner.pack_forget()
        self.label.configure(text=msg, text_color="#E74C3C")
        self.retry_btn.pack(pady=10)

    def retry(self):
        self.label.configure(text="Connecting...", text_color=("black", "white"))
        self.retry_btn.pack_forget()
        self.spinner.pack(pady=10)
        self.spinner.start()
        self.master.after(100, self.retry_callback)


class LoginFrame(ctk.CTkFrame):
    def __init__(self, master, login_cb, register_cb):
        super().__init__(master)
        self.login_cb = login_cb
        self.register_cb = register_cb

        self.place(relx=0.5, rely=0.5, anchor="center")
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(0, weight=1)

        self.card = ctk.CTkFrame(self, width=350, height=450, corner_radius=15)
        self.card.grid(row=0, column=0)
        self.card.grid_propagate(False)

        ctk.CTkLabel(self.card, text="RemoteFS", font=("Roboto", 30, "bold")).pack(pady=(40, 10))
        
        self.user_entry = ctk.CTkEntry(self.card, placeholder_text="Username", width=220)
        self.user_entry.pack(pady=10)
        self.pass_entry = ctk.CTkEntry(self.card, placeholder_text="Password", show="*", width=220)
        self.pass_entry.pack(pady=10)

        ctk.CTkButton(self.card, text="Login", width=220, command=self.do_login).pack(pady=10)
        ctk.CTkButton(self.card, text="Register", width=220, fg_color="transparent", border_width=1, command=self.do_register).pack(pady=5)

        self.status = ctk.CTkLabel(self.card, text="", font=("Roboto", 12))
        self.status.pack(pady=10)

    def do_login(self):
        u, p = self.user_entry.get(), self.pass_entry.get()
        if u and p:
            s, m = self.login_cb(u, p)
            if not s: self.status.configure(text=m, text_color="#E74C3C")
        else:
            self.status.configure(text="Fields cannot be empty", text_color="#E74C3C")

    def do_register(self):
        u, p = self.user_entry.get(), self.pass_entry.get()
        if u and p:
            s, m = self.register_cb(u, p)
            self.status.configure(text=m, text_color="#2ECC71" if s else "#E74C3C")

    def clear_inputs(self):
        self.user_entry.delete(0, 'end')
        self.pass_entry.delete(0, 'end')
        self.status.configure(text="")


class MainFrame(ctk.CTkFrame):
    def __init__(self, master, cmd_cb, logout_cb, upload_cb):
        super().__init__(master)
        self.cmd_cb = cmd_cb
        self.logout_cb = logout_cb
        self.upload_cb = upload_cb

        # Layout
        self.grid_columnconfigure(1, weight=1)
        self.grid_rowconfigure(0, weight=1)

        # --- Sidebar (Container) ---
        self.sidebar = ctk.CTkFrame(self, width=300, corner_radius=0)
        self.sidebar.grid(row=0, column=0, sticky="nsew")
        self.sidebar.grid_rowconfigure(2, weight=1) # Tabview expands

        # Header
        self.logo = ctk.CTkLabel(self.sidebar, text="RemoteFS", font=("Roboto", 24, "bold"))
        self.logo.grid(row=0, column=0, padx=20, pady=(20, 5))
        self.user_label = ctk.CTkLabel(self.sidebar, text="Guest", font=("Roboto", 14), text_color="gray")
        self.user_label.grid(row=1, column=0, padx=20, pady=(0, 10))

        # --- Tab View for Categories ---
        self.tabs = ctk.CTkTabview(self.sidebar, width=280)
        self.tabs.grid(row=2, column=0, padx=10, pady=10, sticky="nsew")
        
        # Create Tabs
        self.tab_files = self.tabs.add("Files")
        self.tab_folders = self.tabs.add("Folders")
        self.tab_account = self.tabs.add("Account")

        # Configure Grid for Tabs (2 columns)
        for t in [self.tab_files, self.tab_folders, self.tab_account]:
            t.grid_columnconfigure(0, weight=1)
            t.grid_columnconfigure(1, weight=1)

        # --- Define Buttons ---
        # Format: (Tab, Text, Command, GridRow, GridCol, Color)
        buttons_config = [
            # Files Tab
            (self.tab_files, "Refresh (LS)", self.refresh_files, 0, 0, "#2980B9"),
            (self.tab_files, "List All (LSR)", self.list_all, 0, 1, "#2980B9"),
            (self.tab_files, "File Details", self.stat_file, 1, 0, None),
            (self.tab_files, "Read File", self.read_file, 1, 1, None),
            (self.tab_files, "Write File", self.write_file, 2, 0, None),
            (self.tab_files, "Create File", self.touch_file, 2, 1, None),
            (self.tab_files, "Delete File", self.del_file, 3, 0, "#C0392B"),
            (self.tab_files, "Copy File", self.copy_file, 3, 1, None),
            (self.tab_files, "Move/Rename", self.move_file, 4, 0, None),
            (self.tab_files, "Upload File", self.upload_cb, 5, 0, "#27AE60"), # Spans 2 cols manually below

            # Folders Tab
            (self.tab_folders, "New Folder", self.mk_dir, 0, 0, None),
            (self.tab_folders, "Remove Folder", self.rm_dir, 0, 1, "#C0392B"),
            (self.tab_folders, "Put File in Dir", self.put_file, 1, 0, None),
            (self.tab_folders, "Share Item", self.share_folder, 2, 0, "#F39C12"),
            (self.tab_folders, "Shared With Me", self.list_shared, 2, 1, "#F39C12"),
            (self.tab_folders, "Explore Path", self.explore_dir, 3, 0, "#2980B9"),

            # Account Tab
            (self.tab_account, "Change Pass", self.ch_pass, 0, 0, None),
            (self.tab_account, "Logout", self.logout_cb, 1, 0, "#C0392B"),
        ]

        for item in buttons_config:
            parent, text, cmd, r, c, color = item
            fg = color if color else "transparent"
            hover = None
            
            # Special case for Logout/Upload spanning
            if text == "Upload File":
                b = ctk.CTkButton(parent, text=text, command=cmd, fg_color=fg, height=35)
                b.grid(row=r, column=0, columnspan=2, sticky="ew", padx=5, pady=5)
                continue
            
            if text == "Logout":
                b = ctk.CTkButton(parent, text=text, command=cmd, fg_color=fg, height=35)
                b.grid(row=r, column=0, columnspan=2, sticky="ew", padx=5, pady=20)
                continue
            
            # Center 'Explore Path' if needed or just let it sit
            if text == "Explore Path":
                 b = ctk.CTkButton(parent, text=text, command=cmd, fg_color=fg, height=35)
                 b.grid(row=r, column=0, columnspan=2, sticky="ew", padx=5, pady=5)
                 continue

            b = ctk.CTkButton(parent, text=text, command=cmd, fg_color=fg, height=35, anchor="center")
            if color is None: b.configure(border_width=1, border_color="#555") # Outline for standard buttons
            b.grid(row=r, column=c, sticky="ew", padx=5, pady=5)


        # --- Content Area ---
        self.content = ctk.CTkFrame(self, fg_color="transparent")
        self.content.grid(row=0, column=1, sticky="nsew", padx=20, pady=20)

        ctk.CTkLabel(self.content, text="Console Output", font=("Roboto", 16, "bold")).pack(anchor="w")
        
        self.console = ctk.CTkTextbox(self.content, font=("Consolas", 13), wrap="word")
        self.console.pack(fill="both", expand=True, pady=(5, 0))
        self.console.configure(state="disabled")
        
        # Clear Console & Help
        action_bar = ctk.CTkFrame(self.content, fg_color="transparent")
        action_bar.pack(fill="x", pady=(5,0))
        ctk.CTkButton(action_bar, text="Clear Console", command=self.clear_console, 
                      height=24, width=100, fg_color="#555").pack(side="right")

    def update_user(self, u):
        self.user_label.configure(text=f"Logged in as: {u}")

    def log_output(self, msg):
        self.console.configure(state="normal")
        self.console.insert("end", f">>>\n{msg}\n\n")
        self.console.see("end")
        self.console.configure(state="disabled")

    def clear_console(self):
        self.console.configure(state="normal")
        self.console.delete("1.0", "end")
        self.console.configure(state="disabled")

    def _ask_input(self, title, msg):
        return ctk.CTkInputDialog(text=msg, title=title).get_input()

    # --- Command Wrapper with Auto-Refresh ---
    def cmd_cb(self, cmd):
        # We need to send this to the server
        try:
            # We must use a new socket connection for each command in this specific architecture 
            # OR reuse the existing one if it's persistent. 
            # Looking at client.c, it seems commands are one-off or persistent?
            # The python client uses `self.client_socket`.
            
            if not self.client_socket:
                return "Not Connected"
                
            self.client_socket.send(cmd.encode())
            response = self.client_socket.recv(4096).decode()
            
            # Auto-Refresh if command modifies state OR user requested READ
            if any(x in cmd.split()[0] for x in ["UPLOAD", "DELETE", "WRITE", "MKDIR", "RMDIR", "TOUCH", "COPY", "MOVE", "PUTFILE", "READ"]):
                 # We can't immediately call refresh because it might mix responses if not careful.
                 # But since we are synchronous here:
                 self.after(500, self.refresh_files)

            return response
        except Exception as e:
            return f"Error: {e}"

    def refresh_files(self): self.log_output(self.cmd_cb("LS"))
    def list_all(self): self.log_output(self.cmd_cb("LSR"))
    def list_shared(self): self.log_output(self.cmd_cb("SHARED_WITH_ME"))
    
    def explore_dir(self):
        path = self._ask_input("Explore Folder", "Enter path OR 'SHARED/user/folder':")
        if path:
            self.log_output(self.cmd_cb(f"LSR {path}"))

    def share_folder(self):
        raw_input = self._ask_input("Share Folder", "Enter: Folder User Permission\n(e.g. 'docs user2 READ')")
        if not raw_input: return
        parts = raw_input.strip().split()
        if len(parts) == 3:
            self.log_output(self.cmd_cb(f"SHARE {parts[0]} WITH {parts[1]} {parts[2]}"))
        elif len(parts) == 4 and parts[1].upper() == "WITH":
            self.log_output(self.cmd_cb(f"SHARE {raw_input}"))
        else:
             self.log_output("Error: Invalid format. Use 'Folder User Perm'")
             
    # Update read/write to hint at shared paths
    def read_file(self):
        f = self._ask_input("Read File", "Enter Filename (or SHARED/user/folder/file):")
        if f: self.log_output(self.cmd_cb(f"READ {f}"))
        
    def write_file(self):
        f = self._ask_input("Write File", "Enter Filename (or SHARED/user/folder/file):")
        if f:
            c = self._ask_input("Write Content", "Enter text to append:")
            if c: self.log_output(self.cmd_cb(f"WRITE {f} {c}"))

    def mk_dir(self):
        val = self._ask_input("New Folder", "Folder Name:")
        if val: self.log_output(self.cmd_cb(f"MKDIR {val}"))

    def rm_dir(self):
        val = self._ask_input("Remove Folder", "Folder Name:")
        if val: self.log_output(self.cmd_cb(f"RMDIR {val}"))

    def touch_file(self):
        val = self._ask_input("New File", "Filename:")
        if val: self.log_output(self.cmd_cb(f"TOUCH {val}"))

    def del_file(self):
        val = self._ask_input("Delete File", "Filename:")
        if val: self.log_output(self.cmd_cb(f"DELETE {val}"))

    def stat_file(self):
        val = self._ask_input("File Details", "Filename:")
        if val: self.log_output(self.cmd_cb(f"STAT {val}"))

    # (Duplicates removed)

    def copy_file(self):
        val = self._ask_input("Copy File", "SourceFilename DestinationFilename\n(e.g. file1.txt file2.txt)")
        if val: self.log_output(self.cmd_cb(f"COPY {val}"))

    def move_file(self):
        val = self._ask_input("Move File", "SourceFilename DestinationFilename\n(e.g. file1.txt newname.txt)")
        if val: self.log_output(self.cmd_cb(f"MOVE {val}"))

    def put_file(self):
        val = self._ask_input("Put File", "Filename DirectoryName\n(e.g. file.txt docs)")
        if val: self.log_output(self.cmd_cb(f"PUTFILE {val}"))
    
    def ch_pass(self):
        val = self._ask_input("Change Password", "OldPassword NewPassword")
        if val: self.log_output(self.cmd_cb(f"CHPASS {val}"))

if __name__ == "__main__":
    app = ModernClient()
    app.mainloop()
