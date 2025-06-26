#pragma once

#include "ByteBuffer.h"

namespace cucumberDS
{
	class BufferWriter : public ByteBuffer
	{
	public:
		BufferWriter() : ByteBuffer() { }
		BufferWriter(unsigned int size) : ByteBuffer(size) { }
		~BufferWriter() override { }

		unsigned int Write(const unsigned char* buffer, unsigned int size);
		unsigned int Write(unsigned char value);
		unsigned int Write(unsigned int value);
	};
}
