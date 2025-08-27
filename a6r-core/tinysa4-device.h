#pragma once

#include <cstdint>
#include <cstdlib>

struct sp_port;

namespace TinySA4
{

class Device
{
public:
	static constexpr uint16_t VID = 0x0483;
	static constexpr uint16_t PID = 0x5740;

	explicit Device(const char* portname = nullptr);
	~Device();

	size_t Send(const void* buffer, size_t size);
	size_t Receive(void* buffer, size_t size);

	size_t GetTimeOut() const { return timeout; }
	void SetTimeOut(const size_t value) { timeout = value; }

private:
	sp_port* device = nullptr;
	size_t timeout = 10;  // ms
};

} // namespace TinySA4
