#pragma once

#include "PipeProtocol.h"
#include "BufferReader.h"
#include "BufferWriter.h"

// Forward declarations.
typedef void* HANDLE;

namespace cucumberDS
{
	// Forward declarations.
	class MessageHeaderData;

	class PipeConnection
	{
	public:
		PipeConnection(const wchar_t* name);
		~PipeConnection();

		bool TryConnect(unsigned long waitTime);

		bool ReceiveHandshake();
		unsigned int ReceiveData(MessageHeaderData* headerData);
		unsigned int ReceiveDataOfLength(unsigned int size);
		unsigned int ReceiveDataOfLengthInto(unsigned char* buffer, unsigned int size);

		unsigned int SendData(unsigned int currentStep);

		PipeProtocol* GetProtocol() { return &protocol; }
		bool GetIsBroken() const { return isBroken; }
		wchar_t* GetName() const { return name; }

		BufferReader* GetInputReader() { return &inputBuffer; }
		BufferWriter* GetOutputWriter() { return &outputBuffer; }

	private:
		PipeProtocol protocol;

		bool isBroken = false;
		wchar_t* name = nullptr;

		HANDLE handle;

		BufferReader inputBuffer;
		BufferWriter outputBuffer;

		unsigned long tryConnect();
		bool tryInitialise();

		void readReceivedData();
	};
}