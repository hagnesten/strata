#include "engine/check/check.hh"

namespace engine {

	void forget_report ( check_report& out ) {

		out.memory.reset ( );
		base::release ( out.bad );
		out.problems = nullptr;
		out.problem_count = 0;
		out.problem_room = 0;
		out.bad = nullptr;
		out.pages = 0;
		out.trees = 0;
		out.free_pages = 0;
		out.rows = 0;
		out.entries = 0;
		out.done = false;

	}

	static void problem ( check_report& out, const base::text& line ) {

		if ( out.problem_count == CHECK_PROBLEMS )
			return;

		const char* kept = out.memory.keep ( line.value ( ), line.size );

		if ( kept )
			base::push ( out.memory, out.problems, out.problem_count, out.problem_room, kept );

	}

	static void mark ( check_report& out, uint32_t page ) {

		if ( out.bad && page && page <= out.pages )
			out.bad [ page ] = 1;

	}

	static void claim_tree ( database& db, check_report& out, uint32_t* owners, uint32_t tree, const char* name, uint32_t root, const char* const* names ) {

		storage::page_summary* summary = base::make < storage::page_summary > ( 1 );
		uint32_t* queue = base::make < uint32_t > ( out.pages + 1 );

		if ( !summary || !queue ) {

			base::release ( summary );
			base::release ( queue );

			return;

		}

		uint32_t count = 0;

		if ( root && root <= out.pages )
			queue [ count++ ] = root;

		for ( uint32_t at = 0; at < count; at++ ) {

			uint32_t page = queue [ at ];

			if ( owners [ page ] ) {

				base::text line;

				line.add ( "page " ).number ( page ).add ( " is used by both " ).add ( names [ owners [ page ] - 1 ] ).add ( " and " ).add ( name );
				problem ( out, line );
				mark ( out, page );

				continue;

			}

			owners [ page ] = tree;

			if ( !storage::inspect_page ( db.pages, page, *summary ) ) {

				base::text line;

				line.add ( "page " ).number ( page ).add ( " of " ).add ( name ).add ( " could not be read" );
				problem ( out, line );
				mark ( out, page );

				continue;

			}

			for ( uint32_t c = 0; c < summary->child_count && count <= out.pages; c++ ) {

				uint32_t child = summary->children [ c ];

				if ( !child || child > out.pages ) {

					base::text line;

					line.add ( "page " ).number ( page ).add ( " of " ).add ( name ).add ( " points outside the file" );
					problem ( out, line );
					mark ( out, page );

					continue;

				}

				queue [ count++ ] = child;

			}

		}

		base::release ( summary );
		base::release ( queue );

	}

	static void check_tree ( database& db, check_report& out, const char* name, uint32_t root, bool index, uint64_t& entries, uint64_t& used ) {

		storage::tree_report report;

		entries = 0;
		used = 0;

		if ( !storage::check_btree ( db.pages, root, index, report ) || !report.fine ) {

			base::text line;

			line.add ( name ).add ( ": " ).add ( report.problem.size ? report.problem.value ( ) : db.pages.problem );
			problem ( out, line );
			mark ( out, root );

		}

		entries = report.entries;
		used = report.pages + report.overflow;

	}

	static void check_index_rows ( database& db, check_report& out, const index_info& index, const table_info& table ) {

		storage::btree tree = { &db.pages, index.root, true };
		storage::btree rows = { &db.pages, table.root, false };
		storage::cursor at;
		base::text entry;
		base::text payload;
		base::text previous;
		uint32_t reported = 0;
		bool first = true;

		if ( !storage::cursor_first ( at, tree ) )
			return;

		while ( at.valid && reported < 5 ) {

			if ( !storage::cursor_payload ( at, entry ) )
				break;

			storage::value key [ CATALOG_KEY_COLUMNS + 1 ];
			uint32_t count = 0;
			bool found = false;
			base::text line;

			if ( !storage::decode_record ( reinterpret_cast < const uint8_t* > ( entry.value ( ) ), entry.size, key, CATALOG_KEY_COLUMNS + 1, count ) || count != index.column_count + 1 || key [ count - 1 ].kind != storage::VALUE_INTEGER ) {

				line.add ( "an entry of index " ).add ( index.name ).add ( " is damaged" );
				problem ( out, line );
				reported++;

			} else if ( !storage::table_find ( rows, key [ count - 1 ].integer, payload, found ) || !found ) {

				line.add ( "index " ).add ( index.name ).add ( " points at row " ).signed_number ( key [ count - 1 ].integer ).add ( " of " ).add ( table.name ).add ( ", which is gone" );
				problem ( out, line );
				reported++;

			} else if ( index.unique && !first ) {

				bool nulls = false;

				for ( uint32_t k = 0; k < index.column_count; k++ )
					nulls = nulls || key [ k ].kind == storage::VALUE_NULL;

				if ( !nulls && storage::compare_records ( reinterpret_cast < const uint8_t* > ( previous.value ( ) ), previous.size, reinterpret_cast < const uint8_t* > ( entry.value ( ) ), entry.size, index.column_count ) == 0 ) {

					line.add ( "unique index " ).add ( index.name ).add ( " holds the same key twice" );
					problem ( out, line );
					reported++;

				}

			}

			previous.clear ( );
			previous.add ( entry.value ( ), entry.size );
			first = false;

			if ( !storage::cursor_next ( at ) )
				break;

		}

	}

