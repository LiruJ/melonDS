#pragma once

#include "ByteBuffer.h"

namespace cucumberDS
{
	class BufferReader : public ByteBuffer
	{
	public:
		BufferReader() : ByteBuffer() { }
		BufferReader(unsigned int size) : ByteBuffer(size) { }
		~BufferReader() override { }

		unsigned int GetCurrentInputSize() const { return currentInputSize; }
		void SetCurrentInputSize(unsigned int currentInputSize) { this->currentInputSize = currentInputSize; }
		bool HasRemainingInput() const { return offset < currentInputSize; }
		bool HasRemainingInput(unsigned int size) const { return offset + size <= currentInputSize; }

		unsigned char ReadByte();
		unsigned int ReadUInt();
		char* ReadString(unsigned int* size);
		char* ReadStringCopy();

	private:
		unsigned int currentInputSize = 0;
	};
}