#include <windows.h>

extern "C" BOOL WINAPI library_start ( HINSTANCE instance, DWORD reason, LPVOID reserved ) {

	( void ) instance;
	( void ) reason;
	( void ) reserved;

	return TRUE;

}
