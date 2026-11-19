#include "engine/search/search.hh"
#include "base/math/math.hh"

namespace engine {

	static bool word_byte ( uint8_t byte ) {

		return ( byte >= '0' && byte <= '9' ) || ( byte >= 'a' && byte <= 'z' ) || ( byte >= 'A' && byte <= 'Z' ) || byte >= 0x80;

	}

	static void skip_space ( search_parse& item ) {

		while ( item.at < item.size && !word_byte ( item.text [ item.at ] ) && item.text [ item.at ] != '(' && item.text [ item.at ] != ')' && item.text [ item.at ] != '"' && item.text [ item.at ] != '-' )
			item.at++;

	}

	static bool operator_word ( search_parse& item, const char* word ) {

		size_t size = base::length ( word );

		if ( item.at + size > item.size || !base::same_bytes ( item.text + item.at, word, size ) )
			return false;

		if ( item.at + size < item.size && word_byte ( item.text [ item.at + size ] ) )
			return false;

		item.at += size;

		return true;

	}

	static search_node* make_node ( search_parse& item, search_node_kind kind, search_node* left, search_node* right ) {

		search_node* made = item.memory->make < search_node > ( 1 );

		if ( !made ) {

			item.failed = true;

			return nullptr;

		}

		made->kind = kind;
		made->left = left;
		made->right = right;

		return made;

	}

	static search_node* parse_or ( search_parse& item );

	static search_node* parse_word ( search_parse& item ) {

		search_token token;
		size_t at = item.at;

		if ( !next_search_token ( item.text, item.size, at, token ) )
			return nullptr;

		item.at = at;

		search_node* made = make_node ( item, SEARCH_TERM, nullptr, nullptr );

		if ( !made )
			return nullptr;

		base::copy ( made->term, token.folded, token.folded_size );
		made->term_size = token.folded_size;

		if ( item.at < item.size && item.text [ item.at ] == '*' ) {

			made->prefix = true;
			item.at++;

		}

		return made;

	}

	static search_node* parse_phrase ( search_parse& item ) {

		item.at++;

		size_t end = item.at;

		while ( end < item.size && item.text [ end ] != '"' )
			end++;

		search_node* joined = nullptr;
		search_token token;
		size_t at = item.at;

		while ( next_search_token ( item.text, end, at, token ) ) {

			search_node* term = make_node ( item, SEARCH_TERM, nullptr, nullptr );

			if ( !term )
				return nullptr;

			base::copy ( term->term, token.folded, token.folded_size );
			term->term_size = token.folded_size;
			joined = joined ? make_node ( item, SEARCH_AND, joined, term ) : term;

		}

		item.at = end < item.size ? end + 1 : end;

		return joined;

	}

	static search_node* parse_unary ( search_parse& item ) {

		skip_space ( item );

		if ( item.at >= item.size || ++item.depth > SEARCH_DEPTH ) {

			item.depth--;

			return nullptr;

		}

		search_node* result = nullptr;

		if ( item.text [ item.at ] == '-' ) {

			item.at++;

			search_node* inner = parse_unary ( item );

			result = inner ? make_node ( item, SEARCH_NOT, inner, nullptr ) : nullptr;

		} else if ( operator_word ( item, "NOT" ) ) {

			search_node* inner = parse_unary ( item );

			result = inner ? make_node ( item, SEARCH_NOT, inner, nullptr ) : nullptr;

		} else if ( item.text [ item.at ] == '(' ) {

			item.at++;
			result = parse_or ( item );
			skip_space ( item );

			if ( item.at < item.size && item.text [ item.at ] == ')' )
				item.at++;

		} else if ( item.text [ item.at ] == '"' )
			result = parse_phrase ( item );
		else
			result = parse_word ( item );

		item.depth--;

		return result;

	}

	static bool ends_group ( search_parse& item ) {

		skip_space ( item );

		if ( item.at >= item.size || item.text [ item.at ] == ')' )
			return true;

		size_t saved = item.at;

		if ( operator_word ( item, "OR" ) ) {

			item.at = saved;

			return true;

		}

		return false;

	}

	static search_node* parse_and ( search_parse& item ) {

		search_node* left = nullptr;

		while ( !ends_group ( item ) && !item.failed ) {

			operator_word ( item, "AND" );

			size_t before = item.at;
			search_node* right = parse_unary ( item );

			if ( !right ) {

				if ( item.at == before )
					item.at++;

				continue;

			}

			left = left ? make_node ( item, SEARCH_AND, left, right ) : right;

		}

		return left;

	}

