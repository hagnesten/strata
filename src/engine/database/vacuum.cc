#include "engine/database/database.hh"

namespace engine {

	static void erase_database_files ( const char* path ) {

		base::text log;
		base::text index;

		base::erase_path ( path );
		log.add ( path ).add ( "-wal" );
		index.add ( path ).add ( "-shm" );
		base::erase_path ( log.value ( ) );
		base::erase_path ( index.value ( ) );

	}

	static bool copy_schema ( storage::pager& from, storage::pager& to, base::text& problem ) {

		if ( !from.header.schema_root )
			return true;

		storage::btree tree = { &from, from.header.schema_root, false };
		storage::cursor at;
		base::text payload;

		if ( !storage::cursor_first ( at, tree ) ) {

			problem.add ( "the schema could not be read" );

			return false;

		}

		while ( at.valid ) {

			storage::value values [ SCHEMA_COLUMNS ];
			uint32_t count = 0;
			int64_t rowid = 0;

			if ( !storage::cursor_rowid ( at, rowid ) || !storage::cursor_payload ( at, payload ) || !storage::decode_record ( reinterpret_cast < const uint8_t* > ( payload.value ( ) ), payload.size, values, SCHEMA_COLUMNS, count ) || count != SCHEMA_COLUMNS || values [ 3 ].kind != storage::VALUE_INTEGER ) {

				problem.add ( "a schema row is damaged" );

				return false;

			}

			base::text kind;
			base::text name;
			base::text table;
			base::text sql;
			uint32_t root = 0;

			kind.add ( reinterpret_cast < const char* > ( values [ 0 ].bytes ), values [ 0 ].size );
			name.add ( reinterpret_cast < const char* > ( values [ 1 ].bytes ), values [ 1 ].size );
			table.add ( reinterpret_cast < const char* > ( values [ 2 ].bytes ), values [ 2 ].size );
			sql.add ( reinterpret_cast < const char* > ( values [ 4 ].bytes ), values [ 4 ].size );

			bool index = base::same ( kind.value ( ), "index" );

			if ( values [ 3 ].integer && !storage::copy_btree ( from, static_cast < uint32_t > ( values [ 3 ].integer ), index, to, root ) ) {

				problem.add ( "the contents of " ).add ( name.value ( ) ).add ( " could not be copied: " ).add ( from.problem [ 0 ] ? from.problem : to.problem );

				return false;

			}

			if ( !write_schema_row ( to, kind.value ( ), name.value ( ), table.value ( ), root, sql.value ( ), rowid ) ) {

				problem.add ( "the schema could not be copied: " ).add ( to.problem );

				return false;

			}

			if ( !storage::cursor_next ( at ) ) {

				problem.add ( "the schema could not be read" );

				return false;

			}

		}

		return true;

	}

	static bool write_copy ( database& item, const char* path, base::text& problem ) {

		storage::pager* fresh = base::make < storage::pager > ( 1 );

		if ( !fresh ) {

			problem.add ( "out of memory" );

			return false;

		}

		erase_database_files ( path );

		bool fine = storage::open_pager ( *fresh, path );

		if ( !fine )
			problem.add ( "the copy could not be made: " ).add ( fresh->problem );

		fine = fine && copy_schema ( item.pages, *fresh, problem );

		if ( fine ) {

			fresh->header.schema_version = item.pages.header.schema_version + 1;
			fresh->header.change = item.pages.header.change;

			if ( !storage::commit_pager ( *fresh ) || !storage::checkpoint ( *fresh ) ) {

				problem.add ( "the copy could not be saved: " ).add ( fresh->problem );
				fine = false;

			}

		}

		storage::close_pager ( *fresh );
		base::release ( fresh );

		base::text log;
		base::text index;

		log.add ( path ).add ( "-wal" );
		index.add ( path ).add ( "-shm" );
		base::erase_path ( log.value ( ) );
		base::erase_path ( index.value ( ) );

		if ( !fine )
			base::erase_path ( path );

		return fine;

	}

	bool run_vacuum ( database& item, run_context& ctx, const sql::statement* statement ) {

		if ( item.in_transaction )
			return fail_run ( ctx, "VACUUM cannot run inside a transaction" );

		if ( !statement->into && !storage::pager_alone ( item.pages ) )
			return fail_run ( ctx, "VACUUM needs every other connection to this database closed first" );

		base::text target;

		if ( statement->into ) {

			storage::value name;

			if ( !bind_expression ( ctx, nullptr, statement->into, BIND_CONSTANT ) || !evaluate ( ctx, nullptr, statement->into, name ) )
				return false;

			if ( name.kind != storage::VALUE_TEXT || !name.size )
				return fail_run ( ctx, "VACUUM INTO needs a file name" );

			target.add ( reinterpret_cast < const char* > ( name.bytes ), name.size );

			if ( base::path_exists ( target.value ( ) ) )
				return fail_named ( ctx, "the file ", target.value ( ), " already exists" );

		} else
			target.add ( item.pages.path ).add ( "-vacuum" );

		if ( !statement->into && !storage::checkpoint ( item.pages ) )
			return pager_problem ( item, "the log could not be folded into the database" );

		base::text problem;

		if ( !write_copy ( item, target.value ( ), problem ) )
			return fail_run ( ctx, problem.value ( ) );

		if ( statement->into )
			return true;

		char path [ FILE_PATH_ROOM ];

		base::copy_text ( path, sizeof ( path ), item.pages.path );
		close_database ( item );

		bool swapped = base::replace_path ( target.value ( ), path );

		if ( swapped ) {

			base::text log;

			log.add ( path ).add ( "-wal" );
			base::erase_path ( log.value ( ) );

		} else
			base::erase_path ( target.value ( ) );

		if ( !open_database ( item, path ) )
			return false;

		if ( !swapped )
			return fail_run ( ctx, "the compacted copy could not replace the database file" );

		return true;

	}

}