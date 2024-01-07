#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>



#define TEST_FILE  TEXT("Q:\\test.dat")
#define FILE_SIZE  0x100000ULL * 32000

#define IO_BUFFER_SZ  0x100000 * 2
#define IO_QUEUE_DEPTH  256


#define LOG(fmt, ...) printf("\r\n" fmt, __VA_ARGS__)




void Entry()
{
	HANDLE hFile;
	HANDLE hIOCP;
	DWORD Result;
	LARGE_INTEGER FileOffset;


	// Create dummy file and front-load to touch.
	if((hFile = CreateFile(TEST_FILE, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, NULL, NULL)) == INVALID_HANDLE_VALUE)
	{
		LOG("Failed to open dst file");
		return;
	}

	LARGE_INTEGER FileSz;
	FileSz.QuadPart = FILE_SIZE - 1;

	if(!SetFilePointerEx(hFile, FileSz, &FileSz, FILE_BEGIN) || !WriteFile(hFile, "\x00", 1, &Result, NULL) || Result != 1)
	{
		LOG("Failed to allocate file");
		return;
	}

	CloseHandle(hFile);

	LOG("File allocated; testing.. ");

	// 
	if((hFile = CreateFile(TEST_FILE, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING, NULL)) == INVALID_HANDLE_VALUE)
	{
		LOG("Failed to open dst file");
		return;
	}

	static __declspec(align(0x1000)) OVERLAPPED IOB[IO_QUEUE_DEPTH];
	BYTE* IoBuffers;

	if(!(hIOCP = CreateIoCompletionPort(hFile, NULL, 0, 0)) || !(IoBuffers = (BYTE*) VirtualAlloc(NULL, IO_QUEUE_DEPTH * IO_BUFFER_SZ, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE)))
	{
		LOG("Init failed");
		return;
	}

	for(int i = 0; i < (IO_QUEUE_DEPTH*IO_BUFFER_SZ) / 0x1000; i++)
		IoBuffers[i*0x1000] = 0;

	FileOffset.QuadPart = 0;

	for(int i = 0; i < IO_QUEUE_DEPTH; i++)
	{
		IOB[i].Offset = FileOffset.LowPart;
		IOB[i].OffsetHigh = FileOffset.HighPart;

		if(!WriteFile(hFile, &IoBuffers[i*IO_BUFFER_SZ], IO_BUFFER_SZ, &Result, &IOB[i]) && GetLastError() != ERROR_IO_PENDING)
		{
			LOG("Failed to submit I/O");
			return;
		}

		FileOffset.QuadPart += IO_BUFFER_SZ;
	}

	
	for(;;)
	{
		OVERLAPPED_ENTRY Entry[16];
		ULONG Count;

		if(!GetQueuedCompletionStatusEx(hIOCP, Entry, ARRAYSIZE(Entry), &Count, INFINITE, FALSE))
			return;

		for(ULONG i = 0; i < Count; i++)
		{
			if(Entry[i].Internal != 0)
			{
				LOG("I/O failure: 0x%X", Entry[i].Internal);
				return;
			}

			FileOffset.QuadPart += IO_BUFFER_SZ;

			Entry[i].lpOverlapped->Offset = FileOffset.LowPart;
			Entry[i].lpOverlapped->OffsetHigh = FileOffset.HighPart;

			if(!WriteFile(hFile, &IoBuffers[(Entry[i].lpOverlapped - IOB) * IO_BUFFER_SZ], IO_BUFFER_SZ, &Result, Entry[i].lpOverlapped) && GetLastError() != ERROR_IO_PENDING)
			{
				LOG("Failed to submit I/O");
			}
		}
	}



}


void main()
{
	Entry();
	Sleep(INFINITE);
}