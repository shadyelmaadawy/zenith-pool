#include <windows.h>
#include "synchronization/zt_atomic.h"

void zt_atomic_increment(volatile long* value) {
	InterlockedIncrement(value);
}

void zt_atomic_decrement(volatile long* value) {
	InterlockedDecrement(value);
}

void zt_atomic_read(volatile long* value, long* out_value) {
	*out_value = InterlockedCompareExchange(value, 0, 0);
}

void zt_atomic_write(volatile long* value, long new_value) {
	InterlockedExchange(value, new_value);
}
