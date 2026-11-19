#include "base/runtime/coroutine.hh"
#include "base/runtime/memory.hh"
#include "base/platform/os.hh"
#include "base/platform/linux/calls.hh"

extern "C" {

	__attribute__ ( ( naked ) ) void strata_switch_stack ( void** save, void* load ) {

		__asm__ volatile (
			"push %rbp\n"
			"push %rbx\n"
			"push %r12\n"
			"push %r13\n"
			"push %r14\n"
			"push %r15\n"
			"mov %rsp, (%rdi)\n"
			"mov %rsi, %rsp\n"
			"pop %r15\n"
			"pop %r14\n"
			"pop %r13\n"
			"pop %r12\n"
			"pop %rbx\n"
			"pop %rbp\n"
			"ret\n" );

	}

	__attribute__ ( ( used ) ) void strata_coroutine_main ( base::coroutine* item ) {

		for ( ;; ) {

			item->body ( item->context );
			item->finished = true;
			strata_switch_stack ( &item->saved, item->caller_saved );

		}

	}

	__attribute__ ( ( naked ) ) void strata_coroutine_start ( ) {

		__asm__ volatile (
			"mov %r12, %rdi\n"
			"sub $8, %rsp\n"
			"call strata_coroutine_main\n"
			"ud2\n" );

	}

}

namespace base {

	coroutine* make_coroutine ( coroutine_body body, void* context ) {

		coroutine* item = make < coroutine > ( 1 );

		if ( !item )
			return nullptr;

		item->body = body;
		item->context = context;
		item->stack_size = COROUTINE_STACK;
		item->stack = os::allocate_pages ( item->stack_size );

		if ( !item->stack ) {

			release ( item );

			return nullptr;

		}

		linux_calls::call3 ( LINUX_MPROTECT, reinterpret_cast < long > ( item->stack ), 4096, LINUX_PROT_NONE );

		uint64_t* top = reinterpret_cast < uint64_t* > ( static_cast < uint8_t* > ( item->stack ) + item->stack_size );

		top [ -1 ] = 0;
		top [ -2 ] = reinterpret_cast < uint64_t > ( &strata_coroutine_start );
		top [ -3 ] = 0;
		top [ -4 ] = 0;
		top [ -5 ] = reinterpret_cast < uint64_t > ( item );
		top [ -6 ] = 0;
		top [ -7 ] = 0;
		top [ -8 ] = 0;
		item->saved = top - 8;

		return item;

	}

	void free_coroutine ( coroutine* item ) {

		if ( !item )
			return;

		os::release_pages ( item->stack, item->stack_size );
		release ( item );

	}

	void enter_coroutine ( coroutine* item ) {

		item->finished = false;
		strata_switch_stack ( &item->caller_saved, item->saved );

	}

	void leave_coroutine ( coroutine* item ) {

		strata_switch_stack ( &item->saved, item->caller_saved );

	}

}
