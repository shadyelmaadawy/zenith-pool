/**
 * @file memory_win32.c
 * @brief Win32 implementation of the contract declared in memory.h.
 *
 * notes:
 *  - Backed by the default process heap (GetProcessHeap).
 *  - HEAP_ZERO_MEMORY provides the zero-init guarantee.

 *  - memory_copy is a forward byte copy; overlap is not supported.
*/

#include <windows.h>
#include <stdbool.h>

#include "memory/zt_memory.h"

bool zt_allocate(void** address, size_t size) {

	if (address == NULL || size == 0)
		return false;

	bool allocate_state		 = false;
	HANDLE process_heap		 = NULL;
	void* memory_address	 = NULL;
	

	process_heap = GetProcessHeap();
	if (process_heap == NULL) 
		goto cleanup;

	memory_address = HeapAlloc(process_heap, HEAP_ZERO_MEMORY, size);
	if (memory_address == NULL)
		goto cleanup;

	allocate_state = true;
	*address	   = memory_address;

cleanup:
	
	return allocate_state;

}

bool zt_deallocate(void** address) {

	if (address == NULL || *address == NULL)
		return false;

	bool deallocate_state = false;
	HANDLE process_heap	  = NULL;

	process_heap = GetProcessHeap();
	if(process_heap == NULL) 
		goto cleanup;


	deallocate_state = HeapFree(process_heap, 0, *address) != 0;

	if (deallocate_state)
		*address = NULL;

cleanup:

	return deallocate_state;

}

bool zt_reallocate(void** address, size_t new_size) {
	
	if (address == NULL)
		return false;

	if (*address == NULL) 
		return zt_allocate(address, new_size);

	bool		  reallocate_state	 = false;
	HANDLE		  process_heap		 = NULL;
	void*		  memory_address     = NULL;
	
	process_heap = GetProcessHeap();
	if (process_heap == NULL)
		goto cleanup;
	
	memory_address = HeapReAlloc(process_heap, HEAP_ZERO_MEMORY, *address, new_size);
	if (memory_address == NULL)
		goto cleanup;

	*address		 = memory_address;
	reallocate_state = true;

cleanup:
	return reallocate_state;

}

void zt_memcpy(void* dest, const void* src, size_t size) {

	if (dest == NULL || src == NULL || size == 0)
		return;

	unsigned char* d = (unsigned char*)dest;
	const unsigned char* s = (const unsigned char*)src;

	while (size--)
		*d++ = *s++;

}

bool zt_equal(const void* a, const void* b, size_t size) {

	const unsigned char* x = (const unsigned char*)a;
	const unsigned char* y = (const unsigned char*)b;

	while (size--)
		if (*x++ != *y++)
			return false;

	return true;

}

void zt_memset(void* dest, int value, size_t size) {
	if (dest == NULL)
		return;

	volatile unsigned char* d = (volatile unsigned char*)dest;
	unsigned char byte_value = (unsigned char)value;

	while (size--)
		*d++ = byte_value;
}


