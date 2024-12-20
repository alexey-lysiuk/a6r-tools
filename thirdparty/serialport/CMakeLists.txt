cmake_minimum_required(VERSION 3.10)

project(serialport LANGUAGES C)

# TODO: Generate config.h

if(APPLE)
	set(PLATFORM_SOURCES
		macosx.c
	)
	set(PLATFORM_LINKER_FLAGS
		"-framework CoreFoundation"
		"-framework IOKit"
	)
elseif(BSD)
	set(PLATFORM_SOURCES
		freebsd.c
	)
elseif(LINUX)
	set(PLATFORM_SOURCES
		linux.c
		linux_termios.c
		linux_termios.h
	)
else()
	set(PLATFORM_SOURCES
		windows.c
	)
	set(PLATFORM_LINKER_FLAGS
		setupapi
	)
endif()

add_library(serialport STATIC
	serialport.c
	libserialport.h
	libserialport_internal.h
	timing.c
	${PLATFORM_SOURCES}
)

target_include_directories(serialport PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(serialport PRIVATE LIBSERIALPORT_ATBUILD)
target_link_libraries(serialport PRIVATE "${PLATFORM_LINKER_FLAGS}")

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
	set(SERIALPORT_TESTS
		list_ports
		port_info
		send_receive
	)

	foreach(SERIALPORT_TEST IN LISTS SERIALPORT_TESTS)
		add_executable(${SERIALPORT_TEST} ${SERIALPORT_TEST}.c)
		target_link_libraries(${SERIALPORT_TEST} PRIVATE serialport)
	endforeach()
endif()
