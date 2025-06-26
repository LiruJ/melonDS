#pragma once

namespace cucumberDS
{
	// Forward declarations.
	class PipeConnection;
	class BufferReader;
	class BufferWriter;

	class PipeMessage
	{
	public:
		PipeMessage(PipeConnection* pipeConnection, unsigned char id, unsigned int sizePerMessage, unsigned char maximumPerFrame);
		virtual ~PipeMessage() {}

		unsigned char GetId() const { return id; }
		bool GetHasData() const { return currentPerFrame > 0; }
		unsigned int GetSizePerMessage() const { return sizePerMessage; }

		unsigned int CalculateContentSizePerMessage() const { return sizePerMessage - sizeof(unsigned char); }
		unsigned int CalculateSizePerFrame() const { return sizePerMessage * maximumPerFrame; };

		bool Write(unsigned char* buffer, unsigned int size);

		bool WriteEmpty();

		virtual void Reset() { currentPerFrame = 0; }

		bool Read(BufferReader* reader)
		{
			startReading(reader);
			read(reader);
			return stopReading(reader);
		}

		unsigned int GetCurrentReadSize() const;
		unsigned int GetCurrentWriteSize() const;

		static PipeMessage* CreateFromReadProtocol(PipeConnection* pipeConnection);
	protected:
		unsigned char id = 255;
		unsigned int sizePerMessage = 0;
		unsigned char maximumPerFrame = 0;

		PipeConnection* pipeConnection = nullptr;

		unsigned char currentPerFrame = 0;

		bool startWriting(BufferWriter* writer);
		bool stopWriting(BufferWriter* writer);

		virtual void read(BufferReader* reader) {}

		void startReading(BufferReader* reader);
		bool stopReading(BufferReader* reader);
	private:
		unsigned int startReadWritePosition = 0;
	};
}