#pragma once

#include "base/runtime/memory.hh"

#define ARENA_BLOCK 65536

namespace base {

	struct arena_block {

		arena_block* next;
		size_t size;
		size_t used;

	};

	struct arena_mark {

		arena_block* block;
		size_t used;

	};

	struct arena {

		arena_block* first = nullptr;
		size_t total = 0;

		arena_mark mark ( ) const;

		void rewind ( const arena_mark& to );

		arena ( ) = default;

		arena ( const arena& ) = delete;

		arena& operator = ( const arena& ) = delete;

		~arena ( );

		void* take ( size_t size );

		char* keep ( const char* data, size_t size );

		void reset ( );

		template < typename type > type* make ( size_t count ) {

			return static_cast < type* > ( take ( sizeof ( type ) * count ) );

		}

	};

}
