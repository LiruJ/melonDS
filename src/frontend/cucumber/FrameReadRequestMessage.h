#pragma once

#include "PipeMessage.h"

#include <vector>

namespace cucumberDS
{
	// Forward declarations.
	struct FrameReadRequestData;

	class FrameReadRequestMessage : public PipeMessage
	{
	public:
		static unsigned char Id;

		FrameReadRequestMessage(PipeConnection* pipeConnection, unsigned char id, unsigned int sizePerMessage, unsigned char maximumPerFrame);

		void Reset() override;

		unsigned char GetDataCount() const { return currentData.size(); }
		FrameReadRequestData* GetData(unsigned char index) const { return currentData.at(index); }
	protected:
		void read(BufferReader* reader) override;
	private:
		std::vector<FrameReadRequestData*> currentData;
	};
}
