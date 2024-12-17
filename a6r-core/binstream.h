#pragma once

#include <cstdint>
#include <ios>
//#include <streambuf>

// Binary input stream, little endian
// TODO: Add swap bytes on big endian platforms

class BinaryInputStream : public std::ios
{
public:
	explicit BinaryInputStream(std::basic_streambuf<char_type, traits_type>* streambuf = nullptr);

//	BinaryInputStream& operator>>(int8_t& value);
//	BinaryInputStream& operator>>(uint8_t& value);
//	BinaryInputStream& operator>>(int16_t& value);
//	BinaryInputStream& operator>>(uint16_t& value);
//	BinaryInputStream& operator>>(int32_t& value);
//	BinaryInputStream& operator>>(uint32_t& value);
//	BinaryInputStream& operator>>(int64_t& value);
//	BinaryInputStream& operator>>(uint64_t& value);

	template <typename T>
	BinaryInputStream& operator>>(T& value)
	{
		char* const valuePtr = reinterpret_cast<char*>(&value);
		read(valuePtr, sizeof value);
		return *this;
	}

	std::streamsize read(char* buffer, std::streamsize count);
	BinaryInputStream& ignore(std::streamsize count = 1);

	pos_type seekg(const std::streampos pos);
	pos_type seekg(const std::streamoff off, const seekdir way);
};

