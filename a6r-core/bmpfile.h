#pragma once

#include <cstdint>
//#include <cstdlib>
#include <istream>

class BMPFile
{
public:
	explicit BMPFile(const char* filename = nullptr);
	explicit BMPFile(std::istream& stream);
	~BMPFile();

	void Load(const char* filename);
	void Load(std::istream& stream);
	void Save(const char* filename) const;
	//void Reset();

private:
	uint8_t* pixelData = nullptr;
};