	static search_node* parse_or ( search_parse& item ) {

		search_node* left = parse_and ( item );

		for ( ;; ) {

			skip_space ( item );

			if ( item.failed || !operator_word ( item, "OR" ) )
				return left;

			search_node* right = parse_and ( item );

			if ( right )
				left = left ? make_node ( item, SEARCH_OR, left, right ) : right;

		}

	}

	search_node* parse_search ( base::arena& memory, const uint8_t* text, size_t size ) {

		search_parse item = { text, size, 0, &memory, 0, false };
		search_node* tree = parse_or ( item );

		return item.failed ? nullptr : tree;

	}

	static void sort_hits ( search_hit* hits, uint32_t count ) {

		for ( uint32_t gap = count / 2; gap; gap /= 2 ) {

			for ( uint32_t i = gap; i < count; i++ ) {

				search_hit here = hits [ i ];
				uint32_t k = i;

				while ( k >= gap && hits [ k - gap ].rowid > here.rowid ) {

					hits [ k ] = hits [ k - gap ];
					k -= gap;

				}

				hits [ k ] = here;

			}

		}

	}

	static bool term_hits ( run_context& ctx, const search_binding& binding, const search_totals& totals, const search_node& term, search_result& out ) {

		search_posting* postings = nullptr;
		uint32_t count = 0;

		out = { };

		if ( !collect_postings ( ctx, *binding.index, term, binding.columns, postings, count ) )
			return false;

		out.hits = ctx.memory->make < search_hit > ( count + 1 );

		if ( !out.hits )
			return fail_run ( ctx, "out of memory" );

		for ( uint32_t i = 0; i < count; i++ )
			out.hits [ i ] = { postings [ i ].rowid, static_cast < double > ( postings [ i ].frequency ) };

		if ( term.prefix )
			sort_hits ( out.hits, count );

		uint32_t unique = 0;

		for ( uint32_t i = 0; i < count; i++ ) {

			if ( unique && out.hits [ unique - 1 ].rowid == out.hits [ i ].rowid )
				out.hits [ unique - 1 ].score += out.hits [ i ].score;
			else
				out.hits [ unique++ ] = out.hits [ i ];

		}

		out.count = unique;
		out.room = count + 1;

		double documents = static_cast < double > ( totals.documents ? totals.documents : 1 );
		double average = totals.documents ? static_cast < double > ( totals.length ) / documents : 1;
		double weight = base::natural_log ( 1 + ( documents - unique + 0.5 ) / ( unique + 0.5 ) );

		if ( average <= 0 )
			average = 1;

		for ( uint32_t i = 0; i < out.count; i++ ) {

			uint64_t length = 0;

			if ( !document_length ( ctx, *binding.index, out.hits [ i ].rowid, length ) )
				return false;

			double frequency = out.hits [ i ].score;

			out.hits [ i ].score = weight * frequency * ( SEARCH_K1 + 1 ) / ( frequency + SEARCH_K1 * ( 1 - SEARCH_B + SEARCH_B * static_cast < double > ( length ) / average ) );

		}

		return true;

	}

	static bool combine ( run_context& ctx, const search_result& a, const search_result& b, search_node_kind kind, search_result& out ) {

		out = { };
		out.room = a.count + b.count + 1;
		out.hits = ctx.memory->make < search_hit > ( out.room );

		if ( !out.hits )
			return fail_run ( ctx, "out of memory" );

		uint32_t i = 0;
		uint32_t k = 0;

		while ( i < a.count || k < b.count ) {

			bool take_a = k >= b.count || ( i < a.count && a.hits [ i ].rowid < b.hits [ k ].rowid );
			bool take_b = i >= a.count || ( k < b.count && b.hits [ k ].rowid < a.hits [ i ].rowid );

			if ( !take_a && !take_b ) {

				if ( kind != SEARCH_NOT )
					out.hits [ out.count++ ] = { a.hits [ i ].rowid, a.hits [ i ].score + b.hits [ k ].score };

				i++;
				k++;

			} else if ( take_a ) {

				if ( kind != SEARCH_AND )
					out.hits [ out.count++ ] = a.hits [ i ];

				i++;

			} else {

				if ( kind == SEARCH_OR )
					out.hits [ out.count++ ] = b.hits [ k ];

				k++;

			}

		}

		return true;

	}

