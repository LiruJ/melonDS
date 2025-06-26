#pragma once

namespace cucumberDS
{
	// Forward declarations.
	class BufferReader;
	class BufferWriter;

	struct MessageHeaderData
	{
	public:
		MessageHeaderData(unsigned int packetSize, unsigned int currentStep);
		MessageHeaderData() { packetSize = 0; currentStep = 0; }

		unsigned int GetPacketSize() const { return packetSize; }
		unsigned int GetCurrentStep() const { return currentStep; }

		void Read(BufferReader* reader);
		void Write(BufferWriter* writer) const;

		static unsigned int GetSize() { return sizeof(unsigned int) + sizeof(unsigned int); }
	private:
		unsigned int packetSize;
		unsigned int currentStep;
	};
}
