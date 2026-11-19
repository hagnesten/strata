#include "test/harness/harness.hh"
#include "storage/btree/btree.hh"

namespace test {

	static void fill_payload ( base::text& out, uint64_t seed, uint32_t size ) {

		out.clear ( );

		for ( uint32_t i = 0; i < size; i++ )
			out.add ( static_cast < char > ( 'a' + ( seed * 31 + i * 7 ) % 26 ) );

	}

	static bool check_tree ( storage::pager& item, uint32_t root, bool index, uint64_t entries, const char* when ) {

		storage::tree_report report;
		bool fine = storage::check_btree ( item, root, index, report );

		if ( !fine ) {

			base::text line;

			line.add ( when ).add ( ": " ).add ( report.problem.value ( ) );
			expect ( false, line.value ( ) );

			return false;

		}

		if ( report.entries != entries ) {

			base::text line;

			line.add ( when ).add ( ": the tree holds " ).number ( report.entries ).add ( " entries, the model " ).number ( entries );
			expect ( false, line.value ( ) );

			return false;

		}

		return true;

	}

	static void table_model_test ( const char* path ) {

		static const uint32_t keys = 20000;
		static const uint32_t steps = 300000;
		uint32_t* sizes = base::make < uint32_t > ( keys );
		uint64_t* seeds = base::make < uint64_t > ( keys );
		test_random random = { 0x9E3779B97F4A7C15ull };
		storage::pager item;
		uint32_t root = 0;
		uint64_t live = 0;
		bool fine = sizes && seeds && storage::open_pager ( item, path ) && storage::create_btree ( item, false, root );
		base::text payload;
		base::text found;

		expect ( fine, "a table tree is made" );

		storage::btree tree = { &item, root, false };

		for ( uint32_t step = 0; step < steps && fine; step++ ) {

			uint32_t key = random.below ( keys );
			uint32_t action = random.below ( 10 );

			if ( action < 5 ) {

				uint32_t size = random.below ( 8 ) == 0 ? 1000 + random.below ( 5000 ) : random.below ( 300 );
				uint64_t seed = random.next ( );
				bool existed = false;

				fill_payload ( payload, seed, size );
				fine = storage::table_insert ( tree, key, reinterpret_cast < const uint8_t* > ( payload.value ( ) ), payload.size, true, existed );
				fine = fine && expect ( existed == ( sizes [ key ] != 0 ), "insert knows whether the row existed" );

				if ( !sizes [ key ] )
					live++;

				sizes [ key ] = size + 1;
				seeds [ key ] = seed;

			} else if ( action < 8 ) {

				bool gone = false;

				fine = storage::table_delete ( tree, key, gone );
				fine = fine && expect ( gone == ( sizes [ key ] != 0 ), "delete knows whether the row existed" );

				if ( sizes [ key ] )
					live--;

				sizes [ key ] = 0;

			} else {

				bool present = false;

				fine = storage::table_find ( tree, key, found, present );
				fine = fine && expect ( present == ( sizes [ key ] != 0 ), "a lookup agrees with the model" );

				if ( fine && present ) {

					fill_payload ( payload, seeds [ key ], sizes [ key ] - 1 );
					fine = expect ( found.size == payload.size && base::same_bytes ( found.value ( ), payload.value ( ), payload.size ), "a row reads back exactly" );

				}

			}

			if ( fine && step % 25000 == 24999 )
				fine = check_tree ( item, root, false, live, "during random changes" );

			if ( fine && step % 50000 == 49999 )
				fine = expect ( storage::commit_pager ( item ), "a commit in the middle succeeds" );

		}

		if ( !fine && item.problem [ 0 ] )
			expect ( false, item.problem );

		expect ( fine, "300000 random changes match the model" );
		fine = fine && check_tree ( item, root, false, live, "after random changes" );

		storage::cursor at;
		uint32_t expected = 0;
		bool ordered = fine && storage::cursor_first ( at, tree );

		while ( ordered && at.valid ) {

			int64_t rowid = 0;

			while ( expected < keys && !sizes [ expected ] )
				expected++;

			ordered = storage::cursor_rowid ( at, rowid ) && rowid == expected && storage::cursor_payload ( at, found ) && found.size == sizes [ expected ] - 1;
			expected++;
			ordered = ordered && storage::cursor_next ( at );

		}

		while ( expected < keys && !sizes [ expected ] )
			expected++;

		expect ( ordered && expected == keys, "a full scan visits every row in order" );

		int64_t last = 0;
		bool empty = true;
		int64_t highest = -1;

		for ( uint32_t i = 0; i < keys; i++ )
			if ( sizes [ i ] )
				highest = i;

		expect ( storage::table_last ( tree, last, empty ) && !empty && last == highest, "the last row id is found" );
		expect ( storage::commit_pager ( item ), "the final state commits" );
		storage::close_pager ( item );

		fine = storage::open_pager ( item, path );
		tree.owner = &item;
		expect ( fine && check_tree ( item, root, false, live, "after reopening" ), "the tree survives reopening" );

		bool all = fine;

		for ( uint32_t key = 0; key < keys && all; key += 7 ) {

			bool present = false;

			all = storage::table_find ( tree, key, found, present ) && present == ( sizes [ key ] != 0 );

			if ( all && present ) {

				fill_payload ( payload, seeds [ key ], sizes [ key ] - 1 );
				all = found.size == payload.size && base::same_bytes ( found.value ( ), payload.value ( ), payload.size );

			}

		}

		expect ( all, "rows read back after reopening" );

		bool cleared = true;

		for ( uint32_t key = 0; key < keys && cleared; key++ ) {

			bool gone = false;

			cleared = storage::table_delete ( tree, key, gone );

			if ( !cleared ) {

				base::text line;

				line.add ( "deleting row " ).number ( key ).add ( " failed: " ).add ( item.problem );
				expect ( false, line.value ( ) );

			}

		}

		expect ( cleared && check_tree ( item, root, false, 0, "after deleting everything" ), "deleting every row leaves a valid empty tree" );

		storage::tree_report report;

		storage::check_btree ( item, root, false, report );
		expect ( report.pages == 1, "an empty tree shrinks back to its root page" );
		expect ( item.header.free_count + 2 == item.header.page_count, "every other page went back to the free list" );
		storage::close_pager ( item );
		base::release ( sizes );
		base::release ( seeds );

	}

