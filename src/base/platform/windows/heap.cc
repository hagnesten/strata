#include "base/runtime/memory.hh"

#include <windows.h>

namespace base {

	void* allocate ( size_t size ) {

		return HeapAlloc ( GetProcessHeap ( ), HEAP_ZERO_MEMORY, size ? size : 1 );

	}

	void release ( void* block ) {

		if ( block )
			HeapFree ( GetProcessHeap ( ), 0, block );

	}

}
