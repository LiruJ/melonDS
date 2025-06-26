#include "ServerInstance.h"

#include "MessageHeaderData.h"
#include "PipeMessage.h"
#include "AcknowledgeMessage.h"

#include <algorithm>
#include <cstdio>

cucumberDS::ServerInstance::ServerInstance(const wchar_t* pipeName)
{
	pipeConnection = new cucumberDS::PipeConnection(pipeName);
}

cucumberDS::ServerInstance::~ServerInstance()
{
	if (pipeConnection)
	{
		delete pipeConnection;
		pipeConnection = nullptr;
	}
}

bool cucumberDS::ServerInstance::Initialise()
{
	// Try connect to the server. This is blocking, and will take some time.
	while (true)
	{
		bool connectionSuccess = pipeConnection->TryConnect(5000);
		if (connectionSuccess)
			break;
		if (pipeConnection->GetIsBroken())
			return false;
	}

	if (!pipeConnection->ReceiveHandshake())
		return false;

	// Write an acknowledge message and send it to the server to let it know that the client is ready.
	cucumberDS::AcknowledgeMessage* acknowledgeMessage = (cucumberDS::AcknowledgeMessage*)pipeConnection->GetProtocol()->GetOutputMessage(cucumberDS::AcknowledgeMessage::Id);
	if (nullptr == acknowledgeMessage)
	{
		printf("No acknowledge message with id %d exists\n", cucumberDS::AcknowledgeMessage::Id);
		return false;
	}
	if (!acknowledgeMessage->WriteEmpty())
		return false;
	pipeConnection->SendData(0);

	return true;
}

void cucumberDS::ServerInstance::Send(unsigned int currentStep)
{
	pipeConnection->SendData(currentStep);
}

unsigned int cucumberDS::ServerInstance::Receive(MessageHeaderData* headerData)
{
	return pipeConnection->ReceiveData(headerData);
}


