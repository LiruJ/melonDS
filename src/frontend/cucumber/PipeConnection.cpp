#include "PipeConnection.h"

#include "PipeMessage.h"
#include "MessageHeaderData.h"

#include <windows.h>
#include <stdio.h>
#include <cwchar>
#include <cstring>
#include <string.h>
#include <algorithm>

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
	//if (protocol)
	//{
	//	delete protocol;
	//	protocol = nullptr;
	//}
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
	if (WaitNamedPipeW(name, waitTime))
	{
		lastError = tryConnect();
		return ERROR_SUCCESS == lastError;
	}
	
	return false;
}

bool cucumberDS::PipeConnection::ReceiveHandshake()
{
	// TODO: Stack alloc
	unsigned char* handshakeBuffer = new unsigned char[8];

	if (0 == ReceiveDataOfLengthInto(handshakeBuffer, 8))
	{
		delete[] handshakeBuffer;
		return false;
	}

	// Handshake begins with 2 unsigned integer values. One for the size of the protocol, and one for the size of the input buffer.
	unsigned int protocolSize = *((unsigned int*)handshakeBuffer);
	unsigned int inputBufferSize = *((unsigned int*)(handshakeBuffer + 4));

	printf("Received handshake header, protocol size: %d, input buffer size: %d\n", protocolSize, inputBufferSize);

	// The temporary buffer is no longer needed, so delete it.
	delete[] handshakeBuffer;
	handshakeBuffer = nullptr;

	// Initialise the input buffer. It will be used to receive the handshake.
	// Note that the input buffer does not store the header, unlike the output buffer.
	inputBufferSize = std::max(protocolSize, inputBufferSize);
	inputBuffer.Resize(inputBufferSize);

	// Receive the handshake data.
	if (protocolSize != ReceiveDataOfLength(protocolSize) || isBroken)
		return false;
	inputBuffer.SetCurrentInputSize(protocolSize);

	// Create the protocol based on the received data.
	if (!protocol.IntialiseFromConnection(this))
		return false;

	// Initialise the output buffer based on the protocol size.
	// Note that the output buffer stores the header, and so needs extra space.
	unsigned int outputBufferSize = protocol.CalculateOutputBufferSize() + MessageHeaderData::GetSize();
	outputBuffer.Resize(outputBufferSize);
	outputBuffer.SetOffset(MessageHeaderData::GetSize());

	return true;
}

unsigned int cucumberDS::PipeConnection::ReceiveData(MessageHeaderData* headerData)
{
	if (isBroken)
		return 0;

	// Get the packet size.
	if (0 == ReceiveDataOfLengthInto(inputBuffer.GetUnderlyingData(), MessageHeaderData::GetSize()) || isBroken)
		return 0;

	inputBuffer.ResetOffset();
	headerData->Read(&inputBuffer);

	inputBuffer.SetCurrentInputSize(headerData->GetPacketSize());
	if (headerData->GetPacketSize() > inputBuffer.GetDataSize())
	{
		printf("Pipe broke while reading message\n");
		isBroken = true;
		return 0;
	}

	// Receive the packet data.
	inputBuffer.ResetOffset();
	unsigned int receivedSize = ReceiveDataOfLengthInto(inputBuffer.GetUnderlyingData(), headerData->GetPacketSize());

	// Read the received data, so that the messages are parsed.
	readReceivedData();
	inputBuffer.ResetOffset();

	return receivedSize;
}

unsigned int cucumberDS::PipeConnection::ReceiveDataOfLength(unsigned int size)
{
	return ReceiveDataOfLengthInto(inputBuffer.GetUnderlyingData(), size);
}

unsigned int cucumberDS::PipeConnection::ReceiveDataOfLengthInto(unsigned char* buffer, unsigned int size)
{
	if (isBroken)
		return 0;

	unsigned long totalReadBytes = 0;

	while (size > totalReadBytes)
	{
		unsigned long readBytesThisReceive = 0;
		bool success = ReadFile(handle, buffer + totalReadBytes, size, &readBytesThisReceive, NULL);

		if ((!success && GetLastError() != ERROR_MORE_DATA) || 0 == readBytesThisReceive)
		{
			isBroken = true;
			printf("Could not receive data from pipe: %d\n", GetLastError());
			return totalReadBytes;
		}

		totalReadBytes += readBytesThisReceive;
	}

	return totalReadBytes;
}
 
unsigned int cucumberDS::PipeConnection::SendData(unsigned int currentStep)
{
	if (isBroken)
		return 0;

	unsigned int sendSize = outputBuffer.GetOffset();
	unsigned int packetSize = sendSize - MessageHeaderData::GetSize();

	// Write the packet size to the start of the buffer.
	outputBuffer.ResetOffset();
	MessageHeaderData headerData(packetSize, currentStep);
	headerData.Write(&outputBuffer);

	// Keep writing the output buffer through the pipe until it is done.
	unsigned long totalWrittenBytes = 0;
	while (sendSize > totalWrittenBytes)
	{
		unsigned long writtenBytesThisSend = 0;
		bool success = WriteFile(handle, outputBuffer.GetUnderlyingDataAt(totalWrittenBytes), sendSize - totalWrittenBytes, &writtenBytesThisSend, NULL);

		if (!success || 0 == writtenBytesThisSend)
			return totalWrittenBytes;
		totalWrittenBytes += writtenBytesThisSend;
	}

	// Reset the output state.
	protocol.ResetOutputMessages();
	// Note that the output buffer offset will automatically be at the correct position, as it was reset and the header was written.

	return sendSize;
}

unsigned long cucumberDS::PipeConnection::tryConnect()
{
	handle = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

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
	bool success = SetNamedPipeHandleState(handle, &mode, NULL, NULL);

	if (!success)
	{
		isBroken = true;
		printf("Could not initialise pipe: %d\n", GetLastError());
	}

	return success;
}

void cucumberDS::PipeConnection::readReceivedData()
{
	if (isBroken)
		return;

	protocol.ResetInputMessages();

	while (inputBuffer.HasRemainingInput())
	{
		unsigned char messageId = inputBuffer.ReadByte();
		PipeMessage* message = protocol.GetInputMessage(messageId);
		if (nullptr == message)
		{
			printf("\tNo message has id %d\n", messageId);
			isBroken = true;
			return;
		}
		if (!message->Read(&inputBuffer))
		{
			printf("Message with id %d read %d bytes when it should have read %d\n", messageId, message->GetCurrentReadSize(), message->GetSizePerMessage());
			return;
		}
	}
}
