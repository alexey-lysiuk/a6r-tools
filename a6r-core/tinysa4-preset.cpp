#include "tinysa4-preset.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

static inline uint32_t ror(uint32_t op1, uint32_t op2)
{
  return (op1 >> op2) | (op1 << (32 - op2));
}

static bool VerifyPreset(const char* path)
{
	FILE* f = fopen(path, "rb");

	if (!f)
	{
		printf("ERROR: Failed to open file %s\n", path);
		return false;
	}

	TinySA4::Preset preset;

	const size_t bytesread = fread(&preset, 1, sizeof preset, f);

	if (bytesread != sizeof preset)
	{
		printf("ERROR: Failed to read %zu bytes from file %s, read %zu bytes only\n", sizeof preset, path, bytesread);
		return false;
	}

	const uint32_t* current = reinterpret_cast<const uint32_t*>(&preset);
	const uint32_t* end = (uint32_t*)(current + sizeof preset / sizeof(uint32_t) - 2);  // without checksum and four bytes padding
	uint32_t checksum = 0;

	while (current < end)
	{
		checksum = ror(checksum, 31) + *current;
		++current;
	}

	if (checksum == preset.checksum)
		printf("%s: OK, checksum 0x%08X\n", path, checksum);
	else
		printf("%s: checksum mismatch, calculate 0x%08X vs. stored 0x%08X\n", path, checksum, preset.checksum);

	fclose(f);

	return true;
}

int main(int argc, char** argv)
{
	if (argc == 1)
	{
		printf("Usage: %s .prs ...\n", argv[0]);
		return EXIT_FAILURE;
	}

	bool isok = true;

	for (int i = 1; i < argc; ++i)
	{
		const char* path = argv[i];
		isok &= VerifyPreset(path);
	}

	return isok ? EXIT_SUCCESS : EXIT_FAILURE;
}
