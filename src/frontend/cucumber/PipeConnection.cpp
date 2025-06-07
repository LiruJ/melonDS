#include "PipeConnection.h"
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <stdio.h>
#include <cwchar>
#include <cstring>

cucumberDS::PipeConnection::PipeConnection(const wchar_t* name)
{
	if (name) {
		size_t len = std::wcslen(name);
		this->name = new wchar_t[len + 1];
		std::wcscpy(this->name, name);
	}
	else {
		this->name = nullptr;
	}

	handle = NULL;
	isBroken = false;
}

cucumberDS::PipeConnection::~PipeConnection()
{
	if (handle)
	{
		CloseHandle(handle);
		handle = nullptr;
	}
	if (name)
	{
		delete name;
		name = nullptr;
	}
	if (outputBuffer)
	{
		delete[] outputBuffer;
		outputBuffer = nullptr;
	}
}

bool cucumberDS::PipeConnection::TryConnect(unsigned long waitTime)
{
	if (isBroken)
		return false;
	if (NULL != handle)
		return true;

	// Try connect to the pipe. If it succeeds, return true 
	unsigned long lastError = tryConnect();
	if (ERROR_SUCCESS == lastError)
		return true;

	// Begin waiting. If at any point the pipe becomes available, try connect again.
	if (WaitNamedPipe(name, waitTime))
	{
		lastError = tryConnect();
		return ERROR_SUCCESS == lastError;
	}
	else
		return false;
}

void cucumberDS::PipeConnection::InitialiseOutputBuffer(unsigned int size)
{
	if (outputBuffer)
		return;

	maximumOutputBufferSize = size;
	outputBuffer = new unsigned char[size];
}

unsigned int cucumberDS::PipeConnection::Write(const unsigned char* buffer, unsigned int size)
{
	if (maximumOutputBufferSize < currentOutputOffset + size || 0 == size || nullptr == buffer || nullptr == outputBuffer)
		return 0;

	memcpy(outputBuffer + currentOutputOffset, buffer, size);
	currentOutputOffset += size;
	return size;
}

unsigned int cucumberDS::PipeConnection::Write(unsigned char value)
{
	if (maximumOutputBufferSize < currentOutputOffset + 1 || nullptr == outputBuffer)
		return 0;

	*(outputBuffer + currentOutputOffset) = value;
	currentOutputOffset++;
	return 1;
}

unsigned int cucumberDS::PipeConnection::SendData()
{
	unsigned int sendSize = currentOutputOffset;
	unsigned long totalWrittenBytes = 0;

	while (sendSize > totalWrittenBytes)
	{
		unsigned long writtenBytesThisSend = 0;
		bool success = WriteFile(handle, outputBuffer + totalWrittenBytes, sendSize - totalWrittenBytes, &writtenBytesThisSend, NULL);

		if (!success || 0 == writtenBytesThisSend)
			return totalWrittenBytes;
		totalWrittenBytes += writtenBytesThisSend;
	}

	currentOutputOffset = 0;
	return sendSize;
}

unsigned long cucumberDS::PipeConnection::tryConnect()
{
	handle = CreateFile(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

	// 
	if (INVALID_HANDLE_VALUE != handle)
	{
		bool initialisationSuccess = tryInitialise();
		if (initialisationSuccess)
			return ERROR_SUCCESS;
		else
			return GetLastError();
	}

	// Get the last error. If it's anything but the pipe being busy, then the pipe is broken.
	unsigned long lastError = GetLastError();
	if (ERROR_PIPE_BUSY != lastError)
	{
		isBroken = true;
		printf("Could not open pipe: %d\n", lastError);
	}

	return lastError;
}

bool cucumberDS::PipeConnection::tryInitialise()
{
	unsigned long mode = PIPE_READMODE_MESSAGE;
	bool success = SetNamedPipeHandleState(
		handle,    // pipe handle 
		&mode,  // new pipe mode 
		NULL,     // don't set maximum bytes 
		NULL);    // don't set maximum time 

	if (!success)
	{
		isBroken = true;
		printf("Could not initialise pipe: %d\n", GetLastError());
	}

	return success;
}