	static bool evaluate_node ( run_context& ctx, const search_binding& binding, const search_totals& totals, const search_node* node, search_result& out ) {

		out = { };

		if ( !node )
			return true;

		switch ( node->kind ) {

			case SEARCH_TERM:
				return term_hits ( ctx, binding, totals, *node, out );

			case SEARCH_NOT: {

				search_result everything;
				search_result inner;

				return all_documents ( ctx, *binding.index, everything ) && evaluate_node ( ctx, binding, totals, node->left, inner ) && combine ( ctx, everything, inner, SEARCH_NOT, out );

			}

			case SEARCH_AND: {

				search_result left;
				search_result right;

				if ( node->right && node->right->kind == SEARCH_NOT )
					return evaluate_node ( ctx, binding, totals, node->left, left ) && evaluate_node ( ctx, binding, totals, node->right->left, right ) && combine ( ctx, left, right, SEARCH_NOT, out );

				if ( node->left && node->left->kind == SEARCH_NOT )
					return evaluate_node ( ctx, binding, totals, node->right, right ) && evaluate_node ( ctx, binding, totals, node->left->left, left ) && combine ( ctx, right, left, SEARCH_NOT, out );

				return evaluate_node ( ctx, binding, totals, node->left, left ) && evaluate_node ( ctx, binding, totals, node->right, right ) && combine ( ctx, left, right, SEARCH_AND, out );

			}

			case SEARCH_OR: {

				search_result left;
				search_result right;

				return evaluate_node ( ctx, binding, totals, node->left, left ) && evaluate_node ( ctx, binding, totals, node->right, right ) && combine ( ctx, left, right, SEARCH_OR, out );

			}

		}

		return true;

	}

	static query_state* owner_of ( query_state* scope, uint32_t depth ) {

		for ( uint32_t i = 0; i < depth && scope; i++ )
			scope = scope->parent;

		return scope;

	}

	bool search_lookup ( run_context& ctx, query_state* scope, const sql::expression* match, search_entry*& out ) {

		const search_binding* binding = static_cast < const search_binding* > ( match->search );
		query_state* owner = owner_of ( scope, binding ? binding->depth : 0 );

		if ( !binding || !owner )
			return fail_run ( ctx, "MATCH cannot be used here" );

		storage::value query;

		if ( !evaluate ( ctx, scope, match->right, query ) )
			return false;

		storage::value text = query.kind == storage::VALUE_NULL ? storage::text_value ( "", 0 ) : text_of ( *ctx.scratch, query );

		for ( search_entry* entry = ctx.searches; entry; entry = entry->next ) {

			if ( entry->match == match && entry->plan == owner->plan && entry->query_size == text.size && base::same_bytes ( entry->query, text.bytes, text.size ) ) {

				out = entry;

				return true;

			}

		}

		search_entry* entry = ctx.memory->make < search_entry > ( 1 );

		if ( !entry || !( entry->query = ctx.memory->keep ( reinterpret_cast < const char* > ( text.bytes ), text.size ) ) )
			return fail_run ( ctx, "out of memory" );

		entry->match = match;
		entry->plan = owner->plan;
		entry->source = binding->source;
		entry->binding = binding;
		entry->query_size = text.size;
		entry->tree = parse_search ( *ctx.memory, text.bytes, text.size );

		search_totals totals;

		if ( !read_totals ( ctx, *binding->index, totals ) || !evaluate_node ( ctx, *binding, totals, entry->tree, entry->result ) )
			return false;

		entry->next = ctx.searches;
		ctx.searches = entry;
		out = entry;

		return true;

	}

	bool search_find ( const search_result& result, int64_t rowid, double& score ) {

		uint32_t low = 0;
		uint32_t high = result.count;

		while ( low < high ) {

			uint32_t middle = low + ( high - low ) / 2;

			if ( result.hits [ middle ].rowid < rowid )
				low = middle + 1;
			else
				high = middle;

		}

		if ( low < result.count && result.hits [ low ].rowid == rowid ) {

			score = result.hits [ low ].score;

			return true;

		}

		return false;

	}

	bool evaluate_match ( run_context& ctx, query_state* scope, const sql::expression* item, storage::value& out ) {

		search_entry* entry = nullptr;

		if ( !search_lookup ( ctx, scope, item, entry ) )
			return false;

		query_state* owner = owner_of ( scope, entry->binding->depth );
		source_state& source = owner->sources [ entry->binding->source ];

		if ( source.null_row || !source.values ) {

			out = storage::null_value ( );

			return true;

		}

		double score = 0;
		bool found = search_find ( entry->result, source.rowid, score );

		out = storage::integer_value ( found != item->negated ? 1 : 0 );

		return true;

	}

}