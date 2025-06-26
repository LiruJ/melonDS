#include "MessageHeaderData.h"

#include "BufferReader.h"
#include "BufferWriter.h"

#include <cstdio>

cucumberDS::MessageHeaderData::MessageHeaderData(unsigned int packetSize, unsigned int currentStep)
{
	this->packetSize = packetSize;
	this->currentStep = currentStep;
}

void cucumberDS::MessageHeaderData::Read(BufferReader* reader)
{
	if (reader->GetOffset() != 0)
		printf("Input offset was at %d instead of 0 for header!\n", reader->GetOffset());

	packetSize = reader->ReadUInt();
	currentStep = reader->ReadUInt();
}

void cucumberDS::MessageHeaderData::Write(BufferWriter* writer) const
{
	if (writer->GetOffset() != 0)
		printf("Output offset was at %d instead of 0 for header!\n", writer->GetOffset());

	writer->Write(packetSize);
	writer->Write(currentStep);
}
