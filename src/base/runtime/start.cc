#include "app/main/main.hh"

#include <windows.h>

extern "C" void start ( ) {

	ExitProcess ( static_cast < UINT > ( app::run ( ) ) );

}
