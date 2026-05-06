#include <windows.h>
#include <iostream>
#include <iomanip>


#define IOCTL_GET_PROCESS_INFO \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define MAX_PROCESSES 512

typedef struct _PROCESS_INFO {
    DWORD Pid;
    CHAR ImageName[16];
    PVOID Eprocess;
} PROCESS_INFO, * PPROCESS_INFO;

typedef struct _PROCESS_LIST_RESPONSE {
    DWORD ProcessCount;
    PROCESS_INFO Processes[MAX_PROCESSES];
} PROCESS_LIST_RESPONSE, * PPROCESS_LIST_RESPONSE;

int main() {
    HANDLE hDevice;
    PROCESS_LIST_RESPONSE response = { 0 };
    DWORD bytesReturned = 0;

    std::cout << "Opening ProcessWalker driver...\n";

    hDevice = CreateFileW(L"\\\\.\\ProcWalkDriver", GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        std::cerr << "Error: Failed to open driver (Error: " << error << ")\n\n";

        if (error == ERROR_FILE_NOT_FOUND) {
            std::cerr << "The ProcessWalker driver is not loaded.\n";
            std::cerr << "Please load it first:\n";
            std::cerr << "  sc create ProcessWalker type= kernel binPath= C:\\path\\to\\ProcessWalker.sys\n";
            std::cerr << "  sc start ProcessWalker\n";
        }
        else if (error == ERROR_ACCESS_DENIED) {
            std::cerr << "Access denied. Run as Administrator.\n";
        }

        std::cout << "\nPress Enter to exit...";
        std::cin.get();
        return 1;
    }

    std::cout << "Driver opened successfully.\n\n";
    std::cout << "Enumerating all processes...\n\n";

    BOOL success = DeviceIoControl(hDevice, IOCTL_GET_PROCESS_INFO, NULL, 0, &response,
        sizeof(response), &bytesReturned, NULL);

    if (!success) {
        std::cerr << "Error: DeviceIoControl failed (Error: " << GetLastError() << ")\n";
        CloseHandle(hDevice);
        std::cout << "\nPress Enter to exit...";
        std::cin.get();
        return 1;
    }

    // Display results
    std::cout << "========================================\n";
    std::cout << "  PROCESS LIST (" << response.ProcessCount << " processes)\n";
    std::cout << "========================================\n\n";

    std::cout << std::left
        << std::setw(8) << "PID"
        << std::setw(20) << "Process Name"
        << "EPROCESS Address\n";
    std::cout << "--------------------------------------------------------\n";

    for (DWORD i = 0; i < response.ProcessCount; i++) {
        std::cout << std::left
            << std::setw(8) << response.Processes[i].Pid
            << std::setw(20) << response.Processes[i].ImageName
            << "0x" << std::hex << std::uppercase
            << (ULONG_PTR)response.Processes[i].Eprocess
            << std::dec << "\n";
    }

    std::cout << "--------------------------------------------------------\n";
    std::cout << "\nTotal: " << response.ProcessCount << " processes enumerated\n\n";

    CloseHandle(hDevice);

    std::cout << "Press Enter to exit...";
    std::cin.get();

    return 0;
}
