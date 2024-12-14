#include <cassert>
#include <iostream>
#include <string>

#include <libserialport.h>


int main()
{
	sp_port** ports;
	sp_return result = sp_list_ports(&ports);
	assert(result == SP_OK);

	for (size_t i = 0; ports[i]; ++i)
	{
		sp_port* port = ports[i];
		const sp_transport transport = sp_get_port_transport(port);

		if (transport != SP_TRANSPORT_USB)
			continue;

		int vid, pid;
		result = sp_get_port_usb_vid_pid(port, &vid, &pid);
		assert(result == SP_OK);

		if (vid != 0x0483 || pid != 0x5740)
			continue;

		result = sp_open(port, SP_MODE_READ_WRITE);
		assert(result == SP_OK);

		std::string command = "help";

		static const size_t BUFFER_SIZE = 1024;
		char buffer[BUFFER_SIZE];

		static const unsigned int timeout = 100;

		while (true)
		{
			command += '\r';

			result = sp_blocking_write(port, command.c_str(), command.size(), timeout);
			assert(result == command.size());

			while (true)
			{
				result = sp_blocking_read(port, &buffer[0], BUFFER_SIZE, timeout);

				if (result == 0)
					break;
				else if (result < 0)
					exit(1);  // TODO
				else
					buffer[result] = '\0';

				std::cout << &buffer[command.size() + 1];
			}

			std::cin >> command;

			if (command.empty())
				break;
		}

		result = sp_close(port);
		assert(result == SP_OK);

		break;
	}

	sp_free_port_list(ports);

	return 0;
}
