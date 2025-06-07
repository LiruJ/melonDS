#include "FrameOutputMessage.h"

#include "PipeConnection.h"

unsigned int cucumberDS::FrameOutputMessage::Write(const unsigned char* buffer, unsigned char screenId)
{
	if (maximumPerFrame <= currentPerFrame)
		return 0;

	if (!WriteId())
		return 0;

	unsigned int writtenSize = pipeConnection->Write(screenId);
	if (writtenSize == 0)
		return 1;
	writtenSize += pipeConnection->Write(buffer, GetDataSize());

	currentPerFrame++;
	return writtenSize;
}