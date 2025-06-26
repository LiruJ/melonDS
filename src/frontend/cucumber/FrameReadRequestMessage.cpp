#include "FrameReadRequestMessage.h"

#include "PipeConnection.h"
#include "FrameReadRequestData.h"

unsigned char cucumberDS::FrameReadRequestMessage::Id = 255;

cucumberDS::FrameReadRequestMessage::FrameReadRequestMessage(PipeConnection* pipeConnection, unsigned char id, unsigned int sizePerMessage, unsigned char maximumPerFrame) : PipeMessage(pipeConnection, id, sizePerMessage, maximumPerFrame)
{
	Id = id;
}

void cucumberDS::FrameReadRequestMessage::Reset()
{
    currentData.clear();
    PipeMessage::Reset();
}

void cucumberDS::FrameReadRequestMessage::read(BufferReader* reader)
{
    FrameReadRequestData* data = (FrameReadRequestData*)reader->GetUnderlyingDataAtOffset();
    currentData.emplace_back(data);
    reader->IncrementOffset(CalculateContentSizePerMessage());
    currentPerFrame++;
}