#pragma once

#include "base/runtime/memory.hh"

namespace base {

	typedef int32_t ( *compare_items ) ( void* context, const void* a, const void* b );

	bool sort_pointers ( void** items, size_t count, compare_items compare, void* context );

}
