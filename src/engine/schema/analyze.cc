#include "engine/schema/schema.hh"

#define STAT_SQL "CREATE TABLE strata_stat ( tbl TEXT, idx TEXT, stat TEXT )"

namespace engine {

	static bool fresh_stat_tree ( run_context& ctx, uint32_t& root, int64_t& rowid ) {

		table_info* stats = find_table ( *ctx.schema, STAT_TABLE );

		rowid = stats ? stats->schema_rowid : 0;

		if ( stats && !storage::drop_btree ( *ctx.pages, stats->root ) )
			return fail_storage ( ctx, "the old statistics could not be cleared" );

		if ( !storage::create_btree ( *ctx.pages, false, root ) )
			return fail_storage ( ctx, "the statistics table could not be made" );

		if ( !write_schema_row ( *ctx.pages, "table", STAT_TABLE, STAT_TABLE, root, STAT_SQL, rowid ) )
			return fail_storage ( ctx, "the statistics table could not be recorded" );

		return true;

	}

	static bool count_rows ( run_context& ctx, uint32_t root, uint64_t& rows ) {

		storage::btree tree = { ctx.pages, root, false };
		storage::cursor at;

		rows = 0;

		if ( !storage::cursor_first ( at, tree ) )
			return fail_storage ( ctx, "a table could not be read" );

		while ( at.valid ) {

			rows++;

			if ( !storage::cursor_next ( at ) )
				return fail_storage ( ctx, "a table could not be read" );

		}

		return true;

	}

	static bool measure_index ( run_context& ctx, const index_info& index, uint64_t* distinct, uint64_t& entries ) {

		storage::btree tree = { ctx.pages, index.root, true };
		storage::cursor at;
		base::text previous;
		bool first = true;

		entries = 0;

		for ( uint32_t k = 0; k < index.column_count; k++ )
			distinct [ k ] = 0;

		if ( !storage::cursor_first ( at, tree ) )
			return fail_storage ( ctx, "an index could not be read" );

		while ( at.valid ) {

			if ( !storage::cursor_payload ( at, ctx.row_bytes ) )
				return fail_storage ( ctx, "an index could not be read" );

			const uint8_t* entry = reinterpret_cast < const uint8_t* > ( ctx.row_bytes.value ( ) );

			for ( uint32_t k = 0; k < index.column_count; k++ ) {

				if ( first || storage::compare_records ( reinterpret_cast < const uint8_t* > ( previous.value ( ) ), previous.size, entry, ctx.row_bytes.size, k + 1 ) != 0 ) {

					for ( uint32_t rest = k; rest < index.column_count; rest++ )
						distinct [ rest ]++;

					break;

				}

			}

			previous.clear ( );
			previous.add ( ctx.row_bytes.value ( ), ctx.row_bytes.size );
			first = false;
			entries++;

			if ( !storage::cursor_next ( at ) )
				return fail_storage ( ctx, "an index could not be read" );

		}

		return true;

	}

	static bool add_stat ( run_context& ctx, uint32_t root, int64_t rowid, const char* table, const char* index, const char* stat ) {

		storage::btree tree = { ctx.pages, root, false };
		storage::value values [ STAT_COLUMNS ];
		base::text record;
		bool existed = false;

		values [ 0 ] = storage::text_value ( table, base::length ( table ) );
		values [ 1 ] = index ? storage::text_value ( index, base::length ( index ) ) : storage::null_value ( );
		values [ 2 ] = storage::text_value ( stat, base::length ( stat ) );
		storage::encode_record ( values, STAT_COLUMNS, record );

		if ( !storage::table_insert ( tree, rowid, reinterpret_cast < const uint8_t* > ( record.value ( ) ), record.size, false, existed ) )
			return fail_storage ( ctx, "the statistics could not be written" );

		return true;

	}

	bool run_analyze ( run_context& ctx ) {

		uint32_t root = 0;
		int64_t schema_rowid = 0;
		int64_t next = 1;

		if ( !fresh_stat_tree ( ctx, root, schema_rowid ) )
			return false;

		for ( uint32_t t = 0; t < ctx.schema->table_count; t++ ) {

			const table_info& table = ctx.schema->tables [ t ];
			uint64_t rows = 0;
			base::text stat;

			if ( base::same_folded ( table.name, STAT_TABLE ) )
				continue;

			if ( !count_rows ( ctx, table.root, rows ) )
				return false;

			stat.number ( rows );

			if ( !add_stat ( ctx, root, next++, table.name, nullptr, stat.value ( ) ) )
				return false;

			for ( uint32_t i = 0; i < ctx.schema->index_count; i++ ) {

				const index_info& index = ctx.schema->indexes [ i ];
				uint64_t distinct [ CATALOG_KEY_COLUMNS ];
				uint64_t entries = 0;

				if ( !base::same_folded ( index.table, table.name ) )
					continue;

				if ( !measure_index ( ctx, index, distinct, entries ) )
					return false;

				stat.clear ( );
				stat.number ( entries );

				for ( uint32_t k = 0; k < index.column_count; k++ )
					stat.add ( ' ' ).number ( distinct [ k ] ? ( entries + distinct [ k ] - 1 ) / distinct [ k ] : 1 );

				if ( !add_stat ( ctx, root, next++, table.name, index.name, stat.value ( ) ) )
					return false;

			}

		}

		return refresh_schema ( ctx );

	}

}