#pragma once

#include "PipeMessage.h"

namespace cucumberDS
{
	class FrameReadResponseMessage : public PipeMessage
	{
	public:
		static unsigned char Id;

		FrameReadResponseMessage(PipeConnection* pipeConnection, unsigned char id, unsigned int sizePerMessage, unsigned char maximumPerFrame);;

		bool Write(const unsigned char* buffer, unsigned char screenId);

		static unsigned int GetDataSize() { return (256 * 192) * sizeof(unsigned int); }
	};
}