#include "engine/catalog/catalog.hh"

namespace engine {

	static bool next_count ( const storage::value& text, size_t& at, uint64_t& out ) {

		while ( at < text.size && text.bytes [ at ] == ' ' )
			at++;

		if ( at >= text.size || text.bytes [ at ] < '0' || text.bytes [ at ] > '9' )
			return false;

		out = 0;

		while ( at < text.size && text.bytes [ at ] >= '0' && text.bytes [ at ] <= '9' && out < 100000000000000000ull )
			out = out * 10 + ( text.bytes [ at++ ] - '0' );

		return true;

	}

	static bool same_text ( const storage::value& item, const char* name ) {

		return item.kind == storage::VALUE_TEXT && item.size == base::length ( name ) && base::same_bytes ( item.bytes, name, item.size );

	}

	static table_info* table_named ( catalog& out, const storage::value& name ) {

		for ( uint32_t t = 0; t < out.table_count; t++ )
			if ( same_text ( name, out.tables [ t ].name ) )
				return &out.tables [ t ];

		return nullptr;

	}

	static index_info* index_named ( catalog& out, const storage::value& name ) {

		for ( uint32_t i = 0; i < out.index_count; i++ )
			if ( same_text ( name, out.indexes [ i ].name ) )
				return &out.indexes [ i ];

		return nullptr;

	}

	bool load_statistics ( storage::pager& pages, catalog& out ) {

		table_info* stats = find_table ( out, STAT_TABLE );

		if ( !stats )
			return true;

		storage::btree tree = { &pages, stats->root, false };
		storage::cursor at;
		base::text payload;

		if ( !storage::cursor_first ( at, tree ) )
			return false;

		while ( at.valid ) {

			storage::value values [ STAT_COLUMNS ];
			uint32_t count = 0;

			if ( storage::cursor_payload ( at, payload ) && storage::decode_record ( reinterpret_cast < const uint8_t* > ( payload.value ( ) ), payload.size, values, STAT_COLUMNS, count ) && count == STAT_COLUMNS && values [ 2 ].kind == storage::VALUE_TEXT ) {

				table_info* table = table_named ( out, values [ 0 ] );
				size_t position = 0;
				uint64_t rows = 0;

				if ( table && next_count ( values [ 2 ], position, rows ) ) {

					table->stat_rows = rows;
					table->stat_known = true;

					index_info* index = values [ 1 ].kind == storage::VALUE_TEXT ? index_named ( out, values [ 1 ] ) : nullptr;

					if ( index && base::same_folded ( index->table, table->name ) ) {

						uint32_t k = 0;
						uint64_t average = 0;

						while ( k < index->column_count && next_count ( values [ 2 ], position, average ) )
							index->stat_average [ k++ ] = average ? average : 1;

						index->stat_known = k == index->column_count;

					}

				}

			}

			if ( !storage::cursor_next ( at ) )
				return false;

		}

		return true;

	}

	uint64_t table_rows ( storage::pager& pages, const table_info& table ) {

		if ( table.stat_known )
			return table.stat_rows;

		return storage::estimate_entries ( pages, table.root );

	}

}