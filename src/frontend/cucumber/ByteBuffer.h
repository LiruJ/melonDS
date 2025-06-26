#pragma once

#include <algorithm>

namespace cucumberDS
{
	class ByteBuffer
	{
	public:
		ByteBuffer() 
		{
			data = nullptr; 
			dataSize = 0;
			offset = 0;
		}
		ByteBuffer(unsigned int size)
		{
			data = new unsigned char[size];
			dataSize = size;
			offset = 0;
		}
		virtual ~ByteBuffer()
		{
			if (data)
			{
				delete[] data;
				data = nullptr;
			}
			dataSize = 0;
			offset = 0;
		}

		// Delete copy and move operations to avoid double freeing the data.
		ByteBuffer(const ByteBuffer&) = delete;
		ByteBuffer& operator=(const ByteBuffer&) = delete;
		ByteBuffer(ByteBuffer&&) = delete;
		ByteBuffer& operator=(ByteBuffer&&) = delete;

		void Resize(unsigned int newSize)
		{
			if (newSize == dataSize)
				return;

			unsigned int oldDataSize = dataSize;
			dataSize = newSize;
			if (oldDataSize > newSize)
				return;

			if (data)
				delete[] data;
			data = new unsigned char[newSize];

			offset = std::min(offset, newSize);
		}

		bool HasRemainingData() const { return offset < dataSize; }
		bool HasRemainingData(unsigned int size) const { return offset + size <= dataSize; }

		unsigned int GetDataSize() const { return dataSize; }

		void ResetOffset() { offset = 0; }
		unsigned int GetOffset() const { return offset; }
		void SetOffset(unsigned int newOffset) { offset = std::min(newOffset, dataSize); }
		void IncrementOffset(unsigned int amount) { offset = std::min(offset + amount, dataSize); }

		bool GetHasUnderlyingData() const { return nullptr != data; }
		unsigned char* GetUnderlyingData() const { return data; }
		unsigned char* GetUnderlyingDataAt(unsigned int offset) const { return data + offset; }
		unsigned char* GetUnderlyingDataAtOffset() const { return data + offset; }

	protected:
		unsigned char* data;

		unsigned int dataSize;

		unsigned int offset;
	};
}
