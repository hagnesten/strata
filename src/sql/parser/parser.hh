#pragma once

#include "sql/lexer/lexer.hh"
#include "sql/syntax/syntax.hh"

#define PARSE_LIST_ROOM 2000
#define PARSE_DEPTH 200

namespace sql {

	struct parser {

		lexer lex;
		token current;
		base::arena* memory;
		parse_error* error;
		uint32_t parameters;
		uint32_t depth;

	};

	template < typename type > struct grow_list {

		type* items = nullptr;
		uint32_t count = 0;
		uint32_t room = 0;

		bool push ( parser& owner, const type& value ) {

			if ( count == PARSE_LIST_ROOM )
				return fail_here ( owner, "too many items in a list" );

			if ( count == room ) {

				uint32_t grown = room ? room * 2 : 8;
				type* made = owner.memory->make < type > ( grown );

				if ( !made )
					return fail_here ( owner, "out of memory" );

				if ( items )
					base::copy ( made, items, sizeof ( type ) * count );

				items = made;
				room = grown;

			}

			items [ count++ ] = value;

			return true;

		}

	};

	bool fail_here ( parser& item, const char* message );

	bool parse_one ( base::arena& memory, const char* source, size_t& consumed, statement*& out, parse_error& error );

	void advance ( parser& item );

	bool accept_word ( parser& item, const char* word );

	bool accept_symbol ( parser& item, const char* symbol );

	bool expect_word ( parser& item, const char* word );

	bool expect_symbol ( parser& item, const char* symbol );

	bool reserved_word ( const token& item );

	const char* take_name ( parser& item, const char* what );

	expression* parse_expression ( parser& item );

	select_statement* parse_select ( parser& item );

	expression* new_expression ( parser& item, expression_kind kind );

	bool read_real ( const char* text, size_t size, double& out );

}
