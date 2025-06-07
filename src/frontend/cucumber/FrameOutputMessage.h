#pragma once

#include "PipeMessage.h"

namespace cucumberDS
{
	class FrameOutputMessage : public PipeMessage
	{
	public:
		FrameOutputMessage(PipeConnection* pipeConnection) : PipeMessage(pipeConnection, FRAME_OUTPUT_MESSAGE_ID, 2) {};

		unsigned int Write(const unsigned char* buffer, unsigned char screenId);
		unsigned int Write(unsigned int* buffer, unsigned char screenId) { return Write(reinterpret_cast<unsigned char*>(buffer), screenId); }

		static unsigned int GetDataSize() { return (256 * 192) * sizeof(unsigned int); }
	protected:
		virtual unsigned int calculateMaximumContentLength() const override { return sizeof(unsigned char) + GetDataSize(); }
	};
}