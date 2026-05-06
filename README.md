# Windows Process Walker — Kernel Driver & Usermode Client

A Windows kernel driver that enumerates all running processes by directly walking the `EPROCESS` `ActiveProcessLinks` linked list. Includes a usermode client that communicates with the driver via a custom IOCTL interface.

Built as a learning project to understand Windows kernel internals, driver development, and the boundary between kernel and user mode.

---

## What This Does

The kernel driver (`ProcWalker.sys`) exposes a device object that usermode applications can open and send IOCTLs to. When queried, the driver walks the kernel's internal process list by traversing `EPROCESS.ActiveProcessLinks` — the same circular doubly-linked list the Windows kernel uses to track all active processes — and returns the PID, image name, and `EPROCESS` pointer for each process.

The usermode client (`Client.exe`) opens the driver device, sends the IOCTL, and displays all running processes in a formatted table including their PID, name, and kernel `EPROCESS` address.

---

## Screenshots

Driver loaded and running:

<img width="725" height="245" alt="image" src="https://github.com/user-attachments/assets/c4481951-9515-443d-92b4-405096a52c42" />

Process list output — system processes with kernel EPROCESS addresses:

<img width="1020" height="583" alt="image" src="https://github.com/user-attachments/assets/8a5097a8-32b8-40d0-9e5f-d20abdeb29b6" />

<img width="1022" height="580" alt="image" src="https://github.com/user-attachments/assets/4d02a8bb-25f3-41b5-b6a8-dd7a5bb4add7" />




---

## What I Learned

- **EPROCESS structure** — how the Windows kernel represents processes internally, and how fields like `UniqueProcessId`, `ImageFileName`, and `ActiveProcessLinks` are laid out in memory
- **Kernel driver development** — creating device objects, symbolic links, handling IRPs, and implementing `DriverEntry`, `DriverUnload`, and major function dispatch routines
- **IOCTL communication** — how usermode and kernel mode communicate through `DeviceIoControl` and buffered I/O
- **Kernel/usermode boundary** — how to safely pass data structures across the boundary using shared definitions

---


## How It Works

### Kernel Side
1. `DriverEntry` creates a device object (`\Device\ProcWalkDriver`) and a symbolic link (`\\.\ProcWalkDriver`) so usermode can open it
2. On receiving `IOCTL_GET_PROCESS_INFO`, the driver calls `WalkProcessList`
3. `WalkProcessList` starts from the current process (`PsGetCurrentProcess`) and traverses `ActiveProcessLinks` — a circular `LIST_ENTRY` embedded inside every `EPROCESS`
4. For each entry, it calculates the `EPROCESS` base pointer, reads the PID and image name, and stores them in the output buffer
5. The populated buffer is returned to usermode via buffered I/O

### Usermode Side
1. Opens `\\.\ProcWalkDriver` with `CreateFile`
2. Sends `IOCTL_GET_PROCESS_INFO` via `DeviceIoControl`
3. Reads the returned `PROCESS_LIST_RESPONSE` structure and displays all processes in a formatted table — PID, image name, and `EPROCESS` kernel address

---

## ⚠️ Important: Hardcoded EPROCESS Offsets

The driver uses hardcoded offsets to locate fields within the `EPROCESS` structure:

```cpp
ULONG g_UniqueProcessIdOffset    = 0x440;
ULONG g_ActiveProcessLinksOffset = 0x448;
ULONG g_ImageFileNameOffset      = 0x5A8;
```

**These offsets are verified for Windows 10 22H2 (build 19045).** The `EPROCESS` structure is undocumented and its layout changes between Windows versions and even between updates. Running this driver on a different build without updating the offsets will likely result in a system crash (BSOD).

To find the correct offsets for your build, use WinDbg:
```
bcdedit /debug on
bcdedit /dbgsettings local
```
Reboot, then open WinDbg as Administrator → File → Attach to kernel → Local, and run:
```
dt nt!_EPROCESS
```

Look for `UniqueProcessId`, `ActiveProcessLinks`, and `ImageFileName` and note their `+0x...` values.

---

## Requirements

- Windows 10 22H2 (build 19045) — see offset note above for other versions
- Visual Studio with WDK (Windows Driver Kit) installed
- Test signing enabled (driver is unsigned)


### Enable Test Signing
```cmd
bcdedit /set testsigning on
```
Reboot after running this command.

---

## Building

1. Open `ProcWalker.sln` in Visual Studio with WDK installed
2. Build the `ProcWalker` project — produces `ProcWalker.sys`
3. Build the `Client` project — produces `Client.exe`

---

## Loading the Driver

```cmd
sc create ProcWalkDriver type= kernel binPath= "C:\path\to\ProcWalker.sys"
sc start ProcWalkDriver
```

To unload:
```cmd
sc stop ProcWalkDriver
sc delete ProcWalkDriver
```

---

## Purpose

This project was built purely for educational purposes — to understand how the Windows kernel manages processes internally and how kernel drivers interact with usermode applications.

## References & Further Reading

- *Windows Internals, Part 1* — Yosifovich, Ionescu, Russinovich, Solomon
- [Windows Driver Kit Documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/)
