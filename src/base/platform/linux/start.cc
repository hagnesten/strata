#include "app/main/main.hh"
#include "base/platform/os.hh"
#include "base/platform/linux/calls.hh"

typedef void ( *starter ) ( );

extern "C" {

	extern starter __init_array_start [ ] __attribute__ ( ( weak ) );
	extern starter __init_array_end [ ] __attribute__ ( ( weak ) );

	__attribute__ ( ( used ) ) void linux_entry ( long* stack ) {

		int32_t count = static_cast < int32_t > ( stack [ 0 ] );
		char** arguments = reinterpret_cast < char** > ( stack + 1 );

		base::linux_calls::set_environment ( arguments + count + 1 );

		for ( starter* at = __init_array_start; at && at < __init_array_end; at++ )
			( *at ) ( );

		base::os::exit_process ( app::run ( count, arguments, false ) );

	}

	__attribute__ ( ( naked, noreturn ) ) void _start ( ) {

		__asm__ volatile (
			"xor %rbp, %rbp\n"
			"mov %rsp, %rdi\n"
			"and $-16, %rsp\n"
			"call linux_entry\n"
			"ud2\n" );

	}

}
