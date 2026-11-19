#include "engine/search/search.hh"

namespace engine {

	bool search_function ( int32_t kind ) {

		return kind == FUNCTION_BM25 || kind == FUNCTION_HIGHLIGHT || kind == FUNCTION_SNIPPET;

	}

	static query_state* owner_of ( query_state* scope, uint32_t depth ) {

		for ( uint32_t i = 0; i < depth && scope; i++ )
			scope = scope->parent;

		return scope;

	}

	static void positive_terms ( const search_node* node, const search_node** out, uint32_t& count, uint32_t room ) {

		if ( !node || node->kind == SEARCH_NOT )
			return;

		if ( node->kind == SEARCH_TERM ) {

			if ( count < room )
				out [ count++ ] = node;

			return;

		}

		positive_terms ( node->left, out, count, room );
		positive_terms ( node->right, out, count, room );

	}

	static bool hits_term ( const search_token& token, const search_node* const* terms, uint32_t count ) {

		for ( uint32_t i = 0; i < count; i++ )
			if ( same_search_term ( token, *terms [ i ] ) )
				return true;

		return false;

	}

	static bool text_arg ( run_context& ctx, query_state* scope, const sql::expression* item, uint32_t at, const char* fallback, storage::value& out ) {

		if ( at >= item->count ) {

			out = storage::text_value ( fallback, base::length ( fallback ) );

			return true;

		}

		storage::value value;

		if ( !evaluate ( ctx, scope, item->list [ at ], value ) )
			return false;

		out = value.kind == storage::VALUE_NULL ? storage::text_value ( "", 0 ) : text_of ( *ctx.scratch, value );

		return true;

	}

	static bool integer_arg ( run_context& ctx, query_state* scope, const sql::expression* item, uint32_t at, int64_t fallback, int64_t& out ) {

		out = fallback;

		if ( at >= item->count )
			return true;

		storage::value value;

		if ( !evaluate ( ctx, scope, item->list [ at ], value ) )
			return false;

		if ( value.kind != storage::VALUE_NULL )
			integer_of ( value, out );

		return true;

	}

	static void add_bytes ( base::text& out, const storage::value& item ) {

		out.add ( reinterpret_cast < const char* > ( item.bytes ), item.size );

	}

	static bool column_text ( run_context& ctx, const search_entry& entry, const source_state& source, uint32_t column, storage::value& out ) {

		const index_info& index = *entry.binding->index;
		const storage::value& value = source.values [ index.columns [ column ] ];

		out = value.kind == storage::VALUE_NULL ? storage::text_value ( "", 0 ) : text_of ( *ctx.scratch, value );

		return true;

	}

	static uint32_t count_hits ( const storage::value& text, const search_node* const* terms, uint32_t count ) {

		search_token token;
		size_t at = 0;
		uint32_t hits = 0;

		while ( next_search_token ( text.bytes, text.size, at, token ) )
			if ( hits_term ( token, terms, count ) )
				hits++;

		return hits;

	}

	static bool make_highlight ( run_context& ctx, const storage::value& text, const search_node* const* terms, uint32_t count, const storage::value& open, const storage::value& close, storage::value& out ) {

		base::text made;
		search_token token;
		size_t at = 0;
		size_t copied = 0;

		while ( next_search_token ( text.bytes, text.size, at, token ) ) {

			if ( !hits_term ( token, terms, count ) )
				continue;

			made.add ( reinterpret_cast < const char* > ( text.bytes + copied ), token.start - copied );
			add_bytes ( made, open );
			made.add ( reinterpret_cast < const char* > ( text.bytes + token.start ), token.size );
			add_bytes ( made, close );
			copied = token.start + token.size;

		}

		made.add ( reinterpret_cast < const char* > ( text.bytes + copied ), text.size - copied );

		char* kept = ctx.scratch->keep ( made.value ( ), made.size );

		if ( !kept )
			return fail_run ( ctx, "out of memory" );

		out = storage::text_value ( kept, made.size );

		return true;

	}

