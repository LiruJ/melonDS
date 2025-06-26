#include "BufferReader.h"

#include <cstring>

unsigned char cucumberDS::BufferReader::ReadByte()
{
	if (!HasRemainingInput(sizeof(unsigned char)) || nullptr == data)
		return 0;

	unsigned char value = *(data + offset);
	IncrementOffset(sizeof(unsigned char));
	return value;
}

unsigned int cucumberDS::BufferReader::ReadUInt()
{
	if (!HasRemainingInput(sizeof(unsigned int)) || nullptr == data)
		return 0;

	unsigned int value = *(unsigned int*)(data + offset);
	IncrementOffset(sizeof(unsigned int));
	return value;
}

char* cucumberDS::BufferReader::ReadString(unsigned int* size)
{
	if (!HasRemainingInput() || nullptr == data)
		return nullptr;

	const unsigned char* end = (unsigned char*)memchr(data + offset, '\0', dataSize - offset);
	if (nullptr == end)
		return nullptr;
	unsigned int stringLength = (unsigned int)(end - (data + offset));

	if (nullptr != size)
		*size = stringLength;
	char* outputString = (char*)(data + offset);

	IncrementOffset(stringLength + 1);
	return outputString;
}

char* cucumberDS::BufferReader::ReadStringCopy()
{
	unsigned int stringLength = 0;
	char* bufferString = ReadString(&stringLength);

	if (nullptr == bufferString)
		return nullptr;

	char* stringCopy = new char[stringLength + 1];
	memcpy(stringCopy, bufferString, stringLength);
	stringCopy[stringLength] = '\0';
	return stringCopy;
}