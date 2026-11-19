#include "app/shell/shell.hh"
#include "base/console/console.hh"

namespace app {

	static void flush_table ( shell_table& table ) {

		if ( !table.announced )
			return;

		uint32_t widths [ 256 ];
		uint32_t shown = table.width < 256 ? table.width : 256;

		for ( uint32_t c = 0; c < shown; c++ ) {

			widths [ c ] = static_cast < uint32_t > ( base::count_codes ( reinterpret_cast < const uint8_t* > ( table.names [ c ] ), base::length ( table.names [ c ] ) ) );

			for ( uint32_t r = 0; r < table.rows; r++ ) {

				const char* cell = table.cells [ r * table.width + c ];
				uint32_t size = static_cast < uint32_t > ( base::count_codes ( reinterpret_cast < const uint8_t* > ( cell ), base::length ( cell ) ) );

				widths [ c ] = base::max ( widths [ c ], size );

			}

			widths [ c ] = base::min < uint32_t > ( widths [ c ], SHELL_CELL_WIDTH );

		}

		auto cell_line = [ & ] ( base::text& line, const char* cell, uint32_t c, bool last ) {

			size_t size = base::length ( cell );
			size_t codes = base::count_codes ( reinterpret_cast < const uint8_t* > ( cell ), size );

			if ( codes > widths [ c ] ) {

				size_t cut = base::skip_codes ( reinterpret_cast < const uint8_t* > ( cell ), size, widths [ c ] - 1 );

				line.add ( cell, cut ).add ( "\xE2\x80\xA6" );
				codes = widths [ c ];

			} else
				line.add ( cell, size );

			if ( !last )
				line.repeat ( ' ', widths [ c ] - codes + 2 );

		};

		base::text line;

		for ( uint32_t c = 0; c < shown; c++ )
			cell_line ( line, table.names [ c ], c, c + 1 == shown );

		line.add ( '\n' );

		for ( uint32_t c = 0; c < shown; c++ )
			line.repeat ( '-', widths [ c ] ).add ( c + 1 == shown ? "" : "  " );

		line.add ( '\n' );
		base::write ( line );

		for ( uint32_t r = 0; r < table.rows; r++ ) {

			line.clear ( );

			for ( uint32_t c = 0; c < shown; c++ )
				cell_line ( line, table.cells [ r * table.width + c ], c, c + 1 == shown );

			line.add ( '\n' );
			base::write ( line );

		}

		line.clear ( );
		line.add ( '(' ).number ( table.rows ).add ( table.rows == 1 ? " row)\n" : " rows)\n" );
		base::write ( line );
		table.memory.reset ( );
		table.cells = nullptr;
		table.count = 0;
		table.room = 0;
		table.rows = 0;
		table.announced = false;

	}

	static bool table_columns ( void* context, const char* const* names, uint32_t count ) {

		shell_table& table = *static_cast < shell_table* > ( context );

		flush_table ( table );
		table.names = table.memory.make < const char* > ( count + 1 );
		table.width = count;
		table.announced = true;

		for ( uint32_t c = 0; c < count && table.names; c++ )
			table.names [ c ] = table.memory.keep ( names [ c ], base::length ( names [ c ] ) );

		return table.names != nullptr;

	}

	static bool table_row ( void* context, const storage::value* values, uint32_t count ) {

		shell_table& table = *static_cast < shell_table* > ( context );

		for ( uint32_t c = 0; c < count; c++ ) {

			base::text cell;

			engine::describe_value ( cell, values [ c ] );

			for ( size_t i = 0; i < cell.size; i++ )
				if ( cell.data [ i ] == '\n' || cell.data [ i ] == '\r' || cell.data [ i ] == '\t' )
					cell.data [ i ] = ' ';

			const char* kept = table.memory.keep ( cell.value ( ), cell.size );

			if ( !kept || !base::push ( table.memory, table.cells, table.count, table.room, kept ) )
				return false;

		}

		table.rows++;

		return true;

	}

	static bool run_text ( engine::database& db, const char* sql ) {

		shell_table table;

		table.names = nullptr;
		table.width = 0;
		table.cells = nullptr;
		table.count = 0;
		table.room = 0;
		table.rows = 0;
		table.announced = false;

		engine::query_sink sink = { &table, table_columns, table_row };
		bool fine = engine::execute ( db, sql, nullptr, 0, &sink );

		flush_table ( table );

		if ( !fine ) {

			base::text line;

			line.add ( "error: " );

			if ( db.parse_failed )
				line.add ( "line " ).number ( db.error_line ).add ( ", column " ).number ( db.error_column ).add ( ": " );

			line.add ( db.problem.value ( ) ).add ( '\n' );
			base::write ( line );

		}

		return fine;

	}

