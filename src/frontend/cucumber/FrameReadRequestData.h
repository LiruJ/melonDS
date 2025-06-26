#pragma once

namespace cucumberDS
{
	struct FrameReadRequestData
	{
	public:
		unsigned char GetScreenId() const { return screenId; }
	private:
		unsigned char screenId;
	};
}
