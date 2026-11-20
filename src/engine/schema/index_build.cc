#include "engine/schema/schema.hh"

#define SORT_RUN 16

namespace engine {

	static void head_of ( const storage::value& item, sort_key& out ) {

		out.head = 0;
		out.exact = true;
		out.kind = static_cast < uint8_t > ( item.kind );

		switch ( item.kind ) {

			case storage::VALUE_NULL:
				out.rank = 0;
				break;

			case storage::VALUE_INTEGER:
				out.rank = 1;
				out.head = static_cast < uint64_t > ( item.integer ) ^ 0x8000000000000000ull;
				break;

			case storage::VALUE_REAL: {

				uint64_t bits = 0;

				if ( item.real != 0 )
					base::copy ( &bits, &item.real, 8 );

				out.rank = 1;
				out.head = bits >> 63 ? ~bits : bits | 0x8000000000000000ull;
				break;

			}

			default: {

				size_t take = item.size < 8 ? item.size : 8;

				out.rank = item.kind == storage::VALUE_TEXT ? 2 : 3;
				out.exact = item.size < 8;

				for ( size_t i = 0; i < 8; i++ ) {

					uint8_t byte = i < take ? item.bytes [ i ] : 0;

					out.exact = out.exact && ( i >= take || byte );
					out.head = out.head << 8 | byte;

				}

				break;

			}

		}

	}

	static inline int32_t compare_keys ( const sort_key& a, const sort_key& b, bool single ) {

		if ( a.rank != b.rank )
			return a.rank < b.rank ? -1 : 1;

		if ( a.kind == b.kind ) {

			if ( a.head != b.head )
				return a.head < b.head ? -1 : 1;

			if ( single && a.exact && b.exact )
				return a.rowid == b.rowid ? 0 : a.rowid < b.rowid ? -1 : 1;

		}

		return storage::compare_records ( a.data, a.size, b.data, b.size, 0 );

	}

	static void sort_keys ( sort_key* items, sort_key* spare, uint32_t count, bool single ) {

		for ( uint32_t start = 0; start < count; start += SORT_RUN ) {

			uint32_t end = start + SORT_RUN < count ? start + SORT_RUN : count;

			for ( uint32_t i = start + 1; i < end; i++ ) {

				sort_key held = items [ i ];
				uint32_t k = i;

				while ( k > start && compare_keys ( held, items [ k - 1 ], single ) < 0 ) {

					items [ k ] = items [ k - 1 ];
					k--;

				}

				items [ k ] = held;

			}

		}

		sort_key* from = items;
		sort_key* to = spare;

		for ( uint32_t width = SORT_RUN; width < count; width *= 2 ) {

			for ( uint32_t start = 0; start < count; start += width * 2 ) {

				uint32_t middle = start + width < count ? start + width : count;
				uint32_t end = start + width * 2 < count ? start + width * 2 : count;
				uint32_t left = start;
				uint32_t right = middle;
				uint32_t at = start;

				while ( left < middle && right < end )
					to [ at++ ] = compare_keys ( from [ right ], from [ left ], single ) < 0 ? from [ right++ ] : from [ left++ ];

				while ( left < middle )
					to [ at++ ] = from [ left++ ];

				while ( right < end )
					to [ at++ ] = from [ right++ ];

			}

			sort_key* held = from;

			from = to;
			to = held;

		}

		if ( from != items )
			base::copy ( items, from, sizeof ( sort_key ) * count );

	}

	static bool grow_keys ( sort_key*& items, uint32_t count, uint32_t& room ) {

		if ( count < room )
			return true;

		uint32_t next = room ? room * 2 : 4096;
		sort_key* made = base::make < sort_key > ( next );

		if ( !made )
			return false;

		if ( items )
			base::copy ( made, items, sizeof ( sort_key ) * count );

		base::release ( items );
		items = made;
		room = next;

		return true;

	}

