#include "ff.h"

DWORD __stdcall OnRead(IN HANDLE hDisk, IN PVOID Context, IN PVOID Buffer, IN ULONG Length,
	IN LARGE_INTEGER ByteOffset, IN BOOL PagingIo, OUT PULONG BytesRead);
DWORD __stdcall OnWrite(IN HANDLE hDisk, IN PVOID Context, IN PVOID Buffer, IN ULONG Length,
	IN LARGE_INTEGER ByteOffset, IN BOOL PagingIo, OUT PULONG BytesWritten);
VOID __stdcall OnEvent(IN HANDLE hDisk, IN LONG DiskEvent, IN PVOID Context);
BOOL __stdcall OnFormat(IN HANDLE hDisk, IN DWORD Progress, IN PVOID Context);
