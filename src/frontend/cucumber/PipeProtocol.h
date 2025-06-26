#pragma once

#include "PipeMessage.h"
#include <unordered_map>

namespace cucumberDS
{
	// Forward declarations.
	class PipeConnection;

	class PipeProtocol
	{
	public:
		PipeProtocol() {}
		~PipeProtocol();

		// Delete copy and move operations to avoid double freeing the data.
		PipeProtocol(const PipeProtocol&) = delete;
		PipeProtocol& operator=(const PipeProtocol&) = delete;
		PipeProtocol(PipeProtocol&&) = delete;
		PipeProtocol& operator=(PipeProtocol&&) = delete;

		unsigned int CalculateInputBufferSize();
		unsigned int CalculateOutputBufferSize();

		PipeMessage* GetInputMessage(unsigned char id) { return inputMessages.at(id); }
		PipeMessage* GetOutputMessage(unsigned char id) { return outputMessages.at(id); }

		void ResetInputMessages();
		void ResetOutputMessages();

		bool IntialiseFromConnection(PipeConnection* pipeConnection);
	private:
		std::unordered_map<unsigned char, PipeMessage*> inputMessages = std::unordered_map<unsigned char, PipeMessage*>();
		std::unordered_map<unsigned char, PipeMessage*> outputMessages = std::unordered_map<unsigned char, PipeMessage*>();

		void initialiseInputMessages(PipeConnection* pipeConnection);
		void initialiseOutputMessages(PipeConnection* pipeConnection);

		void addInputMessage(PipeMessage* inputMessage) { inputMessages.insert_or_assign(inputMessage->GetId(), inputMessage); }
		void addOutputMessage(PipeMessage* outputMessage) { outputMessages.insert_or_assign(outputMessage->GetId(), outputMessage); }
	};
}
