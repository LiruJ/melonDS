#include "BufferWriter.h"

#include <cstring>

unsigned int cucumberDS::BufferWriter::Write(const unsigned char* buffer, unsigned int size)
{
	if (!HasRemainingData(size) || 0 == size || nullptr == buffer || nullptr == data)
		return 0;

	memcpy(data + offset, buffer, size);
	IncrementOffset(size);
	return size;
}

unsigned int cucumberDS::BufferWriter::Write(unsigned char value)
{
	if (!HasRemainingData(sizeof(unsigned char)) || nullptr == data)
		return 0;

	*(data + offset	) = value;
	IncrementOffset(sizeof(unsigned char));
	return sizeof(unsigned char);
}

unsigned int cucumberDS::BufferWriter::Write(unsigned int value)
{
	if (!HasRemainingData(sizeof(unsigned int)) || nullptr == data)
		return 0;

	*(unsigned int*)(data + offset) = value;
	IncrementOffset(sizeof(unsigned int));
	return sizeof(unsigned int);
}