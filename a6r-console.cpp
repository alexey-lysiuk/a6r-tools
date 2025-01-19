#include <exception>
#include <iostream>
#include <string>

#include "argparse.hpp"
#include "tinysa4.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_BMP
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"


static void ConvertBMP(const std::string& filename)
{
	int width, height, channels;
	stbi_uc* data = stbi_load(filename.c_str(), &width, &height, &channels, 0);

	if (data == nullptr)
	{
		std::string message = "Could not read BMP file ";
		message += filename;
		throw std::runtime_error(message);
	}

	const int result = stbi_write_bmp(filename.c_str(), width, height, channels, data);

	if (result != 1)
	{
		std::string message = "Could not write BMP file ";
		message += filename;
		throw std::runtime_error(message);
	}
}

static void SendReceive(TinySA4& device, std::string& command)
{
	command += '\r';

	const size_t commandsize = command.size();

	constexpr size_t BUFFER_SIZE = 1024;
	char buffer[BUFFER_SIZE];

	if (device.Send(command.c_str(), commandsize) != commandsize)
		std::cerr << "Incomplete send to device" << std::endl;  // TODO: make it fatal?

	while (true)
	{
		const size_t read = device.Receive(&buffer[0], BUFFER_SIZE - 1);

		if (read == 0)
			break;
		else
			buffer[read] = '\0';

		std::cout << &buffer[commandsize + 1];  // skip repeated command
	}
}

static void RunInteractiveMode(TinySA4& device)
{
	std::cout << "Type 'exit' to leave interactive mode" << std::endl;

	std::string command = "help";

	while (true)
	{
		SendReceive(device, command);

		std::cin >> command;

		if (command == "exit")
			break;
	}
}

int main(int argc, char** argv)
{
	argparse::ArgumentParser args("a6r-console", "0.0.1");
	auto& group = args.add_mutually_exclusive_group(true);
	group.add_argument("-c", "--command").metavar("COMMAND").append().help("execure command(s)");
	group.add_argument("-i", "--interactive").help("enter interactive mode").flag();
	group.add_argument("-x", "--convert").metavar("FILE").append().help("convert BMP file(s)");

	try
	{
		args.parse_args(argc, argv);
	}
	catch (const std::exception& ex)
	{
		std::cerr << ex.what() << std::endl << args;
		return EXIT_FAILURE;
	}

	try
	{
		if (args.present("--convert"))
		{
			const auto filenames = args.get<std::vector<std::string>>("--convert");

			for (const std::string& filename : filenames)
				ConvertBMP(filename);

			return EXIT_SUCCESS;
		}

		TinySA4 device;

		if (args.get<bool>("--interactive"))
			RunInteractiveMode(device);
		else
		{
			auto commands = args.get<std::vector<std::string>>("--command");

			for (std::string& command : commands)
			{
				SendReceive(device, command);
				std::cout << std::endl;
			}
		}
	}
	catch (const std::exception& ex)
	{
		std::cerr << ex.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
