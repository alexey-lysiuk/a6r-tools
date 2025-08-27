#include "tinysa4-device.h"

#include <cassert>
#include <stdexcept>
#include <string>

#include <libserialport.h>


class DeviceLocator
{
public:
	DeviceLocator(const uint16_t vid, const uint16_t pid, const char* const portname = nullptr)
	: vid(vid)
	, pid(pid)
	, portname(portname)
	, device(portname ? LocateByPortName() : LocateByEnumeration())
	{
	}

	~DeviceLocator()
	{
		if (devices)
			sp_free_port_list(devices);
	}

	sp_port* operator*() const
	{
		return device;
	}

private:
	const uint16_t vid;
	const uint16_t pid;
	const char* const portname;

	sp_port** devices = nullptr;
	sp_port* device;

	sp_port* LocateByPortName()
	{
		sp_port* device;

		if (sp_get_port_by_name(portname, &device) != SP_OK)
		{
			std::string message = "Could not find device at port ";
			message += portname;
			throw std::runtime_error(message);
		}

		assert(device);
		return IsMatch(device) ? device : nullptr;
	}

	sp_port* LocateByEnumeration()
	{
		if (sp_list_ports(&devices) != SP_OK)
			throw std::runtime_error("Could not enumerate devices");

		assert(devices);

		for (sp_port** current = devices; *current; ++current)
		{
			if (IsMatch(*current))
			{
				sp_port* device;

				if (sp_copy_port(*current, &device) != SP_OK)
				{
					std::string message = "Could not allocate device at port ";
					message += sp_get_port_name(*current);
					throw std::runtime_error(message);
				}

				return device;
			}
		}

		return nullptr;
	}

	bool IsMatch(sp_port* device)
	{
		assert(device);

		const sp_transport transport = sp_get_port_transport(device);

		if (transport != SP_TRANSPORT_USB)
			return false;

		int dvid, dpid;

		if (sp_get_port_usb_vid_pid(device, &dvid, &dpid) != SP_OK)
		{
			std::string message = "Could not obtain device VID and PID at port ";
			message += sp_get_port_name(device);
			throw std::runtime_error(message);
		}

		return int(vid) == dvid && int(pid) == dpid;
	}
};


namespace TinySA4
{

Device::Device(const char* portname)
: device(*DeviceLocator(VID, PID, portname))
{
	if (device == nullptr)
	{
		std::string message = "Could not find tinySA4 device";

		if (portname)
		{
			message += " at port ";
			message += portname;
		}

		throw std::runtime_error(message);
	}

	if (sp_open(device, SP_MODE_READ_WRITE) != SP_OK)
		throw std::runtime_error("Could not open tinySA4 device");
}

Device::~Device()
{
	if (device)
	{
		// TODO: Check and report error on close
		sp_close(device);
		sp_free_port(device);
	}
}

size_t Device::Send(const void* buffer, size_t size)
{
	if (size == 0)
		return 0;

	const sp_return result = sp_blocking_write(device, buffer, size, unsigned(timeout));

	if (result < 0)
	{
		std::string message = "Could not write to device at port ";
		message += sp_get_port_name(device);
		throw std::runtime_error(message);
	}

	return size_t(result);
}

size_t Device::Receive(void* buffer, size_t size)
{
	if (size == 0)
		return 0;

	const sp_return result = sp_blocking_read(device, buffer, size, unsigned(timeout));

	if (result < 0)
	{
		std::string message = "Could not write to device at port ";
		message += sp_get_port_name(device);
		throw std::runtime_error(message);
	}

	return size_t(result);
}

} // namespace TinySA4
