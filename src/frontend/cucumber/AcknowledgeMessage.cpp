#include "AcknowledgeMessage.h"

#include "PipeConnection.h"

unsigned char cucumberDS::AcknowledgeMessage::Id = 255;

cucumberDS::AcknowledgeMessage::AcknowledgeMessage(PipeConnection* pipeConnection, unsigned char id, unsigned int sizePerMessage, unsigned char maximumPerFrame) : PipeMessage(pipeConnection, id, sizePerMessage, maximumPerFrame)
{
	Id = id;
}