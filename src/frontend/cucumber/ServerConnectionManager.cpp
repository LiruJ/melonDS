#include "ServerConnectionManager.h"

#include "PipeMessage.h"
#include "FrameOutputMessage.h"

cucumberDS::ServerConnectionManager::ServerConnectionManager(const wchar_t* pipeName)
{
    pipeConnection = new cucumberDS::PipeConnection(pipeName);
}

cucumberDS::ServerConnectionManager::~ServerConnectionManager()
{
    if (pipeConnection)
    {
        delete pipeConnection;
        pipeConnection = nullptr;
    }

    for (auto const& [key, value] : inputMessages)
        delete value;
    for (auto const& [key, value] : outputMessages)
        delete value;
    inputMessages.clear();
    outputMessages.clear();
}

bool cucumberDS::ServerConnectionManager::Initialise()
{
    while (true)
    {
        bool connectionSuccess = pipeConnection->TryConnect(5000);

        if (connectionSuccess)
            break;

        if (pipeConnection->GetIsBroken())
            return false;
    }

    initialiseInputMessages();
    initialiseOutputMessages();

    unsigned int outputBufferSize = calculateOutputBufferSize();
    pipeConnection->InitialiseOutputBuffer(outputBufferSize);

    return true;
}

void cucumberDS::ServerConnectionManager::Send()
{
    unsigned int sentDataSize = pipeConnection->SendData();

    for (auto const& [key, value] : inputMessages)
        value->Reset();
    for (auto const& [key, value] : outputMessages)
        value->Reset();
}

void cucumberDS::ServerConnectionManager::initialiseInputMessages()
{
}

void cucumberDS::ServerConnectionManager::initialiseOutputMessages()
{
    FrameOutputMessage* frameOutputMessage = new FrameOutputMessage(pipeConnection);
    addOutputMessage(frameOutputMessage);
}

unsigned int cucumberDS::ServerConnectionManager::calculateOutputBufferSize()
{
    unsigned int currentSize = 0;
    for (auto const& [key, value] : outputMessages)
        currentSize += value->CalculateMaximumLength();
    return currentSize;
}
