#pragma once

namespace cucumberDS
{
	struct MemoryReadRequestData
	{
	public:
		unsigned int GetAddress() const { return address; }
		unsigned int GetSize() const { return size; }
	private:
		unsigned int address;
		unsigned int size;
	};
}
