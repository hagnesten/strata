#include "base/sort/sort.hh"

namespace base {

	bool sort_pointers ( void** items, size_t count, compare_items compare, void* context ) {

		if ( count < 2 )
			return true;

		for ( size_t i = 1; i < count && count <= 16; i++ ) {

			void* here = items [ i ];
			size_t k = i;

			while ( k && compare ( context, items [ k - 1 ], here ) > 0 ) {

				items [ k ] = items [ k - 1 ];
				k--;

			}

			items [ k ] = here;

		}

		if ( count <= 16 )
			return true;

		void** spare = make < void* > ( count );

		if ( !spare )
			return false;

		void** from = items;
		void** to = spare;

		for ( size_t width = 1; width < count; width *= 2 ) {

			for ( size_t start = 0; start < count; start += width * 2 ) {

				size_t middle = min ( start + width, count );
				size_t end = min ( start + width * 2, count );
				size_t a = start;
				size_t b = middle;
				size_t at = start;

				while ( a < middle && b < end )
					to [ at++ ] = compare ( context, from [ b ], from [ a ] ) < 0 ? from [ b++ ] : from [ a++ ];

				while ( a < middle )
					to [ at++ ] = from [ a++ ];

				while ( b < end )
					to [ at++ ] = from [ b++ ];

			}

			void** swap = from;

			from = to;
			to = swap;

		}

		if ( from != items )
			copy ( items, from, sizeof ( void* ) * count );

		release ( spare );

		return true;

	}

}
