#include "InputRequestMessage.h"

#include "PipeConnection.h"
#include "InputRequestData.h"

unsigned char cucumberDS::InputRequestMessage::Id = 255;

cucumberDS::InputRequestMessage::InputRequestMessage(PipeConnection* pipeConnection, unsigned char id, unsigned int sizePerMessage, unsigned char maximumPerFrame) : PipeMessage(pipeConnection, id, sizePerMessage, maximumPerFrame)
{
	Id = id;
}

void cucumberDS::InputRequestMessage::Reset()
{
    currentData.clear();
    PipeMessage::Reset();
}

void cucumberDS::InputRequestMessage::read(BufferReader* reader)
{
    InputRequestData* data = (InputRequestData*)reader->GetUnderlyingDataAtOffset();
    currentData.emplace_back(data);
    reader->IncrementOffset(CalculateContentSizePerMessage());
    currentPerFrame++;
}
