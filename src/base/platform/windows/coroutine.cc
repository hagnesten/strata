#include "base/runtime/coroutine.hh"
#include "base/runtime/memory.hh"

#include <windows.h>

namespace base {

	static void WINAPI coroutine_main ( void* parameter ) {

		coroutine* item = static_cast < coroutine* > ( parameter );

		for ( ;; ) {

			item->body ( item->context );
			item->finished = true;
			SwitchToFiber ( item->caller );

		}

	}

	coroutine* make_coroutine ( coroutine_body body, void* context ) {

		coroutine* item = make < coroutine > ( 1 );

		if ( !item )
			return nullptr;

		item->body = body;
		item->context = context;
		item->fiber = CreateFiberEx ( 256 * 1024, COROUTINE_STACK, 0, coroutine_main, item );

		if ( !item->fiber ) {

			release ( item );

			return nullptr;

		}

		return item;

	}

	void free_coroutine ( coroutine* item ) {

		if ( !item )
			return;

		if ( item->fiber )
			DeleteFiber ( item->fiber );

		release ( item );

	}

	void enter_coroutine ( coroutine* item ) {

		if ( !IsThreadAFiber ( ) )
			ConvertThreadToFiber ( nullptr );

		item->caller = GetCurrentFiber ( );
		item->finished = false;
		SwitchToFiber ( item->fiber );

	}

	void leave_coroutine ( coroutine* item ) {

		SwitchToFiber ( item->caller );

	}

}