	static bool gather_keys ( run_context& ctx, table_info& table, index_info& index, sort_key*& items, uint32_t& count, uint32_t& room ) {

		storage::btree rows = { ctx.pages, table.root, false };
		storage::cursor at;
		bool* wanted = ctx.memory->make < bool > ( table.column_count + 1 );
		uint32_t limit = 0;

		if ( !wanted )
			return fail_run ( ctx, "out of memory" );

		for ( uint32_t k = 0; k < index.column_count; k++ ) {

			wanted [ index.columns [ k ] ] = true;
			limit = base::max < uint32_t > ( limit, index.columns [ k ] + 1 );

		}

		if ( !storage::cursor_first ( at, rows ) )
			return fail_storage ( ctx, "the table could not be read" );

		storage::value* values = ctx.memory->make < storage::value > ( table.column_count + 1 );
		storage::value key [ CATALOG_KEY_COLUMNS + 1 ];

		if ( !values )
			return fail_run ( ctx, "out of memory" );

		while ( at.valid ) {

			base::arena_mark mark = ctx.scratch->mark ( );
			int64_t rowid = 0;
			const uint8_t* data = nullptr;
			size_t size = 0;
			uint32_t decoded = 0;

			if ( !storage::cursor_take ( at, *ctx.scratch, rowid, data, size ) )
				return fail_storage ( ctx, "the table could not be read" );

			if ( table.generated_count ) {

				if ( !decode_row ( ctx, table, data, size, rowid, values ) )
					return false;

			} else {

				if ( !storage::decode_wanted ( data, size, values, table.column_count, wanted, limit, decoded ) )
					return fail_named ( ctx, "a row of table ", table.name, " is damaged" );

				for ( uint32_t k = 0; k < index.column_count; k++ )
					if ( index.columns [ k ] >= decoded && !fallback_value ( ctx, table.columns [ index.columns [ k ] ], values [ index.columns [ k ] ] ) )
						return false;

				if ( table.rowid_column >= 0 )
					values [ table.rowid_column ] = storage::integer_value ( rowid );

			}

			bool nulls = false;

			for ( uint32_t k = 0; k < index.column_count; k++ ) {

				key [ k ] = values [ index.columns [ k ] ];
				nulls = nulls || key [ k ].kind == storage::VALUE_NULL;

			}

			key [ index.column_count ] = storage::integer_value ( rowid );

			size_t bytes = storage::record_size ( key, index.column_count + 1 );
			uint8_t* kept = static_cast < uint8_t* > ( ctx.memory->take_raw ( bytes + 1 ) );

			if ( !kept || !grow_keys ( items, count, room ) )
				return fail_run ( ctx, "out of memory" );

			storage::write_record ( key, index.column_count + 1, kept );

			sort_key& made = items [ count++ ];

			head_of ( key [ 0 ], made );
			made.rowid = rowid;
			made.data = kept;
			made.size = static_cast < uint32_t > ( bytes );
			made.nulls = nulls;
			ctx.scratch->rewind ( mark );

		}

		return true;

	}

	bool fill_index ( run_context& ctx, table_info& table, index_info& index, storage::btree& tree ) {

		sort_key* items = nullptr;
		uint32_t count = 0;
		uint32_t room = 0;
		bool fine = gather_keys ( ctx, table, index, items, count, room );
		sort_key* spare = fine && count ? base::make < sort_key > ( count ) : nullptr;
		storage::build_key* keys = fine && count ? base::make < storage::build_key > ( count ) : nullptr;

		if ( fine && count && ( !spare || !keys ) )
			fine = fail_run ( ctx, "out of memory" );

		if ( fine && count )
			sort_keys ( items, spare, count, index.column_count == 1 );

		for ( uint32_t i = 1; fine && index.unique && i < count; i++ ) {

			if ( items [ i ].nulls || items [ i - 1 ].nulls )
				continue;

			if ( storage::compare_records ( items [ i - 1 ].data, items [ i - 1 ].size, items [ i ].data, items [ i ].size, index.column_count ) == 0 ) {

				base::text message;

				message.add ( "UNIQUE constraint failed: table " ).add ( table.name ).add ( " already holds duplicates for the index " ).add ( index.name );
				fine = fail_run ( ctx, message.value ( ) );

			}

		}

		for ( uint32_t i = 0; fine && i < count; i++ )
			keys [ i ] = { items [ i ].data, items [ i ].size };

		if ( fine && !storage::build_index ( tree, keys, count ) )
			fine = fail_storage ( ctx, "the index could not be written" );

		base::release ( items );
		base::release ( spare );
		base::release ( keys );

		return fine;

	}

}
