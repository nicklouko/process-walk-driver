#include <ntddk.h>

#define DEVICE_NAME L"\\Device\\ProcWalkDriver"
#define SYMLINK_NAME L"\\??\\ProcWalkDriver"


#define IOCTL_GET_PROCESS_INFO \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define MAX_PROCESSES 512

typedef struct _PROCESS_INFO {
	ULONG Pid;
	CHAR ImageName[16];
	PVOID Eprocess;
} PROCESS_INFO, *PPROCESS_INFO;

typedef struct _PROCESS_LIST_RESPONSE {
	ULONG ProcessCount;
	PROCESS_INFO Processes[MAX_PROCESSES];
} PROCESS_LIST_RESPONSE, *PPROCESS_LIST_RESPONSE;

ULONG g_ActiveProcessLinksOffset = 0x448;// 0x1D8(windows 11);
ULONG g_ImageFileNameOffset = 0x5A8; //0x338;
ULONG g_UniqueProcessIdOffset = 0x440; //0x1D0;

VOID DriverUnload(PDRIVER_OBJECT DriverObject);
NTSTATUS DriverCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS DriverDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS WalkProcessList(PPROCESS_LIST_RESPONSE Response);

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
	UNREFERENCED_PARAMETER(RegistryPath);
	NTSTATUS status;
	PDEVICE_OBJECT deviceObject = NULL;	
	UNICODE_STRING deviceName, symLinkName;

	RtlInitUnicodeString(&deviceName, DEVICE_NAME);
	status = IoCreateDevice(DriverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN,
		FILE_DEVICE_SECURE_OPEN, FALSE, &deviceObject);

	if (!NT_SUCCESS(status)) {
		return status;		
	}

	KdPrint(("ProcWalk Driver: Device Created successfully"));

	RtlInitUnicodeString(&symLinkName, SYMLINK_NAME);
	status = IoCreateSymbolicLink(&symLinkName, &deviceName);

	if (!NT_SUCCESS(status)) {
		return status;	
	}

	KdPrint(("ProcWalk Driver: SymLink Created successfully"));

	DriverObject->DriverUnload = DriverUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverCreateClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = DriverCreateClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DriverDeviceControl;


	KdPrint(("ProcWalk Driver: Driver loaded successfully"));

	return status;
}


NTSTATUS DriverDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);

	PIO_STACK_LOCATION	stack = IoGetCurrentIrpStackLocation(Irp);
	NTSTATUS status = STATUS_SUCCESS;
	ULONG bytesReturned = 0;

	ULONG controlCode = stack->Parameters.DeviceIoControl.IoControlCode;
	//PVOID inputBuffer = Irp->AssociatedIrp.SystemBuffer;
	PVOID outputBuffer = Irp->AssociatedIrp.SystemBuffer;

	//ULONG inputSize = stack->Parameters.DeviceIoControl.InputBufferLength;
	ULONG outputSize = stack->Parameters.DeviceIoControl.OutputBufferLength;

	if (controlCode == IOCTL_GET_PROCESS_INFO) {
		if (outputSize < sizeof(PROCESS_LIST_RESPONSE)) {
			KdPrint(("ProcessWalker: Output buffer too small\n"));
			status= STATUS_BUFFER_TOO_SMALL;
		}
		else {
			PPROCESS_LIST_RESPONSE response = (PPROCESS_LIST_RESPONSE)outputBuffer;
			KdPrint(("ProcessWalker: Walking process list...\n"));
			status = WalkProcessList(response);

			if (NT_SUCCESS(status)) {
				bytesReturned = sizeof(PROCESS_LIST_RESPONSE);
				KdPrint(("ProcessWalker: Successfully enumerated %lu processes\n", response->ProcessCount));
			}
			else {
				KdPrint(("ProcessWalker: Failed to walk process List\n"));
			}
		}
	}
	else {
		status = STATUS_INVALID_DEVICE_REQUEST;
	}
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = bytesReturned;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}


NTSTATUS WalkProcessList(PPROCESS_LIST_RESPONSE Response) {
	NTSTATUS status;
	PEPROCESS currentProcess = NULL;
	PEPROCESS startProcess = NULL;
	PLIST_ENTRY listEntry = NULL;
	PLIST_ENTRY startListEntry = NULL;
	ULONG processCount = 0;

	RtlZeroMemory(Response, sizeof(PROCESS_LIST_RESPONSE));
	//GET the current process as our starting point

	currentProcess = PsGetCurrentProcess();
	if (!currentProcess) {
		KdPrint(("ProcessWalker: Failed to get current process\n"));
		return STATUS_UNSUCCESSFUL;
	}

	startProcess = currentProcess;

	KdPrint(("ProcessWalker: Starting from EPROCESS at 0x%p\n", currentProcess));

	__try {
		// Get the ActiveProcessLinks LIST_ENTRY from the current process
		listEntry = (PLIST_ENTRY)((ULONG_PTR)currentProcess + g_ActiveProcessLinksOffset);
		startListEntry = listEntry;

		//Walk the circular linked list
		do {
			// Calculate the EPROCESS pointer from the LIST_ENTRY
		   // We subtract the offset to get back to the start of EPROCESS
			currentProcess = (PEPROCESS)((ULONG_PTR)listEntry - g_ActiveProcessLinksOffset);

			//GET the PID
			HANDLE* pidPtr = (HANDLE*)((ULONG_PTR)currentProcess + g_UniqueProcessIdOffset);
			HANDLE pid = *pidPtr;

			//Get the image name
			PCHAR imageName = (PCHAR)((ULONG_PTR)currentProcess + g_ImageFileNameOffset);


			//store in our response array
			if (processCount < MAX_PROCESSES) {
				Response->Processes[processCount].Pid = (ULONG)(ULONG_PTR)pid;
				Response->Processes[processCount].Eprocess = currentProcess;

				//Copy image name(max 15 chars+null terminator)
				RtlCopyMemory(Response->Processes[processCount].ImageName, imageName, 15);
				Response->Processes[processCount].ImageName[15] = '\0';

				KdPrint(("ProcessWalker: [%lu] PID: %lu, Name: %s, EPROCESS: 0x%p\n",
					processCount,
					Response->Processes[processCount].Pid,
					Response->Processes[processCount].ImageName,
					Response->Processes[processCount].Eprocess));

				processCount++;

			}
			else {
				KdPrint(("ProcessWalker: Maximum process count reached (%d)\n", MAX_PROCESSES));
				break;
			}

			// Move to the next process in the list
			listEntry = listEntry->Flink;

		} while (listEntry != startListEntry && processCount < MAX_PROCESSES);

		Response->ProcessCount = processCount;
		status = STATUS_SUCCESS;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		KdPrint(("ProcessWalker: Exception while walking process list!\n"));
		status = STATUS_ACCESS_VIOLATION;
	}
	return status;
}

NTSTATUS DriverCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;

}


VOID DriverUnload(PDRIVER_OBJECT DriverObject) {
	UNICODE_STRING symlinkName;

	RtlInitUnicodeString(&symlinkName, SYMLINK_NAME);
	IoDeleteSymbolicLink(&symlinkName);

	
	IoDeleteDevice(DriverObject->DeviceObject);

	KdPrint(("Driver Unloaded Successfully"));

}