#include "engine/search/search.hh"
#include "base/sort/sort.hh"

namespace engine {

	static void entry_key ( base::text& out, const char* term, size_t size, const storage::value& rowid, uint64_t first, uint64_t second, uint32_t width ) {

		storage::value values [ 4 ];

		values [ 0 ] = storage::text_value ( term, size );
		values [ 1 ] = rowid;
		values [ 2 ] = storage::integer_value ( static_cast < int64_t > ( first ) );
		values [ 3 ] = storage::integer_value ( static_cast < int64_t > ( second ) );
		out.clear ( );
		storage::encode_record ( values, width, out );

	}

	static int32_t compare_words ( void* context, const void* a, const void* b ) {

		( void ) context;

		const search_word& x = *static_cast < const search_word* > ( a );
		const search_word& y = *static_cast < const search_word* > ( b );
		uint32_t shared = x.size < y.size ? x.size : y.size;

		for ( uint32_t i = 0; i < shared; i++ )
			if ( x.text [ i ] != y.text [ i ] )
				return static_cast < uint8_t > ( x.text [ i ] ) < static_cast < uint8_t > ( y.text [ i ] ) ? -1 : 1;

		return x.size == y.size ? 0 : x.size < y.size ? -1 : 1;

	}

	static bool change_entry ( run_context& ctx, storage::btree& tree, const base::text& key, bool adding ) {

		bool flag = false;
		const uint8_t* bytes = reinterpret_cast < const uint8_t* > ( key.value ( ) );

		if ( adding ? !storage::index_insert ( tree, bytes, key.size, flag ) : !storage::index_delete ( tree, bytes, key.size, flag ) )
			return fail_storage ( ctx, "a FULLTEXT index could not be written" );

		return true;

	}

	bool read_totals ( run_context& ctx, const index_info& index, search_totals& out ) {

		storage::btree tree = { ctx.pages, index.root, true };
		storage::cursor at;
		base::text probe;

		out = { };
		entry_key ( probe, "", 0, storage::null_value ( ), 0, 0, 2 );

		if ( !storage::cursor_seek_key ( at, tree, reinterpret_cast < const uint8_t* > ( probe.value ( ) ), probe.size, 2 ) )
			return fail_storage ( ctx, "a FULLTEXT index could not be read" );

		if ( !at.valid )
			return true;

		if ( !storage::cursor_payload ( at, ctx.row_bytes ) )
			return fail_storage ( ctx, "a FULLTEXT index could not be read" );

		storage::value key [ 4 ];
		uint32_t count = 0;

		if ( !storage::decode_record ( reinterpret_cast < const uint8_t* > ( ctx.row_bytes.value ( ) ), ctx.row_bytes.size, key, 4, count ) || count != 4 )
			return fail_named ( ctx, "an entry of index ", index.name, " is damaged" );

		if ( key [ 0 ].kind == storage::VALUE_TEXT && !key [ 0 ].size && key [ 1 ].kind == storage::VALUE_NULL ) {

			out.documents = static_cast < uint64_t > ( key [ 2 ].integer );
			out.length = static_cast < uint64_t > ( key [ 3 ].integer );

		}

		return true;

	}

	bool document_length ( run_context& ctx, const index_info& index, int64_t rowid, uint64_t& out ) {

		storage::btree tree = { ctx.pages, index.root, true };
		storage::cursor at;
		base::text probe;

		out = 0;
		entry_key ( probe, "", 0, storage::integer_value ( rowid ), 0, 0, 2 );

		if ( !storage::cursor_seek_key ( at, tree, reinterpret_cast < const uint8_t* > ( probe.value ( ) ), probe.size, 2 ) )
			return fail_storage ( ctx, "a FULLTEXT index could not be read" );

		if ( !at.valid || !storage::cursor_payload ( at, ctx.row_bytes ) )
			return at.valid ? fail_storage ( ctx, "a FULLTEXT index could not be read" ) : true;

		storage::value key [ 4 ];
		uint32_t count = 0;

		if ( storage::decode_record ( reinterpret_cast < const uint8_t* > ( ctx.row_bytes.value ( ) ), ctx.row_bytes.size, key, 4, count ) && count == 4 && key [ 0 ].kind == storage::VALUE_TEXT && !key [ 0 ].size && key [ 1 ].kind == storage::VALUE_INTEGER && key [ 1 ].integer == rowid )
			out = static_cast < uint64_t > ( key [ 2 ].integer );

		return true;

	}