	bool check_database ( database& db, check_report& out ) {

		forget_report ( out );

		if ( !db.open || !db.schema )
			return false;

		out.pages = db.pages.header.page_count;
		out.bad = base::make < uint8_t > ( out.pages + 1 );

		uint32_t* owners = base::make < uint32_t > ( out.pages + 1 );
		const char** names = base::make < const char* > ( db.schema->table_count + db.schema->index_count + 2 );

		if ( !out.bad || !owners || !names ) {

			base::release ( owners );
			base::release ( names );

			return false;

		}

		catalog& schema = *db.schema;
		uint64_t accounted = 1;
		uint32_t tree = 0;
		uint64_t entries = 0;
		uint64_t used = 0;

		names [ tree++ ] = "the schema";
		claim_tree ( db, out, owners, tree, "the schema", db.pages.header.schema_root, names );

		if ( db.pages.header.schema_root ) {

			check_tree ( db, out, "the schema", db.pages.header.schema_root, false, entries, used );
			accounted += used;

		}

		for ( uint32_t t = 0; t < schema.table_count; t++ ) {

			table_info& table = schema.tables [ t ];

			names [ tree++ ] = table.name;
			claim_tree ( db, out, owners, tree, table.name, table.root, names );
			check_tree ( db, out, table.name, table.root, false, entries, used );
			accounted += used;
			out.rows += entries;

			for ( uint32_t i = 0; i < schema.index_count; i++ ) {

				index_info& index = schema.indexes [ i ];

				if ( !base::same_folded ( index.table, table.name ) )
					continue;

				uint64_t index_entries = 0;

				names [ tree++ ] = index.name;
				claim_tree ( db, out, owners, tree, index.name, index.root, names );
				check_tree ( db, out, index.name, index.root, true, index_entries, used );
				accounted += used;
				out.entries += index_entries;

				if ( index.fulltext )
					continue;

				if ( index_entries != entries ) {

					base::text line;

					line.add ( "index " ).add ( index.name ).add ( " has " ).number ( index_entries ).add ( " entries but " ).add ( table.name ).add ( " has " ).number ( entries ).add ( " rows" );
					problem ( out, line );
					mark ( out, index.root );

				}

				check_index_rows ( db, out, index, table );

			}

		}

		uint32_t page = db.pages.header.free_head;
		uint32_t walked = 0;
		uint8_t* buffer = base::make < uint8_t > ( PAGE_SIZE );

		while ( buffer && page && walked <= out.pages ) {

			if ( page > out.pages ) {

				base::text line;

				line.add ( "the free list points at page " ).number ( page ).add ( ", outside the file" );
				problem ( out, line );

				break;

			}

			if ( owners [ page ] ) {

				base::text line;

				line.add ( "page " ).number ( page ).add ( " is on the free list but used by " ).add ( names [ owners [ page ] - 1 ] );
				problem ( out, line );
				mark ( out, page );

				break;

			}

			owners [ page ] = 0xFFFFFFFF;

			if ( !storage::read_page_bytes ( db.pages, page, buffer ) || buffer [ 0 ] != storage::PAGE_FREE ) {

				base::text line;

				line.add ( "page " ).number ( page ).add ( " is on the free list but is not a free page" );
				problem ( out, line );
				mark ( out, page );

				break;

			}

			walked++;
			page = base::read32 ( buffer + 4 );

		}

		base::release ( buffer );
		out.free_pages = walked;
		out.trees = tree;

		if ( walked != db.pages.header.free_count ) {

			base::text line;

			line.add ( "the header counts " ).number ( db.pages.header.free_count ).add ( " free pages but the free list holds " ).number ( walked );
			problem ( out, line );
			mark ( out, 1 );

		}

		accounted += walked;

		if ( accounted != out.pages ) {

			base::text line;

			if ( accounted < out.pages )
				line.number ( out.pages - accounted ).add ( out.pages - accounted == 1 ? " page belongs to nothing" : " pages belong to nothing" );
			else
				line.add ( "the trees claim " ).number ( accounted ).add ( " pages but the file has " ).number ( out.pages );

			problem ( out, line );

		}

		base::release ( owners );
		base::release ( names );
		out.done = true;

		return out.problem_count == 0;

	}

}