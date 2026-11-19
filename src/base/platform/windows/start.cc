#include "app/main/main.hh"
#include "base/runtime/memory.hh"

#include <windows.h>
#include <shellapi.h>

static int32_t read_arguments ( char** out, int32_t room ) {

	int32_t count = 0;
	int32_t kept = 0;
	wchar_t** wide = CommandLineToArgvW ( GetCommandLineW ( ), &count );

	for ( int32_t i = 0; wide && i < count && kept < room; i++ ) {

		int32_t size = WideCharToMultiByte ( CP_UTF8, 0, wide [ i ], -1, nullptr, 0, nullptr, nullptr );
		char* made = size > 0 ? base::make < char > ( static_cast < size_t > ( size ) + 1 ) : nullptr;

		if ( made && WideCharToMultiByte ( CP_UTF8, 0, wide [ i ], -1, made, size, nullptr, nullptr ) > 0 )
			out [ kept++ ] = made;

	}

	if ( wide )
		LocalFree ( wide );

	return kept;

}

extern "C" void start ( ) {

	static char* arguments [ MAIN_ARGUMENTS ];
	const uint8_t* image = reinterpret_cast < const uint8_t* > ( GetModuleHandleW ( nullptr ) );
	const IMAGE_NT_HEADERS* headers = reinterpret_cast < const IMAGE_NT_HEADERS* > ( image + reinterpret_cast < const IMAGE_DOS_HEADER* > ( image )->e_lfanew );
	int32_t count = read_arguments ( arguments, MAIN_ARGUMENTS );

	ExitProcess ( static_cast < UINT > ( app::run ( count, arguments, headers->OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI ) ) );

}