	bool index_document ( run_context& ctx, const index_info& index, const storage::value* values, int64_t rowid, bool adding ) {

		search_word** words = nullptr;
		uint32_t count = 0;
		uint32_t room = 0;

		for ( uint32_t c = 0; c < index.column_count; c++ ) {

			const storage::value& value = values [ index.columns [ c ] ];

			if ( value.kind == storage::VALUE_NULL )
				continue;

			storage::value text = text_of ( *ctx.scratch, value );
			search_token token;
			size_t at = 0;

			while ( next_search_token ( text.bytes, text.size, at, token ) ) {

				search_word* word = ctx.scratch->make < search_word > ( 1 );

				if ( !word || !base::push ( *ctx.scratch, words, count, room, word ) )
					return fail_run ( ctx, "out of memory" );

				base::copy ( word->text, token.folded, token.folded_size );
				word->size = token.folded_size;
				word->column = c;

			}

		}

		if ( count && !base::sort_pointers ( reinterpret_cast < void** > ( words ), count, compare_words, nullptr ) )
			return fail_run ( ctx, "out of memory while sorting words" );

		storage::btree tree = { ctx.pages, index.root, true };
		base::text key;

		for ( uint32_t i = 0; i < count; ) {

			uint32_t end = i;
			uint64_t mask = 0;

			while ( end < count && !compare_words ( nullptr, words [ i ], words [ end ] ) ) {

				mask |= 1ull << words [ end ]->column;
				end++;

			}

			entry_key ( key, words [ i ]->text, words [ i ]->size, storage::integer_value ( rowid ), end - i, mask, 4 );

			if ( !change_entry ( ctx, tree, key, adding ) )
				return false;

			i = end;

		}

		entry_key ( key, "", 0, storage::integer_value ( rowid ), count, 0, 4 );

		if ( !change_entry ( ctx, tree, key, adding ) )
			return false;

		search_totals totals;

		if ( !read_totals ( ctx, index, totals ) )
			return false;

		if ( totals.documents || totals.length ) {

			entry_key ( key, "", 0, storage::null_value ( ), totals.documents, totals.length, 4 );

			if ( !change_entry ( ctx, tree, key, false ) )
				return false;

		}

		totals.documents = adding ? totals.documents + 1 : totals.documents ? totals.documents - 1 : 0;
		totals.length = adding ? totals.length + count : totals.length >= count ? totals.length - count : 0;
		entry_key ( key, "", 0, storage::null_value ( ), totals.documents, totals.length, 4 );

		return change_entry ( ctx, tree, key, true );

	}

	bool collect_postings ( run_context& ctx, const index_info& index, const search_node& term, uint64_t columns, search_posting*& out, uint32_t& count ) {

		storage::btree tree = { ctx.pages, index.root, true };
		storage::cursor at;
		base::text probe;
		uint32_t room = 0;

		out = nullptr;
		count = 0;
		entry_key ( probe, term.term, term.term_size, storage::null_value ( ), 0, 0, 1 );

		if ( !storage::cursor_seek_key ( at, tree, reinterpret_cast < const uint8_t* > ( probe.value ( ) ), probe.size, 1 ) )
			return fail_storage ( ctx, "a FULLTEXT index could not be read" );

		while ( at.valid ) {

			if ( !storage::cursor_payload ( at, ctx.row_bytes ) )
				return fail_storage ( ctx, "a FULLTEXT index could not be read" );

			storage::value key [ 4 ];
			uint32_t fields = 0;

			if ( !storage::decode_record ( reinterpret_cast < const uint8_t* > ( ctx.row_bytes.value ( ) ), ctx.row_bytes.size, key, 4, fields ) || fields != 4 || key [ 0 ].kind != storage::VALUE_TEXT )
				return fail_named ( ctx, "an entry of index ", index.name, " is damaged" );

			bool same = term.prefix ? key [ 0 ].size >= term.term_size && base::same_bytes ( key [ 0 ].bytes, term.term, term.term_size ) : key [ 0 ].size == term.term_size && base::same_bytes ( key [ 0 ].bytes, term.term, term.term_size );

			if ( !same )
				break;

			if ( key [ 1 ].kind == storage::VALUE_INTEGER && ( static_cast < uint64_t > ( key [ 3 ].integer ) & columns ) ) {

				search_posting posting = { key [ 1 ].integer, static_cast < uint64_t > ( key [ 2 ].integer ) };

				if ( !base::push ( *ctx.memory, out, count, room, posting ) )
					return fail_run ( ctx, "out of memory" );

			}

			if ( !storage::cursor_next ( at ) )
				return fail_storage ( ctx, "a FULLTEXT index could not be read" );

		}

		return true;

	}

	bool all_documents ( run_context& ctx, const index_info& index, search_result& out ) {

		storage::btree tree = { ctx.pages, index.root, true };
		storage::cursor at;
		base::text probe;

		out = { };
		entry_key ( probe, "", 0, storage::null_value ( ), 0, 0, 1 );

		if ( !storage::cursor_seek_key ( at, tree, reinterpret_cast < const uint8_t* > ( probe.value ( ) ), probe.size, 1 ) )
			return fail_storage ( ctx, "a FULLTEXT index could not be read" );

		while ( at.valid ) {

			if ( !storage::cursor_payload ( at, ctx.row_bytes ) )
				return fail_storage ( ctx, "a FULLTEXT index could not be read" );

			storage::value key [ 4 ];
			uint32_t fields = 0;

			if ( !storage::decode_record ( reinterpret_cast < const uint8_t* > ( ctx.row_bytes.value ( ) ), ctx.row_bytes.size, key, 4, fields ) || fields != 4 || key [ 0 ].kind != storage::VALUE_TEXT || key [ 0 ].size )
				break;

			if ( key [ 1 ].kind == storage::VALUE_INTEGER ) {

				search_hit hit = { key [ 1 ].integer, 0 };

				if ( !base::push ( *ctx.memory, out.hits, out.count, out.room, hit ) )
					return fail_run ( ctx, "out of memory" );

			}

			if ( !storage::cursor_next ( at ) )
				return fail_storage ( ctx, "a FULLTEXT index could not be read" );

		}

		return true;

	}

}