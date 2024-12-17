#include "binstream.h"

#include <cassert>
#include <streambuf>

// // TODO: Add swap bytes on big endian platforms

BinaryInputStream::BinaryInputStream(std::basic_streambuf<char_type, traits_type>* streambuf)
{
	init(streambuf);
}

//BinaryInputStream& BinaryInputStream::operator>>(int8_t& value)
//{
//	return operator>>(reinterpret_cast<uint8_t&>(value));
//}
//
//BinaryInputStream& BinaryInputStream::operator>>(uint8_t& value)
//{
//	char* const valuePtr = reinterpret_cast<char*>(&value);
//
//	if (rdbuf()->sgetn(valuePtr, sizeof value) != sizeof value)
//		setstate(failbit);
//
//	return *this;
//}
//
//BinaryInputStream& BinaryInputStream::operator>>(int16_t& value)
//{
//	return operator>>(reinterpret_cast<uint16_t&>(value));
//}
//
//BinaryInputStream& BinaryInputStream::operator>>(Uint16& value)
//{
//	char* const valuePtr = reinterpret_cast<char*>(&value);
//
//	if (rdbuf()->sgetn(valuePtr, sizeof value) != sizeof value)
//		setstate(failbit);
//
//	return *this;
//}
//
//BinaryInputStream& BinaryInputStream::operator>>(Sint32& value)
//{
//	return operator>>(reinterpret_cast<Uint32&>(value));
//}
//
//BinaryInputStream& BinaryInputStream::operator>>(Uint32& value)
//{
//	if (good())
//	{
//		char* const valuePtr = reinterpret_cast<char*>(&value);
//
//		if (sizeof value == rdbuf()->sgetn(valuePtr, sizeof value))
//		{
//			value = SDL_SwapLE32(value);
//		}
//		else
//		{
//			setstate(failbit);
//		}
//	}
//
//	return *this;
//}

std::streamsize BinaryInputStream::read(char* const buffer, const std::streamsize count)
{
	assert(rdbuf());
	assert(buffer);
	assert(count > 0);

	const std::streamsize bytesRead = rdbuf()->sgetn(buffer, count);

	if (count != bytesRead)
		setstate(failbit);

	return bytesRead;
}

BinaryInputStream& BinaryInputStream::ignore(std::streamsize count)
{
	seekg(count, std::ios_base::cur);
	return *this;
}

BinaryInputStream::pos_type BinaryInputStream::seekg(const std::streampos pos)
{
	assert(rdbuf());
	return rdbuf()->pubseekpos(pos, in);
}

BinaryInputStream::pos_type BinaryInputStream::seekg(const std::streamoff off, const seekdir way)
{
	assert(rdbuf());
	return rdbuf()->pubseekoff(off, way, in);
}
