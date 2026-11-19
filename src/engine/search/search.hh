#pragma once

#include "engine/run/run.hh"

#define SEARCH_COLUMNS 63
#define SEARCH_TOKEN_ROOM 64
#define SEARCH_DEPTH 64
#define SEARCH_K1 1.2
#define SEARCH_B 0.75
#define SEARCH_SNIPPET_TOKENS 15

namespace engine {

	struct search_token {

		size_t start;
		size_t size;
		char folded [ SEARCH_TOKEN_ROOM ];
		uint32_t folded_size;

	};

	struct search_binding {

		index_info* index;
		table_info* table;
		uint64_t columns;
		int32_t source;
		uint32_t depth;

	};

	enum search_node_kind : uint8_t {

		SEARCH_TERM,
		SEARCH_AND,
		SEARCH_OR,
		SEARCH_NOT,

	};

	struct search_node {

		search_node_kind kind;
		char term [ SEARCH_TOKEN_ROOM ];
		uint32_t term_size;
		bool prefix;
		search_node* left;
		search_node* right;

	};

	struct search_hit {

		int64_t rowid;
		double score;

	};

	struct search_result {

		search_hit* hits;
		uint32_t count;
		uint32_t room;

	};

	struct search_totals {

		uint64_t documents;
		uint64_t length;

	};

	struct search_entry {

		const sql::expression* match;
		const prepared_select* plan;
		int32_t source;
		const search_binding* binding;
		char* query;
		size_t query_size;
		search_node* tree;
		search_result result;
		search_entry* next;

	};

	struct search_parse {

		const uint8_t* text;
		size_t size;
		size_t at;
		base::arena* memory;
		uint32_t depth;
		bool failed;

	};

	struct search_posting {

		int64_t rowid;
		uint64_t frequency;

	};

	struct search_word {

		char text [ SEARCH_TOKEN_ROOM ];
		uint32_t size;
		uint32_t column;

	};

	bool next_search_token ( const uint8_t* text, size_t size, size_t& at, search_token& out );

	bool same_search_term ( const search_token& token, const search_node& term );

	bool index_document ( run_context& ctx, const index_info& index, const storage::value* values, int64_t rowid, bool adding );

	bool read_totals ( run_context& ctx, const index_info& index, search_totals& out );

	bool document_length ( run_context& ctx, const index_info& index, int64_t rowid, uint64_t& out );

	bool collect_postings ( run_context& ctx, const index_info& index, const search_node& term, uint64_t columns, search_posting*& out, uint32_t& count );

	bool all_documents ( run_context& ctx, const index_info& index, search_result& out );

	search_node* parse_search ( base::arena& memory, const uint8_t* text, size_t size );

	bool search_lookup ( run_context& ctx, query_state* scope, const sql::expression* match, search_entry*& out );

	bool search_find ( const search_result& result, int64_t rowid, double& score );

	bool evaluate_match ( run_context& ctx, query_state* scope, const sql::expression* item, storage::value& out );

	bool search_function ( int32_t kind );

	bool call_search_function ( run_context& ctx, query_state* scope, const sql::expression* item, storage::value& out );

}