#pragma once

#include <stdint.h>
#include <stddef.h>

#define COROUTINE_STACK ( 1024u * 1024u )

namespace base {

	typedef void ( *coroutine_body ) ( void* context );

	struct coroutine {

		coroutine_body body;
		void* context;
		void* fiber;
		void* caller;
		void* stack;
		size_t stack_size;
		void* saved;
		void* caller_saved;
		bool finished;

	};

	coroutine* make_coroutine ( coroutine_body body, void* context );

	void free_coroutine ( coroutine* item );

	void enter_coroutine ( coroutine* item );

	void leave_coroutine ( coroutine* item );

}
