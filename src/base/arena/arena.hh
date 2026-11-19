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

		void* take_raw ( size_t size );

		char* keep ( const char* data, size_t size );

		void reset ( );

		void recycle ( );

		template < typename type > type* make ( size_t count ) {

			return static_cast < type* > ( take ( sizeof ( type ) * count ) );

		}

	};

	template < typename type > bool push ( arena& memory, type*& items, uint32_t& count, uint32_t& room, const type& value ) {

		if ( count == room ) {

			uint32_t grown = room ? room * 2 : 8;
			type* made = memory.make < type > ( grown );

			if ( !made )
				return false;

			if ( items )
				copy ( made, items, sizeof ( type ) * count );

			items = made;
			room = grown;

		}

		items [ count++ ] = value;

		return true;

	}

}
