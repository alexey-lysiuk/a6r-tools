#include <cassert>
#include <iostream>
#include <string>

#include <libserialport.h>


static void RunInteractiveMode(sp_port* port)
{
	constexpr unsigned int TIMEOUT = 100;
	std::string command = "help";

	constexpr size_t BUFFER_SIZE = 1024;
	char buffer[BUFFER_SIZE];

	while (true)
	{
		command += '\r';

		sp_return result = sp_blocking_write(port, command.c_str(), command.size(), TIMEOUT);
		assert(result == command.size());

		while (true)
		{
			result = sp_blocking_read(port, &buffer[0], BUFFER_SIZE, TIMEOUT);

			if (result == SP_OK)
				break;
			else if (result < 0)
			{
				assert(false);
				return;
			}
			else
				buffer[result] = '\0';

			std::cout << &buffer[command.size() + 1];
		}

		std::cin >> command;

		if (command.empty())
			break;
	}
}

static bool Run(sp_port* port)
{
	const sp_transport transport = sp_get_port_transport(port);

	if (transport != SP_TRANSPORT_USB)
		return false;

	int vid, pid;
	sp_return result = sp_get_port_usb_vid_pid(port, &vid, &pid);
	assert(result == SP_OK);

	if (vid != 0x0483 || pid != 0x5740)
		return false;

	result = sp_open(port, SP_MODE_READ_WRITE);
	assert(result == SP_OK);

	RunInteractiveMode(port);

	result = sp_close(port);
	assert(result == SP_OK);

	return true;
}

int main()
{
	sp_port** ports;
	sp_return result = sp_list_ports(&ports);
	assert(result == SP_OK);

	for (size_t i = 0; ports[i]; ++i)
	{
		if (Run(ports[i]))
			break;
	}

	sp_free_port_list(ports);

	return 0;
}
