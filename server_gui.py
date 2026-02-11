import customtkinter as ctk
import subprocess
import threading
import sys
import os
import signal
import time
import platform

# --- Configuration ---
SERVER_EXE = "server.exe"  # Assumes in same directory
USERS_FILE = "users.txt"
LOG_FILE = "server_log.txt"
STORAGE_DIR = "storage"

ctk.set_appearance_mode("Dark")
ctk.set_default_color_theme("blue")

class ServerManagerApp(ctk.CTk):
    def __init__(self):
        super().__init__()

        self.title("RemoteFS Server Control")
        self.geometry("900x650")
        self.process = None

        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)

        # --- Top Control Bar ---
        self.top_bar = ctk.CTkFrame(self, fg_color="transparent")
        self.top_bar.grid(row=0, column=0, sticky="ew", padx=20, pady=20)

        self.status_indicator = ctk.CTkLabel(self.top_bar, text="⚫ Stopped", font=("Roboto", 16, "bold"), text_color="gray")
        self.status_indicator.pack(side="left", padx=10)

        # Buttons
        self.btn_open_storage = ctk.CTkButton(self.top_bar, text="Open Storage Folder", command=self.open_storage_folder, fg_color="#F39C12", text_color="black")
        self.btn_open_storage.pack(side="right", padx=10)

        self.btn_stop = ctk.CTkButton(self.top_bar, text="Stop Server", command=self.stop_server, fg_color="#C0392B", state="disabled")
        self.btn_stop.pack(side="right", padx=10)

        self.btn_start = ctk.CTkButton(self.top_bar, text="Start Server", command=self.start_server, fg_color="#27AE60")
        self.btn_start.pack(side="right", padx=10)

        # --- Tab View ---
        self.tab_view = ctk.CTkTabview(self)
        self.tab_view.grid(row=1, column=0, sticky="nsew", padx=20, pady=(0, 20))

        self.tab_console = self.tab_view.add("Console Output")
        self.tab_logs = self.tab_view.add("Command Logs")
        self.tab_users = self.tab_view.add("Registered Users")
        self.tab_storage_tree = self.tab_view.add("Storage Tree")

        # 1. Console Tab
        self.console_text = ctk.CTkTextbox(self.tab_console, font=("Consolas", 12), text_color="#2ECC71", fg_color="#1E1E1E")
        self.console_text.pack(fill="both", expand=True)
        self.console_text.configure(state="disabled")

        # 2. Command Logs Tab (server_log.txt)
        self.logs_text = ctk.CTkTextbox(self.tab_logs, font=("Consolas", 12))
        self.logs_text.pack(fill="both", expand=True, pady=(0, 10))
        ctk.CTkButton(self.tab_logs, text="Refresh Logs", command=self.refresh_logs, height=30).pack(fill="x")

        # 3. Users Tab (users.txt)
        self.users_text = ctk.CTkTextbox(self.tab_users, font=("Roboto", 14))
        self.users_text.pack(fill="both", expand=True, pady=(0, 10))
        ctk.CTkButton(self.tab_users, text="Refresh Users", command=self.refresh_users_list, height=30).pack(fill="x")

        # 4. Storage Tree Tab
        self.storage_text = ctk.CTkTextbox(self.tab_storage_tree, font=("Consolas", 12))
        self.storage_text.pack(fill="both", expand=True, pady=(0, 10))
        ctk.CTkButton(self.tab_storage_tree, text="Refresh Tree", command=self.refresh_storage_tree, height=30).pack(fill="x")
        
        # Initial Refresh
        self.refresh_users_list()
        self.refresh_logs()
        self.refresh_storage_tree()
        
        # Periodic Status Check
        self.check_process_status()

    # --- Server Process Management ---
    def start_server(self):
        if self.process: return

        if not os.path.exists(SERVER_EXE):
            self.log_console(f"[GUI ERROR] {SERVER_EXE} not found.\n")
            return

        try:
            startupinfo = subprocess.STARTUPINFO()
            startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            
            self.process = subprocess.Popen(
                [SERVER_EXE], 
                stdout=subprocess.PIPE, 
                stderr=subprocess.PIPE,
                stdin=subprocess.PIPE,
                text=True,
                bufsize=1,
                startupinfo=startupinfo
            )
            
            self.btn_start.configure(state="disabled", fg_color="gray")
            self.btn_stop.configure(state="normal", fg_color="#C0392B")
            self.status_indicator.configure(text="🟢 Running", text_color="#2ECC71")
            
            self.log_console("[GUI] Server started.\n")
            threading.Thread(target=self.read_output, daemon=True).start()

        except Exception as e:
            self.log_console(f"[GUI] Start failed: {e}\n")

    def stop_server(self):
        if self.process:
            try:
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(self.process.pid)], capture_output=True)
            except: pass
            self.process = None
            
        self.btn_start.configure(state="normal", fg_color="#27AE60")
        self.btn_stop.configure(state="disabled", fg_color="gray")
        self.status_indicator.configure(text="⚫ Stopped", text_color="gray")
        self.log_console("[GUI] Server stopped.\n")

    def read_output(self):
        if not self.process: return
        try:
            for line in iter(self.process.stdout.readline, ''):
                if line: self.after(0, lambda l=line: self.log_console(l))
                else: break
        except: pass

    def check_process_status(self):
        if self.process and self.process.poll() is not None:
            self.stop_server()
            self.log_console("[GUI] Server exited unexpectedly.\n")
        self.after(2000, self.check_process_status)

    def log_console(self, msg):
        self.console_text.configure(state="normal")
        self.console_text.insert("end", msg)
        self.console_text.see("end")
        self.console_text.configure(state="disabled")

    # --- Data Viewers ---
    def refresh_users_list(self):
        content = "(No users found)"
        if os.path.exists(USERS_FILE):
            try:
                with open(USERS_FILE, "r") as f: content = f.read()
            except Exception as e: content = f"Error reading file: {e}"
        
        self.users_text.configure(state="normal")
        self.users_text.delete("1.0", "end")
        self.users_text.insert("0.0", content)
        self.users_text.configure(state="disabled")

    def refresh_logs(self):
        content = "(No logs found)"
        if os.path.exists(LOG_FILE):
            try:
                with open(LOG_FILE, "r") as f: content = f.read()
            except Exception as e: content = f"Error reading file: {e}"

        self.logs_text.configure(state="normal")
        self.logs_text.delete("1.0", "end")
        self.logs_text.insert("0.0", content)
        self.logs_text.see("end") # Auto-scroll to bottom
        self.logs_text.configure(state="disabled")

    def refresh_storage_tree(self):
        tree_str = ""
        if os.path.exists(STORAGE_DIR):
            for root, dirs, files in os.walk(STORAGE_DIR):
                level = root.replace(STORAGE_DIR, '').count(os.sep)
                indent = ' ' * 4 * (level)
                tree_str += f"{indent}{os.path.basename(root)}/\n"
                subindent = ' ' * 4 * (level + 1)
                for f in files:
                    tree_str += f"{subindent}{f}\n"
        else:
            tree_str = "(Storage directory not found)"

        self.storage_text.configure(state="normal")
        self.storage_text.delete("1.0", "end")
        self.storage_text.insert("0.0", tree_str)
        self.storage_text.configure(state="disabled")

    def open_storage_folder(self):
        path = os.path.abspath(STORAGE_DIR)
        if not os.path.exists(path):
            os.makedirs(path)
        os.startfile(path)

    def on_close(self):
        self.stop_server()
        self.destroy()

if __name__ == "__main__":
    app = ServerManagerApp()
    app.protocol("WM_DELETE_WINDOW", app.on_close)
    app.mainloop()
