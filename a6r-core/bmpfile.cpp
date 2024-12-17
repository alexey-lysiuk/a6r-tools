#include "bmpfile.h"

#include <fstream>
#include <stdexcept>

#include "binstream.h"


BMPFile::BMPFile(const char* filename)
{
	if (filename)
		Load(filename);
}

BMPFile::BMPFile(BinaryInputStream& stream)
{
	Load(stream);
}

BMPFile::~BMPFile()
{
	if (pixelData)
		delete[] pixelData;
}

void BMPFile::Load(const char* filename)
{
	//std::ifstream stream(filename, std::ios_base::in | std::ios_base::binary);
	//stream.exceptions(std::ios_base::badbit | std::ios_base::failbit);
	std::filebuf filebuf;

	if (!filebuf.open(filename, std::ios_base::in | std::ios_base::binary))
	{
		std::string message = "Could not open file ";
		message += filename;
		throw std::runtime_error(message);
	}

	BinaryInputStream stream(&filebuf);
	Load(stream);
}

void BMPFile::Load(BinaryInputStream& stream)
{
	char magic[2];
	stream.read(magic, 2);

	if (magic[0] != 'B' || magic[1] != 'M')
		std::runtime_error("Not a BMP file");

	uint32_t fileSize, dataOffset, headerSize;
	stream >> fileSize;
	stream.ignore(sizeof(uint16_t) * 2);  // skip reserved members
	stream >> dataOffset >> headerSize;
}

void BMPFile::Save(const char* filename) const
{
	throw std::runtime_error("not implemented");
}

//void BMPFile::Reset()
//{
//	if (pixeldata)
//	{
//		delete[] pixeldata;
//		pixeldata = nullptr;
//	}
//
//
//}
