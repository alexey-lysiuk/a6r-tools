#include <iostream>
#include <stdexcept>
#include <string>

#include "tinysa4.h"


static void RunInteractiveMode(TinySA4& device)
{
	std::cout << "Type 'exit' to leave interactive mode" << std::endl;

	std::string command = "help";

	constexpr size_t BUFFER_SIZE = 1024;
	char buffer[BUFFER_SIZE];

	while (true)
	{
		command += '\r';

		if (device.Send(command.c_str(), command.size()) != command.size())
			std::cerr << "Incomplete send to device" << std::endl;  // TODO: make it fatal?

		while (true)
		{
			const size_t read = device.Receive(&buffer[0], BUFFER_SIZE - 1);

			if (read == 0)
				break;
			else
				buffer[read] = '\0';

			std::cout << &buffer[command.size() + 1];
		}

		std::cin >> command;

		if (command == "exit")
			break;
	}
}

int main()
{
	try
	{
		TinySA4 device;
		RunInteractiveMode(device);
	}
	catch (const std::runtime_error& ex)
	{
		std::cerr << ex.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
