#pragma once

#include "PipeMessage.h"

#include <vector>

namespace cucumberDS
{
	// Forward declarations.
	struct InputRequestData;

	class InputRequestMessage : public PipeMessage
	{
	public:
		static unsigned char Id;

		InputRequestMessage(PipeConnection* pipeConnection, unsigned char id, unsigned int sizePerMessage, unsigned char maximumPerFrame);

		void Reset() override;

		unsigned char GetDataCount() const { return currentData.size(); }
		InputRequestData* GetData(unsigned char index) const { return currentData.at(index); }
	protected:
		void read(BufferReader* reader) override;
	private:
		std::vector<InputRequestData*> currentData;
	};
}