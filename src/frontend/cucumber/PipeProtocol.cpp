#include "PipeProtocol.h"

#include "PipeMessage.h"
#include "PipeConnection.h"

#include <cstdio>

cucumberDS::PipeProtocol::~PipeProtocol()
{
	for (auto const& [key, value] : inputMessages)
		delete value;
	for (auto const& [key, value] : outputMessages)
		delete value;
	inputMessages.clear();
	outputMessages.clear();
}

unsigned int cucumberDS::PipeProtocol::CalculateInputBufferSize()
{
	unsigned int calculatedInputBufferSize = 0;
	for (auto const& [key, value] : inputMessages)
		calculatedInputBufferSize += value->CalculateSizePerFrame();
	return calculatedInputBufferSize;
}

unsigned int cucumberDS::PipeProtocol::CalculateOutputBufferSize()
{
	unsigned int calculatedOutputBufferSize = 0;
	for (auto const& [key, value] : outputMessages)
		calculatedOutputBufferSize += value->CalculateSizePerFrame();
	return calculatedOutputBufferSize;
}

void cucumberDS::PipeProtocol::ResetInputMessages()
{
	for (auto const& [key, value] : inputMessages)
		value->Reset();
}

void cucumberDS::PipeProtocol::ResetOutputMessages()
{
	for (auto const& [key, value] : outputMessages)
		value->Reset();
}

bool cucumberDS::PipeProtocol::IntialiseFromConnection(PipeConnection* pipeConnection)
{
	// Initialise the output buffer based on the received size.
	unsigned int outputBufferSize = pipeConnection->GetInputReader()->ReadUInt();

	initialiseInputMessages(pipeConnection);
	initialiseOutputMessages(pipeConnection);

	// Check the received output buffer size against the calculated one from the received protocol.
	if (CalculateOutputBufferSize() != outputBufferSize)
	{
		printf("Mismatching output buffer size, received from server: %d Calculated: %d\n", outputBufferSize, CalculateOutputBufferSize());
		return false;
	}

	return true;
}

void cucumberDS::PipeProtocol::initialiseInputMessages(PipeConnection* pipeConnection)
{
	unsigned char messageProtocolCount = pipeConnection->GetInputReader()->ReadByte();
	printf("input message protocols: %d\n", messageProtocolCount);
	for (size_t i = 0; i < messageProtocolCount; i++)
	{
		PipeMessage* message = PipeMessage::CreateFromReadProtocol(pipeConnection);
		if (nullptr == message)
		{
			printf("Message failed to read\n");
			continue;
		}
		addInputMessage(message);
	}
}

void cucumberDS::PipeProtocol::initialiseOutputMessages(PipeConnection* pipeConnection)
{
	unsigned char messageProtocolCount = pipeConnection->GetInputReader()->ReadByte();
	printf("output message protocols: %d\n", messageProtocolCount);
	for (size_t i = 0; i < messageProtocolCount; i++)
	{
		PipeMessage* message = PipeMessage::CreateFromReadProtocol(pipeConnection);
		if (nullptr == message)
		{
			printf("Message failed to read\n");
			continue;
		}
		addOutputMessage(message);
	}
}