	static bool make_snippet ( run_context& ctx, const storage::value& text, const search_node* const* terms, uint32_t count, const storage::value& open, const storage::value& close, const storage::value& ellipsis, uint32_t window, storage::value& out ) {

		search_token* tokens = nullptr;
		uint32_t token_count = 0;
		uint32_t room = 0;
		search_token token;
		size_t at = 0;

		while ( next_search_token ( text.bytes, text.size, at, token ) )
			if ( !base::push ( *ctx.scratch, tokens, token_count, room, token ) )
				return fail_run ( ctx, "out of memory" );

		uint32_t best = 0;
		uint32_t best_hits = 0;
		uint32_t span = window < token_count ? window : token_count;

		for ( uint32_t start = 0; start + span <= token_count && span; start++ ) {

			uint32_t hits = 0;

			for ( uint32_t k = start; k < start + span; k++ )
				if ( hits_term ( tokens [ k ], terms, count ) )
					hits++;

			if ( hits > best_hits ) {

				best_hits = hits;
				best = start;

			}

		}

		base::text made;

		if ( !span ) {

			out = storage::text_value ( "", 0 );

			return true;

		}

		size_t from = best ? tokens [ best ].start : 0;
		size_t to = best + span < token_count ? tokens [ best + span - 1 ].start + tokens [ best + span - 1 ].size : text.size;

		if ( best )
			add_bytes ( made, ellipsis );

		size_t copied = from;

		for ( uint32_t k = best; k < best + span; k++ ) {

			if ( !hits_term ( tokens [ k ], terms, count ) )
				continue;

			made.add ( reinterpret_cast < const char* > ( text.bytes + copied ), tokens [ k ].start - copied );
			add_bytes ( made, open );
			made.add ( reinterpret_cast < const char* > ( text.bytes + tokens [ k ].start ), tokens [ k ].size );
			add_bytes ( made, close );
			copied = tokens [ k ].start + tokens [ k ].size;

		}

		made.add ( reinterpret_cast < const char* > ( text.bytes + copied ), to - copied );

		if ( best + span < token_count )
			add_bytes ( made, ellipsis );

		char* kept = ctx.scratch->keep ( made.value ( ), made.size );

		if ( !kept )
			return fail_run ( ctx, "out of memory" );

		out = storage::text_value ( kept, made.size );

		return true;

	}

	bool call_search_function ( run_context& ctx, query_state* scope, const sql::expression* item, storage::value& out ) {

		const sql::expression* target = item->list [ 0 ];
		query_state* owner = owner_of ( scope, target->depth );
		search_entry* entry = nullptr;

		for ( search_entry* walk = ctx.searches; walk && owner; walk = walk->next ) {

			if ( walk->plan == owner->plan && walk->source == target->source ) {

				entry = walk;

				break;

			}

		}

		if ( !entry )
			return fail_named ( ctx, "the function ", item->name, "() needs a MATCH on the same table earlier in the query" );

		const source_state& source = owner->sources [ target->source ];

		out = storage::null_value ( );

		if ( source.null_row || !source.values )
			return true;

		if ( item->column == FUNCTION_BM25 ) {

			double score = 0;

			if ( search_find ( entry->result, source.rowid, score ) )
				out = storage::real_value ( -score );

			return true;

		}

		const search_node* terms [ SEARCH_DEPTH ];
		uint32_t count = 0;
		uint32_t columns = entry->binding->index->column_count;
		int64_t column = -1;
		storage::value open;
		storage::value close;
		storage::value ellipsis;
		storage::value text;

		positive_terms ( entry->tree, terms, count, SEARCH_DEPTH );

		if ( !integer_arg ( ctx, scope, item, 1, -1, column ) || !text_arg ( ctx, scope, item, 2, "", open ) || !text_arg ( ctx, scope, item, 3, "", close ) )
			return false;

		if ( column >= static_cast < int64_t > ( columns ) )
			return fail_named ( ctx, "the column number given to ", item->name, "() is past the indexed columns" );

		if ( column < 0 ) {

			uint32_t best_hits = 0;

			column = 0;

			for ( uint32_t c = 0; c < columns; c++ ) {

				storage::value candidate;

				column_text ( ctx, *entry, source, c, candidate );

				uint32_t hits = count_hits ( candidate, terms, count );

				if ( hits > best_hits ) {

					best_hits = hits;
					column = c;

				}

			}

		}

		column_text ( ctx, *entry, source, static_cast < uint32_t > ( column ), text );

		if ( item->column == FUNCTION_HIGHLIGHT )
			return make_highlight ( ctx, text, terms, count, open, close, out );

		int64_t window = SEARCH_SNIPPET_TOKENS;

		if ( !text_arg ( ctx, scope, item, 4, "\xE2\x80\xA6", ellipsis ) || !integer_arg ( ctx, scope, item, 5, SEARCH_SNIPPET_TOKENS, window ) )
			return false;

		return make_snippet ( ctx, text, terms, count, open, close, ellipsis, static_cast < uint32_t > ( window < 1 ? 1 : window > 64 ? 64 : window ), out );

	}

}