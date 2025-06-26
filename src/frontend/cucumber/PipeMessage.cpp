#include "PipeMessage.h"

#include "PipeConnection.h"
#include "BufferReader.h"
#include "BufferWriter.h"

#include "AcknowledgeMessage.h"

// Requests
#include "FrameReadRequestMessage.h"
#include "MemoryReadRequestMessage.h"
#include "InputRequestMessage.h"

// Responses
#include "FrameReadResponseMessage.h"

#include <string>
#include <cstring>

cucumberDS::PipeMessage::PipeMessage(PipeConnection* pipeConnection, unsigned char id, unsigned int sizePerMessage, unsigned char maximumPerFrame)
{
	this->pipeConnection = pipeConnection;
	this->id = id;
	this->sizePerMessage = sizePerMessage;
	this->maximumPerFrame = maximumPerFrame;
}

bool cucumberDS::PipeMessage::Write(unsigned char* buffer, unsigned int size)
{
	BufferWriter* writer = pipeConnection->GetOutputWriter();
	if (!startWriting(writer))
		return false;
	writer->Write(buffer, size);
	return stopWriting(writer);
}

bool cucumberDS::PipeMessage::WriteEmpty()
{
	BufferWriter* writer = pipeConnection->GetOutputWriter();
	if (!startWriting(writer))
		return false;
	return stopWriting(writer);
}

bool cucumberDS::PipeMessage::startWriting(BufferWriter* writer)
{
	if (maximumPerFrame <= currentPerFrame)
		return false;

	startReadWritePosition = writer->GetOffset();
	currentPerFrame++;

	return 0 != writer->Write(id);
}

bool cucumberDS::PipeMessage::stopWriting(BufferWriter* writer)
{
	unsigned int writtenSize = GetCurrentWriteSize();
	startReadWritePosition = 0;

	if (writtenSize != sizePerMessage)
	{
		printf("Message with id %d wrote %d bytes when it should have written %d", id, writtenSize, sizePerMessage);
		return false;
	}

	return true;
}

void cucumberDS::PipeMessage::startReading(BufferReader* reader)
{
	// TODO: Maybe check the currentPerFrame?
	// Message Id is read by the connection for packet routing, hence the position is walked back a bit here.
	startReadWritePosition = reader->GetOffset() - sizeof(unsigned char);
}

bool cucumberDS::PipeMessage::stopReading(BufferReader* reader)
{
	unsigned int readSize = GetCurrentReadSize();
	startReadWritePosition = 0;

	if (readSize != sizePerMessage)
	{
		printf("Message with id %d read %d bytes when it should have read %d", id, readSize, sizePerMessage);
		return false;
	}

	return true;
}

unsigned int cucumberDS::PipeMessage::GetCurrentReadSize() const { return pipeConnection->GetInputReader()->GetOffset() - startReadWritePosition; }

unsigned int cucumberDS::PipeMessage::GetCurrentWriteSize() const { return pipeConnection->GetOutputWriter()->GetOffset() - startReadWritePosition; }

cucumberDS::PipeMessage* cucumberDS::PipeMessage::CreateFromReadProtocol(PipeConnection* pipeConnection)
{
	BufferReader* reader = pipeConnection->GetInputReader();

	unsigned int strLen = 0;
	char* name = reader->ReadString(&strLen);

	unsigned char id = reader->ReadByte();
	unsigned int sizePerMessage = reader->ReadUInt();
	unsigned char maximumPerFrame = reader->ReadByte();
	printf("\tname: %s id: %d size:%d maximum:%d\n", name, id, sizePerMessage, maximumPerFrame);

	// This sorta sucks since it means this file needs to include all the other messages. Idk how much of an issue it is though.
	PipeMessage* message = nullptr;
	if (strcmp(name, "MemoryReadRequest") == 0)
		message = new MemoryReadRequestMessage(pipeConnection, id, sizePerMessage, maximumPerFrame);
	else if (strcmp(name, "AcknowledgeMessage") == 0)
		message = new AcknowledgeMessage(pipeConnection, id, sizePerMessage, maximumPerFrame);
	else if (strcmp(name, "FrameReadRequest") == 0)
		message = new FrameReadRequestMessage(pipeConnection, id, sizePerMessage, maximumPerFrame);
	else if (strcmp(name, "FrameReadResponse") == 0)
		message = new FrameReadResponseMessage(pipeConnection, id, sizePerMessage, maximumPerFrame);
	else if (strcmp(name, "InputRequest") == 0)
		message = new InputRequestMessage(pipeConnection, id, sizePerMessage, maximumPerFrame);

	if (message)
	{
		// TODO: Virtual load function.
	}
	return message;
}
