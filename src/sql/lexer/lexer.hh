#pragma once

#include "base/text/text.hh"

namespace sql {

	enum token_kind : uint8_t {

		TOKEN_END,
		TOKEN_NAME,
		TOKEN_INTEGER,
		TOKEN_REAL,
		TOKEN_STRING,
		TOKEN_BLOB,
		TOKEN_PARAMETER,
		TOKEN_SYMBOL,
		TOKEN_BAD,

	};

	struct token {

		token_kind kind;
		const char* start;
		uint32_t size;
		uint32_t line;
		uint32_t column;
		char symbol [ 4 ];

	};

	struct lexer {

		const char* source;
		const char* at;
		uint32_t line;
		const char* line_start;

	};

	void start_lexer ( lexer& out, const char* source );

	token next_token ( lexer& item );

	bool token_is ( const token& item, const char* word );

	bool symbol_is ( const token& item, const char* symbol );

}
