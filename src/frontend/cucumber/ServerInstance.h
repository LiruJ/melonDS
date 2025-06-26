#pragma once

#include "PipeConnection.h"

namespace cucumberDS
{
	class MessageHeaderData;
	class ServerInstance
	{
	public:
		ServerInstance(const wchar_t* pipeName);
		~ServerInstance();

		bool Initialise();

		PipeConnection* GetPipeConnection() { return pipeConnection; }

		void Send(unsigned int currentStep);
		unsigned int Receive(MessageHeaderData* headerData);
	private:
		PipeConnection* pipeConnection = nullptr;
	};
}