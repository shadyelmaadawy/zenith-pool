#include <windows.h>
#include <stdarg.h>
#include <windows.h>
#include "terminal/zt_terminal.h"


void write_to_std(char* format, ...) {

	char buffer[256];
	int length;
	DWORD written = 0;
	va_list args;

	va_start(args, format);
	length = wvsprintfA(buffer, format, args);
	va_end(args);

	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buffer, (DWORD)length, &written, NULL);
}