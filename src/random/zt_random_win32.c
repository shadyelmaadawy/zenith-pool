#include <windows.h>
#include <bcrypt.h>
#include "random/zt_random.h"

#pragma comment(lib, "bcrypt.lib")

bool fill(void* out_value, unsigned long long size) {
	return BCRYPT_SUCCESS(BCryptGenRandom(NULL, out_value, (ULONG)size, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}