#pragma once

#include "PipeMessage.h"

namespace cucumberDS
{
	// Forward declarations.
	class PipeConnection;

	class AcknowledgeMessage : public PipeMessage
	{
	public:
		static unsigned char Id;

		AcknowledgeMessage(PipeConnection* pipeConnection, unsigned char id, unsigned int sizePerMessage, unsigned char maximumPerFrame);
	};
}