	static bool tree_matches ( storage::btree& tree, const uint32_t* sizes, const uint64_t* seeds, uint32_t keys ) {

		storage::cursor at;
		base::text found;
		base::text payload;
		uint32_t expected = 0;

		if ( !storage::cursor_first ( at, tree ) )
			return false;

		while ( at.valid ) {

			int64_t rowid = 0;

			while ( expected < keys && !sizes [ expected ] )
				expected++;

			if ( !storage::cursor_rowid ( at, rowid ) || rowid != expected || !storage::cursor_payload ( at, found ) )
				return false;

			fill_payload ( payload, seeds [ expected ], sizes [ expected ] - 1 );

			if ( found.size != payload.size || !base::same_bytes ( found.value ( ), payload.value ( ), payload.size ) )
				return false;

			expected++;

			if ( !storage::cursor_next ( at ) )
				return false;

		}

		while ( expected < keys && !sizes [ expected ] )
			expected++;

		return expected == keys;

	}

	static void statement_undo_test ( const char* path ) {

		static const uint32_t keys = 3000;
		static const uint32_t statements = 3000;
		uint32_t* sizes = base::make < uint32_t > ( keys * 3 );
		uint64_t* seeds = base::make < uint64_t > ( keys * 3 );
		test_random random = { 0x2545F4914F6CDD1Dull };
		storage::pager item;
		uint32_t root = 0;
		bool fine = sizes && seeds && storage::open_pager ( item, path, 64 ) && storage::create_btree ( item, false, root ) && storage::commit_pager ( item );
		storage::btree tree = { &item, root, false };
		base::text payload;
		uint32_t undone = 0;
		uint32_t* statement_sizes = sizes + keys;
		uint32_t* committed_sizes = sizes + keys * 2;
		uint64_t* statement_seeds = seeds + keys;
		uint64_t* committed_seeds = seeds + keys * 2;

		for ( uint32_t round = 0; round < statements && fine; round++ ) {

			base::copy ( statement_sizes, sizes, sizeof ( uint32_t ) * keys );
			base::copy ( statement_seeds, seeds, sizeof ( uint64_t ) * keys );
			storage::begin_statement ( item );

			uint32_t changes = 1 + random.below ( 40 );

			for ( uint32_t i = 0; i < changes && fine; i++ ) {

				uint32_t key = random.below ( keys );

				if ( random.below ( 3 ) ) {

					uint32_t size = random.below ( 10 ) == 0 ? 1000 + random.below ( 4000 ) : random.below ( 200 );
					uint64_t seed = random.next ( );
					bool existed = false;

					fill_payload ( payload, seed, size );
					fine = storage::table_insert ( tree, key, reinterpret_cast < const uint8_t* > ( payload.value ( ) ), payload.size, true, existed );
					sizes [ key ] = size + 1;
					seeds [ key ] = seed;

				} else {

					bool gone = false;

					fine = storage::table_delete ( tree, key, gone );
					sizes [ key ] = 0;

				}

			}

			if ( random.below ( 3 ) == 0 ) {

				storage::undo_statement ( item );
				base::copy ( sizes, statement_sizes, sizeof ( uint32_t ) * keys );
				base::copy ( seeds, statement_seeds, sizeof ( uint64_t ) * keys );
				undone++;

			} else
				storage::end_statement ( item );

			if ( fine && round % 100 == 99 ) {

				if ( random.below ( 4 ) == 0 ) {

					storage::rollback_pager ( item );
					base::copy ( sizes, committed_sizes, sizeof ( uint32_t ) * keys );
					base::copy ( seeds, committed_seeds, sizeof ( uint64_t ) * keys );

				} else {

					fine = storage::commit_pager ( item );
					base::copy ( committed_sizes, sizes, sizeof ( uint32_t ) * keys );
					base::copy ( committed_seeds, seeds, sizeof ( uint64_t ) * keys );

				}

			}

			if ( fine && round % 250 == 249 ) {

				uint64_t live = 0;

				for ( uint32_t k = 0; k < keys; k++ )
					live += sizes [ k ] != 0;

				fine = check_tree ( item, root, false, live, "after undoing statements" ) && expect ( tree_matches ( tree, sizes, seeds, keys ), "the tree matches the model after undoing statements" );

			}

		}

		if ( !fine && item.problem [ 0 ] )
			expect ( false, item.problem );

		expect ( fine && undone > 500, "3000 statements, a third of them undone, match the model" );
		storage::close_pager ( item );
		base::release ( sizes );
		base::release ( seeds );

	}

