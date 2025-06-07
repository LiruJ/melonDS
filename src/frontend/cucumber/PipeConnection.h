#pragma once

typedef void* HANDLE;

namespace cucumberDS
{
	class PipeConnection
	{
	public:
		PipeConnection(const wchar_t* name);
		~PipeConnection();

		bool TryConnect(unsigned long waitTime);

		void InitialiseOutputBuffer(unsigned int size);

		unsigned int Write(const unsigned char* buffer, unsigned int size);
		unsigned int Write(unsigned char value);

		unsigned int SendData();

		bool GetIsBroken() const { return isBroken; }
		wchar_t* GetName() const { return name; }
	private:
		bool isBroken = false;
		wchar_t* name = nullptr;

		HANDLE handle;

		unsigned int currentOutputOffset = 0;
		unsigned int maximumOutputBufferSize = 0;
		unsigned char* outputBuffer = nullptr;

		unsigned long tryConnect();
		bool tryInitialise();
	};
}