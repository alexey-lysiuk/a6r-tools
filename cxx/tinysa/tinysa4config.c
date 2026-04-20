#include <stdio.h>

#define TINYSA_F303

#include "tinySA/nanovna.h"

static inline uint32_t ror(uint32_t op1, uint32_t op2)
{
	return (op1 >> op2) | (op1 << (32 - op2));
}

static bool VerifyConfig(const char* path)
{
	FILE* f = fopen(path, "rb");

	if (!f)
	{
		printf("ERROR: Failed to open file %s\n", path);
		return false;
	}

	config_t config;

	const size_t bytesread = fread(&config, 1, sizeof config, f);

	if (bytesread != sizeof config)
	{
		printf("ERROR: Failed to read %zu bytes from file %s, read %zu bytes only\n", sizeof config, path, bytesread);
		return false;
	}

	const uint32_t* current = (uint32_t*)&config;
	const uint32_t* end = (uint32_t*)(current + (sizeof config  - sizeof config.checksum) / sizeof(uint32_t));
	uint32_t checksum = 0;

	while (current < end)
	{
		checksum = ror(checksum, 31) + *current;
		++current;
	}

	if (checksum == config.checksum)
		printf("%s: OK, checksum 0x%08X\n", path, checksum);
	else
		printf("%s: checksum mismatch, calculated 0x%08X vs. stored 0x%08X\n", path, checksum, (uint32_t)config.checksum);

	fclose(f);

	return true;
}

int main(int argc, char** argv)
{
	if (argc == 1)
	{
		printf("Usage: %s .cfg ...\n", argv[0]);
		return EXIT_FAILURE;
	}

	bool isok = true;

	for (int i = 1; i < argc; ++i)
	{
		const char* path = argv[i];
		isok &= VerifyConfig(path);
	}

	return isok ? EXIT_SUCCESS : EXIT_FAILURE;
}