	static void make_key ( base::text& out, uint32_t number, uint32_t rowid ) {

		base::text name;
		storage::value values [ 2 ];

		name.add ( "user-" ).number ( number % 997 ).add ( '-' ).number ( number );

		if ( number % 50 == 0 )
			name.repeat ( 'x', 1500 );

		values [ 0 ] = storage::text_value ( name.value ( ), name.size );
		values [ 1 ] = storage::integer_value ( rowid );
		out.clear ( );
		storage::encode_record ( values, 2, out );

	}

	static void index_test ( const char* path ) {

		static const uint32_t keys = 15000;
		storage::pager item;
		uint32_t root = 0;
		bool fine = storage::open_pager ( item, path ) && storage::create_btree ( item, true, root );
		storage::btree tree = { &item, root, true };
		base::text key;
		uint64_t live = 0;

		for ( uint32_t i = 0; i < keys && fine; i++ ) {

			bool existed = false;

			make_key ( key, i, i );
			fine = storage::index_insert ( tree, reinterpret_cast < const uint8_t* > ( key.value ( ) ), key.size, existed ) && !existed;
			live++;

		}

		expect ( fine, "15000 index keys go in, some of them long" );
		fine = fine && check_tree ( item, root, true, live, "after filling the index" );

		bool again = false;

		make_key ( key, 42, 42 );
		expect ( storage::index_insert ( tree, reinterpret_cast < const uint8_t* > ( key.value ( ) ), key.size, again ) && again, "a duplicate key is noticed" );

		for ( uint32_t i = 0; i < keys && fine; i += 3 ) {

			bool gone = false;

			make_key ( key, i, i );
			fine = storage::index_delete ( tree, reinterpret_cast < const uint8_t* > ( key.value ( ) ), key.size, gone ) && gone;
			live--;

		}

		expect ( fine, "every third key comes out" );
		fine = fine && check_tree ( item, root, true, live, "after removing keys" );

		storage::value prefix_values [ 1 ];
		base::text prefix;
		const char* wanted = "user-5-";

		prefix_values [ 0 ] = storage::text_value ( "user-5-", 7 );
		storage::encode_record ( prefix_values, 1, prefix );

		storage::cursor at;
		uint32_t seen = 0;
		bool in_order = fine && storage::cursor_seek_key ( at, tree, reinterpret_cast < const uint8_t* > ( prefix.value ( ) ), prefix.size, 1 );
		base::text payload;

		while ( in_order && at.valid && seen < 5 ) {

			storage::value values [ 4 ];
			uint32_t count = 0;

			in_order = storage::cursor_payload ( at, payload ) && storage::decode_record ( reinterpret_cast < const uint8_t* > ( payload.value ( ) ), payload.size, values, 4, count ) && count == 2 && values [ 0 ].kind == storage::VALUE_TEXT;

			if ( in_order && seen == 0 )
				in_order = storage::compare_values ( values [ 0 ], prefix_values [ 0 ] ) >= 0 && values [ 0 ].size >= 7 && base::same_bytes ( values [ 0 ].bytes, wanted, 7 );

			seen++;
			in_order = in_order && storage::cursor_next ( at );

		}

		expect ( in_order && seen == 5, "a seek lands on the first key at or after a prefix" );
		expect ( storage::drop_btree ( item, root ), "the index is dropped" );
		expect ( item.header.free_count + 1 == item.header.page_count, "dropping frees every page" );
		storage::close_pager ( item );

	}

