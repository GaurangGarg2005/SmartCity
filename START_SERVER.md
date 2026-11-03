# How to Run the SmartCity Server

This guide contains the exact commands and scripts to start the SmartCity backend and web interface on your own machine.

---

## ⚡ Method 1: One-Click Startup (Recommended)

Simply double-click the **`start_server.bat`** file located in `D:\SmartCity`, or run it from any terminal:

```cmd
start_server.bat
```

*(This automatically sets the MinGW runtime PATH, verifies the executable, compiles it if missing, and starts the server).*

---

## 💻 Method 2: Running via Command Prompt (CMD)

Open **Command Prompt** and run these 3 commands:

```cmd
cd /d D:\SmartCity
set PATH=C:\msys64\mingw64\bin;%PATH%
build\src\smartcity_server.exe
```

---

## 🟦 Method 3: Running via PowerShell

Open **PowerShell** and run:

```powershell
cd D:\SmartCity
$env:PATH = "C:\msys64\mingw64\bin;" + $env:PATH
.\build\src\smartcity_server.exe
```

*(Or run `.\start_server.ps1`)*

---

## 🐧 Method 4: Running via Git Bash or MSYS2 Bash

```bash
cd /d/SmartCity
export PATH="/c/msys64/mingw64/bin:$PATH"
./build/src/smartcity_server.exe
```

---

## 🌐 Accessing the Application

Once the server prints `SmartCity/1.0 server is running at http://0.0.0.0:8080`:

1. Open your browser (Opera / Chrome / Edge) and navigate to:
   **[http://localhost:8080](http://localhost:8080)**
2. Press `Ctrl + F5` (hard refresh) if you previously had the page open to ensure the latest assets are loaded.

---

## 🛑 How to Stop the Server

In the terminal window where the server is running, press:
```
Ctrl + C
```

---

## 🔨 How to Recompile After Modifying C++ Code

If you ever edit files in `backend/src/` and need to recompile the binary:

```cmd
cd /d D:\SmartCity
python tools\build_native.py
```

This compiles all C++ sources with `g++` and creates the updated `build\src\smartcity_server.exe`.

---

## ❓ Troubleshooting

| Issue | Solution |
|---|---|
| **`error while loading shared libraries`** / Missing DLLs | Make sure `C:\msys64\mingw64\bin` is added to your `PATH` before running the executable. |
| **`Address already in use: 8080`** | Another instance of `smartcity_server.exe` is already running. Open Task Manager and end `smartcity_server.exe`, or run `taskkill /f /im smartcity_server.exe` in CMD. |
| **Location Access Denied in Opera** | Click the red 🚫 icon next to `localhost:8080` in the address bar, change Location to **Allow**, and reload (`Ctrl+R`). You can also click `⊙` or `📍` to pin your vehicle directly on the map. |

