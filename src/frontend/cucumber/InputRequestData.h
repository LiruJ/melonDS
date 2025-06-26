#pragma once

namespace cucumberDS
{
	struct InputRequestData
	{
	public:
		unsigned char GetXPosition() const { return xPosition; }
		unsigned char GetYPosition() const { return yPosition; }
	private:
		unsigned char xPosition;
		unsigned char yPosition;
	};
}