	static void value_test ( ) {

		storage::value values [ 5 ];
		storage::value back [ 5 ];
		base::text record;
		uint32_t count = 0;
		uint8_t blob [ 3 ] = { 1, 2, 3 };

		values [ 0 ] = storage::null_value ( );
		values [ 1 ] = storage::integer_value ( -123456789012ll );
		values [ 2 ] = storage::real_value ( 3.25 );
		values [ 3 ] = storage::text_value ( "hello", 5 );
		values [ 4 ] = storage::blob_value ( blob, 3 );
		storage::encode_record ( values, 5, record );

		bool fine = storage::decode_record ( reinterpret_cast < const uint8_t* > ( record.value ( ) ), record.size, back, 5, count ) && count == 5;

		fine = fine && back [ 0 ].kind == storage::VALUE_NULL && back [ 1 ].integer == -123456789012ll && back [ 2 ].real == 3.25;
		fine = fine && back [ 3 ].size == 5 && base::same_bytes ( back [ 3 ].bytes, "hello", 5 ) && back [ 4 ].size == 3 && back [ 4 ].bytes [ 2 ] == 3;
		expect ( fine, "a record of every type round trips" );

		storage::value two = storage::integer_value ( 2 );
		storage::value two_and_half = storage::real_value ( 2.5 );
		storage::value word = storage::text_value ( "a", 1 );

		expect ( storage::compare_values ( two, two_and_half ) < 0 && storage::compare_values ( two_and_half, word ) < 0 && storage::compare_values ( values [ 0 ], two ) < 0, "values order as null, numbers, text" );
		expect ( !storage::decode_record ( reinterpret_cast < const uint8_t* > ( record.value ( ) ), record.size - 1, back, 5, count ), "a cut record is refused" );

	}

	void btree_tests ( ) {

		section ( "records and b-trees" );
		value_test ( );

		char path [ FILE_PATH_ROOM ];

		if ( !expect ( scratch_path ( path, sizeof ( path ), "strata-test-btree.sdb" ), "a scratch path exists" ) )
			return;

		forget_database ( path );
		table_model_test ( path );
		forget_database ( path );
		index_test ( path );
		forget_database ( path );
		statement_undo_test ( path );
		forget_database ( path );

	}

}
