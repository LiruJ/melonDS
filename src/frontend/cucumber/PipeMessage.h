#pragma once

constexpr auto FRAME_OUTPUT_MESSAGE_ID = 1;

namespace cucumberDS
{
	class PipeConnection;

	class PipeMessage
	{
	public:
		PipeMessage(PipeConnection* pipeConnection, unsigned char id, unsigned char maximumPerFrame);

		unsigned char GetId() const { return id; }
		bool GetHasData() const { return currentPerFrame > 0; }

		unsigned int CalculateMaximumLength() const 
		{
			// Each message has an id, plus the content, and there should be space for the maximum number of messages per frame.
			return (sizeof(id) + calculateMaximumContentLengthPerMessage()) * maximumPerFrame; 
		};

		unsigned int Write(unsigned char* buffer, unsigned int size);

		bool WriteId();

		virtual void Reset() { currentPerFrame = 0; }
	protected:
		PipeConnection* pipeConnection = nullptr;

		virtual unsigned int calculateMaximumContentLengthPerMessage() const = 0;

		unsigned char currentPerFrame = 0;
		unsigned char maximumPerFrame = 0;

	private:
		unsigned char id;
	};
}