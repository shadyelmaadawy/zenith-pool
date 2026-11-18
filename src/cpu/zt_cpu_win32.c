#include <windows.h>
#include "cpu/zt_cpu.h"

unsigned short zt_cpu_processors() {

	SYSTEM_INFO system_info = { 0 };
	GetSystemInfo(&system_info);

	if (system_info.dwNumberOfProcessors > 0)
		return (unsigned short)(system_info.dwNumberOfProcessors);

	return 0;
}

