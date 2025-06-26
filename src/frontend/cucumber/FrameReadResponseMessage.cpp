#include "FrameReadResponseMessage.h"

#include "PipeConnection.h"

unsigned char cucumberDS::FrameReadResponseMessage::Id = 255;

cucumberDS::FrameReadResponseMessage::FrameReadResponseMessage(PipeConnection* pipeConnection, unsigned char id, unsigned int sizePerMessage, unsigned char maximumPerFrame) : PipeMessage(pipeConnection, id, sizePerMessage, maximumPerFrame)
{
	Id = id;
}

bool cucumberDS::FrameReadResponseMessage::Write(const unsigned char* buffer, unsigned char screenId)
{
	BufferWriter* writer = pipeConnection->GetOutputWriter();
	if (!startWriting(writer))
		return false;
	writer->Write(screenId);
	writer->Write(buffer, GetDataSize());
	return stopWriting(writer);
}
