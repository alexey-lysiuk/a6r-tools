#pragma once

#include <cstdint>
//#include <ios>
//#include <cstdlib>
//#include <istream>
#include "binstream.h"

class BMPFile
{
public:
	explicit BMPFile(const char* filename = nullptr);
	explicit BMPFile(BinaryInputStream& stream);
	~BMPFile();

	void Load(const char* filename);
	void Load(BinaryInputStream& stream);
	void Save(const char* filename) const;
	//void Reset();

private:
	uint8_t* pixelData = nullptr;
};
