#pragma once

#include <unordered_map>

#include "PipeConnection.h"
#include "PipeMessage.h"

namespace cucumberDS
{
	class ServerConnectionManager
	{
	public:
		ServerConnectionManager(const wchar_t* pipeName);
		~ServerConnectionManager();

		bool Initialise();

		PipeMessage* GetInputMessage(unsigned char id) { return inputMessages.at(id); }
		PipeMessage* GetOutputMessage(unsigned char id) { return outputMessages.at(id); }

		void Send();
	private:
		PipeConnection* pipeConnection = nullptr;

		std::unordered_map<unsigned char, PipeMessage*> inputMessages = std::unordered_map<unsigned char, PipeMessage*>();
		std::unordered_map<unsigned char, PipeMessage*> outputMessages = std::unordered_map<unsigned char, PipeMessage*>();

		void initialiseInputMessages();
		void initialiseOutputMessages();

		void addInputMessage(PipeMessage* inputMessage) { inputMessages.insert_or_assign(inputMessage->GetId(), inputMessage); }
		void addOutputMessage(PipeMessage* outputMessage) { outputMessages.insert_or_assign(outputMessage->GetId(), outputMessage); }

		unsigned int calculateOutputBufferSize();
	};
}