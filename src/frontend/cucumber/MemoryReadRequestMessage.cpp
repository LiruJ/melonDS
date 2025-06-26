#include "MemoryReadRequestMessage.h"

#include "PipeConnection.h"
#include "MemoryReadRequestData.h"

#include <cstdio>

unsigned char cucumberDS::MemoryReadRequestMessage::Id = 255;

cucumberDS::MemoryReadRequestMessage::MemoryReadRequestMessage(PipeConnection* pipeConnection, unsigned char id, unsigned int sizePerMessage, unsigned char maximumPerFrame) : PipeMessage(pipeConnection, id, sizePerMessage, maximumPerFrame)
{
	Id = id;
	currentData = std::vector<MemoryReadRequestData*>();
}

void cucumberDS::MemoryReadRequestMessage::Reset()
{
    currentData.clear();
    PipeMessage::Reset();
}

void cucumberDS::MemoryReadRequestMessage::read(BufferReader* reader)
{
    MemoryReadRequestData* data = (MemoryReadRequestData*)reader->GetUnderlyingDataAtOffset();
    currentData.emplace_back(data);
    reader->IncrementOffset(CalculateContentSizePerMessage());
    currentPerFrame++;
}
