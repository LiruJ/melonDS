#pragma once

#include "PipeMessage.h"
#include <vector>

namespace cucumberDS
{
	// Forward declarations.
	class PipeConnection;
	class MemoryReadRequestData;

	class MemoryReadRequestMessage : public PipeMessage
	{
	public:
		static unsigned char Id;

		MemoryReadRequestMessage(PipeConnection* pipeConnection, unsigned char id, unsigned int sizePerMessage, unsigned char maximumPerFrame);

		void Reset() override;

		unsigned char GetDataCount() const { return currentData.size(); }
		MemoryReadRequestData* GetData(unsigned char index) const { return currentData.at(index); }
	protected:
		void read(BufferReader* reader) override;
	private:
		std::vector<MemoryReadRequestData*> currentData;
	};
}
