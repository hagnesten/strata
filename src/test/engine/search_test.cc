#include "test/engine/checks.hh"

#define SEARCH_FUZZ_WORDS 12
#define SEARCH_FUZZ_DOCS 60
#define SEARCH_FUZZ_STEPS 400

namespace test {

	static void search_basics ( engine::database& db ) {

		run ( db, "CREATE TABLE articles ( id INTEGER PRIMARY KEY, title TEXT, body TEXT, extra INT )" );
		run ( db, "INSERT INTO articles ( title, body ) VALUES ( 'Strata storage', 'The pager writes pages through a write-ahead log.' ), ( 'Query planning', 'The planner picks indexes and join order using statistics.' ), ( 'Full text', 'Search words with an inverted index and rank results with BM25.' ), ( 'Storage again', 'Pages, pages and more pages; the log keeps pages safe.' ), ( NULL, 'Nothing to see here' )" );
		run ( db, "CREATE FULLTEXT INDEX articles_text ON articles ( title, body )" );
		rows ( db, "SELECT id FROM articles WHERE articles MATCH 'pages' ORDER BY id", "1\n4" );
		rows ( db, "SELECT id FROM articles WHERE articles MATCH 'PAGES log' ORDER BY id", "1\n4" );
		rows ( db, "SELECT id FROM articles WHERE articles MATCH 'pages OR planner' ORDER BY id", "1\n2\n4" );
		rows ( db, "SELECT id FROM articles WHERE articles MATCH 'pages -ahead' ORDER BY id", "4" );
		rows ( db, "SELECT id FROM articles WHERE articles MATCH 'pages NOT ahead' ORDER BY id", "4" );
		rows ( db, "SELECT id FROM articles WHERE articles MATCH 'stor*' ORDER BY id", "1\n4" );
		rows ( db, "SELECT id FROM articles WHERE articles MATCH '( planner OR search ) AND index*' ORDER BY id", "2\n3" );
		rows ( db, "SELECT id FROM articles WHERE articles MATCH '\"write ahead\"' ORDER BY id", "1" );
		rows ( db, "SELECT id FROM articles WHERE title MATCH 'storage' ORDER BY id", "1\n4" );
		rows ( db, "SELECT id FROM articles WHERE body MATCH 'storage' ORDER BY id", "" );
		rows ( db, "SELECT id FROM articles n WHERE n MATCH 'nothing'", "5" );
		rows ( db, "SELECT id FROM articles WHERE articles MATCH 'pages' ORDER BY bm25 ( articles ) LIMIT 1", "4" );
		rows ( db, "SELECT bm25 ( articles ) < 0 FROM articles WHERE articles MATCH 'pages' AND id = 1", "1" );
		rows ( db, "SELECT highlight ( articles, 1, '[', ']' ) FROM articles WHERE articles MATCH 'log OR pager' AND id = 1", "The [pager] writes pages through a write-ahead [log]." );
		rows ( db, "SELECT highlight ( articles, 0, '<', '>' ) FROM articles WHERE articles MATCH 'stor*' AND id = 4", "<Storage> again" );
		rows ( db, "SELECT snippet ( articles, 1, '<', '>', '...', 4 ) FROM articles WHERE articles MATCH 'statistics'", "...join order using <statistics>." );
		rows ( db, "SELECT count ( * ) FROM articles WHERE articles NOT MATCH 'pages'", "3" );
		rows ( db, "SELECT count ( * ) FROM articles WHERE articles MATCH 'pages' OR id = 5", "3" );
		rows ( db, "SELECT count ( * ) FROM articles WHERE articles MATCH ''", "0" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT id FROM articles WHERE articles MATCH 'pages'", "SEARCH articles USING FULLTEXT INDEX articles_text (MATCH)" );
		run ( db, "UPDATE articles SET body = 'no more storage words' WHERE id = 4" );
		rows ( db, "SELECT id FROM articles WHERE articles MATCH 'pages' ORDER BY id", "1" );
		rows ( db, "SELECT id FROM articles WHERE articles MATCH 'words' ORDER BY id", "3\n4" );
		run ( db, "DELETE FROM articles WHERE id = 1" );
		rows ( db, "SELECT id FROM articles WHERE articles MATCH 'pages'", "" );
		run ( db, "INSERT INTO articles ( title, body ) VALUES ( 'Caf\xC3\xA9', 'pages of the caf\xC3\xA9 menu' )" );
		rows ( db, "SELECT id FROM articles WHERE articles MATCH 'caf\xC3\xA9 pages'", "6" );
		fails ( db, "SELECT 1 FROM articles WHERE extra MATCH 'x'", "no FULLTEXT index on that column" );
		fails ( db, "SELECT bm25 ( articles ) FROM articles", "needs a MATCH" );
		fails ( db, "SELECT 1 FROM people WHERE people MATCH 'x'", "has no FULLTEXT index" );

		for ( uint32_t i = 0; i < db.schema->index_count; i++ ) {

			engine::index_info& index = db.schema->indexes [ i ];

			if ( !index.fulltext )
				continue;

			storage::tree_report report;

			expect ( storage::check_btree ( db.pages, index.root, true, report ), "the FULLTEXT index tree is intact" );

		}

		run ( db, "DROP INDEX articles_text" );
		fails ( db, "SELECT 1 FROM articles WHERE articles MATCH 'x'", "has no FULLTEXT index" );
		run ( db, "DROP TABLE articles" );

	}

	static uint32_t prefix_words ( const char* const* words, const char* prefix ) {

		uint32_t mask = 0;

		for ( uint32_t w = 0; w < SEARCH_FUZZ_WORDS; w++ )
			if ( base::starts_with ( words [ w ], prefix ) )
				mask |= 1u << w;

		return mask;

	}

	static void search_fuzz ( engine::database& db ) {

		static const char* const words [ SEARCH_FUZZ_WORDS ] = { "apple", "apricot", "banana", "berry", "cherry", "date", "elder", "fig", "grape", "guava", "kiwi", "lemon" };
		static const char* const prefixes [ 4 ] = { "ap", "b", "g", "ch" };
		uint32_t first_words [ SEARCH_FUZZ_DOCS ] = { };
		uint32_t second_words [ SEARCH_FUZZ_DOCS ] = { };
		bool present [ SEARCH_FUZZ_DOCS ] = { };
		test_random random = { 0x9B05688C2B3E6C1Full };
		base::text sql;
		uint32_t mismatches = 0;
		bool fine = true;

		run ( db, "CREATE TABLE fz ( id INTEGER PRIMARY KEY, a TEXT, b TEXT )" );
		run ( db, "CREATE FULLTEXT INDEX fz_words ON fz ( a, b )" );

		for ( uint32_t step = 0; step < SEARCH_FUZZ_STEPS && fine; step++ ) {

			uint32_t doc = random.below ( SEARCH_FUZZ_DOCS );
			uint32_t action = random.below ( 10 );

			sql.clear ( );

			if ( action < 6 ) {

				base::text sides [ 2 ];
				uint32_t masks [ 2 ] = { };
				uint32_t count = 1 + random.below ( 6 );

				for ( uint32_t k = 0; k < count; k++ ) {

					uint32_t w = random.below ( SEARCH_FUZZ_WORDS );
					uint32_t side = random.below ( 2 );

					if ( sides [ side ].size )
						sides [ side ].add ( random.below ( 2 ) ? " " : ", " );

					sides [ side ].add ( w == 0 && random.below ( 2 ) ? "APPLE" : words [ w ] );
					masks [ side ] |= 1u << w;

				}

				sql.add ( "INSERT OR REPLACE INTO fz VALUES ( " ).number ( doc + 1 ).add ( ", '" ).add ( sides [ 0 ].value ( ) ).add ( "', '" ).add ( sides [ 1 ].value ( ) ).add ( "' )" );
				first_words [ doc ] = masks [ 0 ];
				second_words [ doc ] = masks [ 1 ];
				present [ doc ] = true;

			} else if ( action < 8 ) {

				sql.add ( "DELETE FROM fz WHERE id = " ).number ( doc + 1 );
				present [ doc ] = false;

			} else {

				sql.add ( "UPDATE fz SET b = NULL WHERE id = " ).number ( doc + 1 );
				second_words [ doc ] = 0;

			}

			if ( !engine::execute ( db, sql.value ( ), nullptr, 0, nullptr ) ) {

				expect ( false, db.problem.value ( ) );
				fine = false;

				break;

			}

			uint32_t shape = random.below ( 5 );
			uint32_t x = random.below ( SEARCH_FUZZ_WORDS );
			uint32_t y = random.below ( SEARCH_FUZZ_WORDS );
			const char* prefix = prefixes [ random.below ( 4 ) ];
			base::text match;
			base::text wanted;

			if ( shape == 0 )
				match.add ( words [ x ] );
			else if ( shape == 1 )
				match.add ( words [ x ] ).add ( ' ' ).add ( words [ y ] );
			else if ( shape == 2 )
				match.add ( words [ x ] ).add ( " OR " ).add ( words [ y ] );
			else if ( shape == 3 )
				match.add ( words [ x ] ).add ( " -" ).add ( words [ y ] );
			else
				match.add ( prefix ).add ( '*' );

			uint32_t prefix_mask = prefix_words ( words, prefix );

			for ( uint32_t d = 0; d < SEARCH_FUZZ_DOCS; d++ ) {

				if ( !present [ d ] )
					continue;

				uint32_t all = first_words [ d ] | second_words [ d ];
				bool hx = all & 1u << x;
				bool hy = all & 1u << y;
				bool hit = shape == 0 ? hx : shape == 1 ? hx && hy : shape == 2 ? hx || hy : shape == 3 ? hx && !hy : ( all & prefix_mask ) != 0;

				if ( !hit )
					continue;

				if ( wanted.size )
					wanted.add ( '\n' );

				wanted.number ( d + 1 );

			}

			base::text fast;
			base::text slow;
			result_capture one;
			result_capture two;

			fast.add ( "SELECT id FROM fz WHERE fz MATCH '" ).add ( match.value ( ) ).add ( "' ORDER BY id" );
			slow.add ( "SELECT id FROM fz WHERE fz MATCH '" ).add ( match.value ( ) ).add ( "' OR id < 0 ORDER BY id" );

			if ( !query ( db, fast.value ( ), one ) || !query ( db, slow.value ( ), two ) ) {

				expect ( false, db.problem.value ( ) );
				fine = false;

				break;

			}

			if ( !base::same ( one.out.value ( ), wanted.value ( ) ) || !base::same ( two.out.value ( ), wanted.value ( ) ) ) {

				if ( mismatches++ < 3 ) {

					base::text line;

					line.add ( fast.value ( ) ).add ( "\n          index:  " ).add ( one.out.value ( ) ).add ( "\n          scan:   " ).add ( two.out.value ( ) ).add ( "\n          model:  " ).add ( wanted.value ( ) );
					expect ( false, line.value ( ) );

				}

			}

		}

		expect ( fine && !mismatches, "random FULLTEXT changes and searches agree with the model" );
		run ( db, "DROP TABLE fz" );

	}

	void search_tests ( engine::database& db ) {

		search_basics ( db );
		search_fuzz ( db );

	}

}