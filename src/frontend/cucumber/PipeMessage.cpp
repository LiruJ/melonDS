#include "PipeMessage.h"

#include "PipeConnection.h"

cucumberDS::PipeMessage::PipeMessage(PipeConnection* pipeConnection, unsigned char id, unsigned char maximumPerFrame)
{
	this->pipeConnection = pipeConnection;
	this->id = id;
	this->maximumPerFrame = maximumPerFrame;
}

unsigned int cucumberDS::PipeMessage::Write(unsigned char* buffer, unsigned int size)
{
	return pipeConnection->Write(buffer, size);
}

bool cucumberDS::PipeMessage::WriteId()
{
	return pipeConnection->Write(id);
}