	static bool open ( engine::database*& db, const char* path ) {

		db = base::make < engine::database > ( 1 );

		if ( !db )
			return false;

		if ( engine::open_database ( *db, path ) )
			return true;

		base::text line;

		line.add ( "error: " ).add ( db->problem.value ( ) ).add ( '\n' );
		base::write ( line );

		return false;

	}

	static void close ( engine::database* db ) {

		if ( !db )
			return;

		engine::close_database ( *db );
		db->problem.~text ( );
		base::release ( db );

	}

	int32_t run_sql ( const char* path, const char* sql ) {

		engine::database* db = nullptr;

		base::use_utf8 ( );

		if ( !open ( db, path ) ) {

			close ( db );

			return 1;

		}

		bool fine = run_text ( *db, sql );

		close ( db );

		return fine ? 0 : 1;

	}

	static void dot_command ( engine::database& db, const char* line ) {

		base::text out;

		if ( base::starts_with ( line, ".tables" ) ) {

			for ( uint32_t i = 0; i < db.schema->table_count; i++ )
				out.add ( db.schema->tables [ i ].name ).add ( '\n' );

		} else if ( base::starts_with ( line, ".indexes" ) ) {

			for ( uint32_t i = 0; i < db.schema->index_count; i++ )
				out.add ( db.schema->indexes [ i ].name ).add ( "  on " ).add ( db.schema->indexes [ i ].table ).add ( '\n' );

		} else if ( base::starts_with ( line, ".schema" ) ) {

			const char* wanted = line + 7;

			while ( *wanted == ' ' )
				wanted++;

			for ( uint32_t i = 0; i < db.schema->table_count; i++ )
				if ( !*wanted || base::same_folded ( wanted, db.schema->tables [ i ].name ) )
					out.add ( db.schema->tables [ i ].sql ).add ( ";\n" );

			for ( uint32_t i = 0; i < db.schema->index_count; i++ )
				if ( !db.schema->indexes [ i ].automatic && ( !*wanted || base::same_folded ( wanted, db.schema->indexes [ i ].table ) ) )
					out.add ( db.schema->indexes [ i ].sql ).add ( ";\n" );

		} else {

			out.add ( ".tables          list the tables\n" );
			out.add ( ".indexes         list the indexes\n" );
			out.add ( ".schema [table]  show the CREATE statements\n" );
			out.add ( ".quit            leave\n" );

		}

		base::write ( out );

	}

	static bool complete ( const base::text& pending ) {

		bool quoted = false;
		char quote = 0;
		bool ended = false;

		for ( size_t i = 0; i < pending.size; i++ ) {

			char here = pending.data [ i ];

			if ( quoted ) {

				if ( here == quote )
					quoted = false;

				continue;

			}

			if ( here == '\'' || here == '"' ) {

				quoted = true;
				quote = here;
				ended = false;

			} else if ( here == ';' )
				ended = true;
			else if ( here != ' ' && here != '\t' && here != '\n' && here != '\r' )
				ended = false;

		}

		return ended && !quoted;

	}

	int32_t run_shell ( const char* path ) {

		engine::database* db = nullptr;

		base::use_utf8 ( );

		if ( !open ( db, path ) ) {

			close ( db );

			return 1;

		}

		base::write ( "strata " ENGINE_VERSION ", type .help for commands, end statements with ;\n" );

		base::text pending;
		base::text line;

		for ( ;; ) {

			base::write ( pending.size ? "   ...> " : "strata> " );

			if ( !base::read_line ( line ) )
				break;

			if ( !pending.size && line.size && line.data [ 0 ] == '.' ) {

				if ( base::starts_with ( line.value ( ), ".quit" ) || base::starts_with ( line.value ( ), ".exit" ) )
					break;

				dot_command ( *db, line.value ( ) );

				continue;

			}

			pending.add ( line.value ( ), line.size ).add ( '\n' );

			if ( !complete ( pending ) )
				continue;

			run_text ( *db, pending.value ( ) );
			pending.clear ( );

		}

		if ( pending.size )
			run_text ( *db, pending.value ( ) );

		close ( db );

		return 0;

	}

}
