#include "sql/parser/parser.hh"

namespace sql {

	static const char* const reserved [ ] = {

		"select", "from", "where", "group", "having", "order", "limit", "offset", "union", "intersect", "except", "values",
		"insert", "update", "delete", "create", "drop", "alter", "into", "set", "on", "using", "join", "left", "inner",
		"cross", "outer", "and", "or", "not", "is", "in", "like", "glob", "between", "case", "when", "then", "else", "end",
		"as", "by", "asc", "desc", "null", "distinct", "all", "exists", "primary", "unique", "check", "default",
		"references", "foreign", "constraint", "table", "index", "begin", "commit", "rollback", "collate", "escape", "cast",

	};

	bool reserved_word ( const token& item ) {

		for ( const char* word : reserved )
			if ( token_is ( item, word ) )
				return true;

		return false;

	}

	void advance ( parser& item ) {

		item.current = next_token ( item.lex );

	}

	bool fail_here ( parser& item, const char* message ) {

		if ( item.error->failed )
			return false;

		item.error->failed = true;
		item.error->line = item.current.line;
		item.error->column = item.current.column;
		item.error->message.clear ( );
		item.error->message.add ( message );

		if ( item.current.kind == TOKEN_END )
			item.error->message.add ( ", but the statement ended" );
		else {

			item.error->message.add ( " near \"" );
			item.error->message.add ( item.current.start, base::min < uint32_t > ( item.current.size, 40 ) );
			item.error->message.add ( '"' );

		}

		return false;

	}

	bool accept_word ( parser& item, const char* word ) {

		if ( !token_is ( item.current, word ) )
			return false;

		advance ( item );

		return true;

	}

	bool accept_symbol ( parser& item, const char* symbol ) {

		if ( !symbol_is ( item.current, symbol ) )
			return false;

		advance ( item );

		return true;

	}

	bool expect_word ( parser& item, const char* word ) {

		if ( accept_word ( item, word ) )
			return true;

		base::text message;

		message.add ( "expected " ).add ( word );

		for ( size_t i = 9; i < message.size; i++ )
			message.data [ i ] = static_cast < char > ( message.data [ i ] >= 'a' && message.data [ i ] <= 'z' ? message.data [ i ] - 32 : message.data [ i ] );

		return fail_here ( item, message.value ( ) );

	}

	bool expect_symbol ( parser& item, const char* symbol ) {

		if ( accept_symbol ( item, symbol ) )
			return true;

		base::text message;

		message.add ( "expected \"" ).add ( symbol ).add ( '"' );

		return fail_here ( item, message.value ( ) );

	}

	const char* take_name ( parser& item, const char* what ) {

		const token& here = item.current;

		if ( here.kind != TOKEN_NAME || ( here.start [ 0 ] != '"' && here.start [ 0 ] != '`' && here.start [ 0 ] != '[' && reserved_word ( here ) ) ) {

			base::text message;

			message.add ( "expected " ).add ( what );
			fail_here ( item, message.value ( ) );

			return nullptr;

		}

		char* name = nullptr;

		if ( here.start [ 0 ] == '"' || here.start [ 0 ] == '`' || here.start [ 0 ] == '[' ) {

			char close = here.start [ 0 ] == '[' ? ']' : here.start [ 0 ];
			base::text plain;

			for ( uint32_t i = 1; i + 1 < here.size; i++ ) {

				plain.add ( here.start [ i ] );

				if ( here.start [ i ] == close && close != ']' )
					i++;

			}

			name = item.memory->keep ( plain.value ( ), plain.size );

		} else
			name = item.memory->keep ( here.start, here.size );

		advance ( item );

		return name;

	}

	expression* new_expression ( parser& item, expression_kind kind ) {

		expression* made = item.memory->make < expression > ( 1 );

		if ( !made ) {

			fail_here ( item, "out of memory" );

			return nullptr;

		}

		made->kind = kind;
		made->source = -1;
		made->column = -1;
		made->aggregate = -1;
		made->offset = static_cast < uint32_t > ( item.current.start - item.lex.source );

		return made;

	}

	bool read_real ( const char* text, size_t size, double& out ) {

		base::decimal_digits digits = { };
		int32_t exponent = 0;
		size_t at = 0;

		while ( at < size && text [ at ] >= '0' && text [ at ] <= '9' )
			base::add_digit ( digits, static_cast < uint32_t > ( text [ at++ ] - '0' ), false );

		if ( at < size && text [ at ] == '.' ) {

			at++;

			while ( at < size && text [ at ] >= '0' && text [ at ] <= '9' )
				base::add_digit ( digits, static_cast < uint32_t > ( text [ at++ ] - '0' ), true );

		}

		bool any = digits.any;

		if ( at < size && ( text [ at ] == 'e' || text [ at ] == 'E' ) ) {

			bool negative = false;

			at++;

			if ( at < size && ( text [ at ] == '+' || text [ at ] == '-' ) )
				negative = text [ at++ ] == '-';

			while ( at < size && text [ at ] >= '0' && text [ at ] <= '9' ) {

				exponent = base::min ( exponent * 10 + ( text [ at++ ] - '0' ), 400 );

			}

			if ( negative )
				exponent = -exponent;

		}

		out = base::decimal_value ( digits, exponent );

		return any && at == size;

	}

	static bool read_integer ( const token& here, storage::value& out ) {

		uint64_t number = 0;
		bool hex = here.size > 2 && here.start [ 0 ] == '0' && ( here.start [ 1 ] == 'x' || here.start [ 1 ] == 'X' );

		for ( uint32_t i = hex ? 2 : 0; i < here.size; i++ ) {

			char c = here.start [ i ];
			uint64_t digit = c >= '0' && c <= '9' ? static_cast < uint64_t > ( c - '0' ) : static_cast < uint64_t > ( base::lower ( c ) - 'a' + 10 );

			if ( hex ) {

				if ( number >> 60 )
					return false;

				number = number << 4 | digit;

			} else {

				if ( number > ( 0x7FFFFFFFFFFFFFFFull - digit ) / 10 ) {

					double real = 0;

					read_real ( here.start, here.size, real );
					out = storage::real_value ( real );

					return true;

				}

				number = number * 10 + digit;

			}

		}

		out = storage::integer_value ( static_cast < int64_t > ( number ) );

		return true;

	}

	static expression* parse_or ( parser& item );

	static expression* parse_comparison ( parser& item );

	static bool parse_list ( parser& item, expression**& out, uint32_t& count ) {

		grow_list < expression* > gathered;

		do {

			expression* one = parse_expression ( item );

			if ( !one || !gathered.push ( item, one ) )
				return false;

		} while ( accept_symbol ( item, "," ) );

		out = gathered.items;
		count = gathered.count;

		return true;

	}

	static bool starts_select ( const token& here ) {

		return token_is ( here, "select" ) || token_is ( here, "values" ) || token_is ( here, "with" );

	}

	static expression* parse_case ( parser& item ) {

		expression* made = new_expression ( item, EXPRESSION_CASE );
		grow_list < expression* > gathered;

		if ( !made )
			return nullptr;

		if ( !token_is ( item.current, "when" ) ) {

			made->left = parse_expression ( item );

			if ( !made->left )
				return nullptr;

		}

		while ( accept_word ( item, "when" ) ) {

			expression* when = parse_expression ( item );

			if ( !when || !gathered.push ( item, when ) || !expect_word ( item, "then" ) )
				return nullptr;

			expression* then = parse_expression ( item );

			if ( !then || !gathered.push ( item, then ) )
				return nullptr;

		}

		if ( !gathered.count ) {

			fail_here ( item, "expected WHEN" );

			return nullptr;

		}

		if ( accept_word ( item, "else" ) ) {

			made->right = parse_expression ( item );

			if ( !made->right )
				return nullptr;

		}

		if ( !expect_word ( item, "end" ) )
			return nullptr;

		made->list = gathered.items;
		made->count = gathered.count;

		return made;

	}

	static expression* parse_primary ( parser& item ) {

		token here = item.current;

		if ( here.kind == TOKEN_INTEGER ) {

			expression* made = new_expression ( item, EXPRESSION_LITERAL );

			if ( !made || !read_integer ( here, made->literal ) ) {

				fail_here ( item, "this number is too large" );

				return nullptr;

			}

			advance ( item );

			return made;

		}

		if ( here.kind == TOKEN_REAL ) {

			expression* made = new_expression ( item, EXPRESSION_LITERAL );
			double number = 0;

			if ( !made || !read_real ( here.start, here.size, number ) ) {

				fail_here ( item, "this is not a valid number" );

				return nullptr;

			}

			made->literal = storage::real_value ( number );
			advance ( item );

			return made;

		}

		if ( here.kind == TOKEN_STRING ) {

			expression* made = new_expression ( item, EXPRESSION_LITERAL );
			base::text plain;

			for ( uint32_t i = 1; i + 1 < here.size; i++ ) {

				plain.add ( here.start [ i ] );

				if ( here.start [ i ] == '\'' )
					i++;

			}

			char* kept = item.memory->keep ( plain.value ( ), plain.size );

			if ( !made || !kept ) {

				fail_here ( item, "out of memory" );

				return nullptr;

			}

			made->literal = storage::text_value ( kept, plain.size );
			advance ( item );

			return made;

		}

		if ( here.kind == TOKEN_BLOB ) {

			expression* made = new_expression ( item, EXPRESSION_LITERAL );
			uint32_t size = ( here.size - 3 ) / 2;
			uint8_t* bytes = item.memory->make < uint8_t > ( size + 1 );

			if ( !made || !bytes ) {

				fail_here ( item, "out of memory" );

				return nullptr;

			}

			for ( uint32_t i = 0; i < size; i++ ) {

				char high = base::lower ( here.start [ 2 + i * 2 ] );
				char low = base::lower ( here.start [ 3 + i * 2 ] );

				bytes [ i ] = static_cast < uint8_t > ( ( high <= '9' ? high - '0' : high - 'a' + 10 ) << 4 | ( low <= '9' ? low - '0' : low - 'a' + 10 ) );

			}

			made->literal = storage::blob_value ( bytes, size );
			advance ( item );

			return made;

		}

		if ( here.kind == TOKEN_PARAMETER ) {

			expression* made = new_expression ( item, EXPRESSION_PARAMETER );

			if ( !made )
				return nullptr;

			uint64_t number = 0;

			if ( here.size > 1 && here.start [ 0 ] == '?' ) {

				base::text digits;

				digits.add ( here.start + 1, here.size - 1 );

				if ( !base::read_number ( digits.value ( ), number ) || !number || number > 999 ) {

					fail_here ( item, "a numbered parameter must be from ?1 to ?999" );

					return nullptr;

				}

				made->parameter = static_cast < uint32_t > ( number - 1 );
				item.parameters = base::max < uint32_t > ( item.parameters, made->parameter + 1 );

			} else {

				made->parameter = item.parameters++;
				made->name = here.start [ 0 ] == '?' ? nullptr : item.memory->keep ( here.start, here.size );

			}

			advance ( item );

			return made;

		}

		if ( here.kind == TOKEN_BAD ) {

			fail_here ( item, "this is not valid SQL" );

			return nullptr;

		}

		if ( accept_symbol ( item, "(" ) ) {

			if ( starts_select ( item.current ) ) {

				expression* made = new_expression ( item, EXPRESSION_SUBQUERY );

				if ( !made )
					return nullptr;

				made->query = parse_select ( item );

				if ( !made->query || !expect_symbol ( item, ")" ) )
					return nullptr;

				return made;

			}

			expression* inner = parse_expression ( item );

			if ( !inner || !expect_symbol ( item, ")" ) )
				return nullptr;

			return inner;

		}

		if ( token_is ( here, "null" ) ) {

			expression* made = new_expression ( item, EXPRESSION_LITERAL );

			if ( made )
				made->literal = storage::null_value ( );

			advance ( item );

			return made;

		}

		if ( token_is ( here, "true" ) || token_is ( here, "false" ) ) {

			expression* made = new_expression ( item, EXPRESSION_LITERAL );

			if ( made )
				made->literal = storage::integer_value ( token_is ( here, "true" ) ? 1 : 0 );

			advance ( item );

			return made;

		}

		if ( accept_word ( item, "exists" ) ) {

			expression* made = new_expression ( item, EXPRESSION_EXISTS );

			if ( !made || !expect_symbol ( item, "(" ) )
				return nullptr;

			made->query = parse_select ( item );

			if ( !made->query || !expect_symbol ( item, ")" ) )
				return nullptr;

			return made;

		}

		if ( accept_word ( item, "case" ) )
			return parse_case ( item );

		if ( accept_word ( item, "cast" ) ) {

			expression* made = new_expression ( item, EXPRESSION_CAST );

			if ( !made || !expect_symbol ( item, "(" ) )
				return nullptr;

			made->left = parse_expression ( item );

			if ( !made->left || !expect_word ( item, "as" ) )
				return nullptr;

			base::text type;

			while ( item.current.kind == TOKEN_NAME && !symbol_is ( item.current, ")" ) ) {

				if ( type.size )
					type.add ( ' ' );

				type.add ( item.current.start, item.current.size );
				advance ( item );

			}

			if ( accept_symbol ( item, "(" ) ) {

				while ( item.current.kind == TOKEN_INTEGER || symbol_is ( item.current, "," ) )
					advance ( item );

				if ( !expect_symbol ( item, ")" ) )
					return nullptr;

			}

			if ( !type.size ) {

				fail_here ( item, "expected a type name" );

				return nullptr;

			}

			made->cast = affinity_of ( type.value ( ) );
			made->name = item.memory->keep ( type.value ( ), type.size );

			if ( !expect_symbol ( item, ")" ) )
				return nullptr;

			return made;

		}

		if ( here.kind == TOKEN_NAME && ( here.start [ 0 ] == '"' || here.start [ 0 ] == '`' || here.start [ 0 ] == '[' || !reserved_word ( here ) ) ) {

			const char* first = take_name ( item, "a name" );

			if ( !first )
				return nullptr;

			if ( accept_symbol ( item, "(" ) ) {

				expression* made = new_expression ( item, EXPRESSION_FUNCTION );

				if ( !made )
					return nullptr;

				made->name = first;

				if ( accept_symbol ( item, "*" ) )
					made->star = true;
				else if ( !symbol_is ( item.current, ")" ) ) {

					made->distinct = accept_word ( item, "distinct" );

					if ( !parse_list ( item, made->list, made->count ) )
						return nullptr;

				}

				if ( !expect_symbol ( item, ")" ) )
					return nullptr;

				return made;

			}

			expression* made = new_expression ( item, EXPRESSION_COLUMN );

			if ( !made )
				return nullptr;

			if ( accept_symbol ( item, "." ) ) {

				made->table = first;
				made->name = take_name ( item, "a column name" );

				if ( !made->name )
					return nullptr;

			} else
				made->name = first;

			return made;

		}

		fail_here ( item, "expected a value or an expression" );

		return nullptr;

	}

	static expression* parse_unary ( parser& item ) {

		if ( ++item.depth > PARSE_DEPTH ) {

			fail_here ( item, "this expression is nested too deeply" );

			return nullptr;

		}

		operator_kind op = OPERATOR_NONE;

		if ( symbol_is ( item.current, "-" ) )
			op = OPERATOR_NEGATE;
		else if ( symbol_is ( item.current, "+" ) )
			op = OPERATOR_PLUS;
		else if ( symbol_is ( item.current, "~" ) )
			op = OPERATOR_BIT_NOT;

		expression* result = nullptr;

		if ( op != OPERATOR_NONE ) {

			expression* made = new_expression ( item, EXPRESSION_UNARY );

			advance ( item );

			if ( made ) {

				made->op = op;
				made->left = parse_unary ( item );
				result = made->left ? made : nullptr;

			}

		} else
			result = parse_primary ( item );

		if ( result && accept_word ( item, "collate" ) && !take_name ( item, "a collation name" ) )
			result = nullptr;

		item.depth--;

		return result;

	}

	static expression* binary ( parser& item, operator_kind op, expression* left, expression* right ) {

		if ( !left || !right )
			return nullptr;

		expression* made = new_expression ( item, EXPRESSION_BINARY );

		if ( !made )
			return nullptr;

		made->op = op;
		made->left = left;
		made->right = right;
		made->offset = left->offset;

		return made;

	}

	static expression* parse_concat ( parser& item ) {

		expression* left = parse_unary ( item );

		while ( left && symbol_is ( item.current, "||" ) ) {

			advance ( item );
			left = binary ( item, OPERATOR_CONCAT, left, parse_unary ( item ) );

		}

		return left;

	}

	static expression* parse_product ( parser& item ) {

		expression* left = parse_concat ( item );

		while ( left ) {

			operator_kind op = symbol_is ( item.current, "*" ) ? OPERATOR_MULTIPLY : symbol_is ( item.current, "/" ) ? OPERATOR_DIVIDE : symbol_is ( item.current, "%" ) ? OPERATOR_MODULO : OPERATOR_NONE;

			if ( op == OPERATOR_NONE )
				break;

			advance ( item );
			left = binary ( item, op, left, parse_concat ( item ) );

		}

		return left;

	}

	static expression* parse_sum ( parser& item ) {

		expression* left = parse_product ( item );

		while ( left ) {

			operator_kind op = symbol_is ( item.current, "+" ) ? OPERATOR_ADD : symbol_is ( item.current, "-" ) ? OPERATOR_SUBTRACT : OPERATOR_NONE;

			if ( op == OPERATOR_NONE )
				break;

			advance ( item );
			left = binary ( item, op, left, parse_product ( item ) );

		}

		return left;

	}

	static expression* parse_bits ( parser& item ) {

		expression* left = parse_sum ( item );

		while ( left ) {

			operator_kind op = symbol_is ( item.current, "&" ) ? OPERATOR_BIT_AND : symbol_is ( item.current, "|" ) ? OPERATOR_BIT_OR : symbol_is ( item.current, "<<" ) ? OPERATOR_SHIFT_LEFT : symbol_is ( item.current, ">>" ) ? OPERATOR_SHIFT_RIGHT : OPERATOR_NONE;

			if ( op == OPERATOR_NONE )
				break;

			advance ( item );
			left = binary ( item, op, left, parse_sum ( item ) );

		}

		return left;

	}

	static expression* parse_comparison ( parser& item ) {

		expression* left = parse_bits ( item );

		while ( left ) {

			operator_kind op = symbol_is ( item.current, "<" ) ? OPERATOR_LESS : symbol_is ( item.current, "<=" ) ? OPERATOR_LESS_EQUAL : symbol_is ( item.current, ">" ) ? OPERATOR_GREATER : symbol_is ( item.current, ">=" ) ? OPERATOR_GREATER_EQUAL : OPERATOR_NONE;

			if ( op == OPERATOR_NONE )
				break;

			advance ( item );
			left = binary ( item, op, left, parse_bits ( item ) );

		}

		return left;

	}

	static expression* parse_equality ( parser& item ) {

		expression* left = parse_comparison ( item );

		while ( left ) {

			if ( symbol_is ( item.current, "=" ) || symbol_is ( item.current, "==" ) || symbol_is ( item.current, "!=" ) || symbol_is ( item.current, "<>" ) ) {

				operator_kind op = symbol_is ( item.current, "=" ) || symbol_is ( item.current, "==" ) ? OPERATOR_EQUAL : OPERATOR_NOT_EQUAL;

				advance ( item );
				left = binary ( item, op, left, parse_comparison ( item ) );

				continue;

			}

			bool is_null = token_is ( item.current, "isnull" );

			if ( is_null || token_is ( item.current, "notnull" ) ) {

				expression* made = new_expression ( item, EXPRESSION_NULL_TEST );

				if ( !made )
					return nullptr;

				advance ( item );
				made->left = left;
				made->negated = !is_null;
				left = made;

				continue;

			}

			if ( accept_word ( item, "is" ) ) {

				bool negated = accept_word ( item, "not" );

				if ( accept_word ( item, "null" ) ) {

					expression* made = new_expression ( item, EXPRESSION_NULL_TEST );

					if ( !made )
						return nullptr;

					made->left = left;
					made->negated = negated;
					left = made;

				} else
					left = binary ( item, negated ? OPERATOR_IS_NOT : OPERATOR_IS, left, parse_comparison ( item ) );

				continue;

			}

			bool negated = false;
			lexer saved = item.lex;
			token saved_token = item.current;

			if ( accept_word ( item, "not" ) ) {

				if ( !token_is ( item.current, "in" ) && !token_is ( item.current, "like" ) && !token_is ( item.current, "glob" ) && !token_is ( item.current, "between" ) && !token_is ( item.current, "null" ) ) {

					item.lex = saved;
					item.current = saved_token;

					break;

				}

				negated = true;

				if ( accept_word ( item, "null" ) ) {

					expression* made = new_expression ( item, EXPRESSION_NULL_TEST );

					if ( !made )
						return nullptr;

					made->left = left;
					made->negated = true;
					left = made;

					continue;

				}

			}

			if ( accept_word ( item, "in" ) ) {

				expression* made = new_expression ( item, EXPRESSION_IN );

				if ( !made || !expect_symbol ( item, "(" ) )
					return nullptr;

				made->left = left;
				made->negated = negated;

				if ( starts_select ( item.current ) ) {

					made->query = parse_select ( item );

					if ( !made->query )
						return nullptr;

				} else if ( !symbol_is ( item.current, ")" ) && !parse_list ( item, made->list, made->count ) )
					return nullptr;

				if ( !expect_symbol ( item, ")" ) )
					return nullptr;

				left = made;

				continue;

			}

			if ( token_is ( item.current, "like" ) || token_is ( item.current, "glob" ) ) {

				expression* made = new_expression ( item, EXPRESSION_LIKE );

				if ( !made )
					return nullptr;

				made->star = token_is ( item.current, "glob" );
				advance ( item );
				made->left = left;
				made->negated = negated;
				made->right = parse_comparison ( item );

				if ( !made->right )
					return nullptr;

				if ( accept_word ( item, "escape" ) ) {

					made->third = parse_comparison ( item );

					if ( !made->third )
						return nullptr;

				}

				left = made;

				continue;

			}

			if ( accept_word ( item, "between" ) ) {

				expression* made = new_expression ( item, EXPRESSION_BETWEEN );

				if ( !made )
					return nullptr;

				made->left = left;
				made->negated = negated;
				made->right = parse_comparison ( item );

				if ( !made->right || !expect_word ( item, "and" ) )
					return nullptr;

				made->third = parse_comparison ( item );

				if ( !made->third )
					return nullptr;

				left = made;

				continue;

			}

			break;

		}

		return left;

	}

	static expression* parse_not ( parser& item ) {

		if ( accept_word ( item, "not" ) ) {

			if ( ++item.depth > PARSE_DEPTH ) {

				fail_here ( item, "this expression is nested too deeply" );

				return nullptr;

			}

			expression* made = new_expression ( item, EXPRESSION_UNARY );

			if ( !made )
				return nullptr;

			made->op = OPERATOR_NOT;
			made->left = parse_not ( item );
			item.depth--;

			return made->left ? made : nullptr;

		}

		return parse_equality ( item );

	}

	static expression* parse_and ( parser& item ) {

		expression* left = parse_not ( item );

		while ( left && accept_word ( item, "and" ) )
			left = binary ( item, OPERATOR_AND, left, parse_not ( item ) );

		return left;

	}

	static expression* parse_or ( parser& item ) {

		expression* left = parse_and ( item );

		while ( left && accept_word ( item, "or" ) )
			left = binary ( item, OPERATOR_OR, left, parse_and ( item ) );

		return left;

	}

	expression* parse_expression ( parser& item ) {

		if ( ++item.depth > PARSE_DEPTH ) {

			fail_here ( item, "this expression is nested too deeply" );

			return nullptr;

		}

		expression* made = parse_or ( item );

		item.depth--;

		return made;

	}

	affinity_kind affinity_of ( const char* type ) {

		base::text folded;

		for ( const char* at = type; at && *at; at++ )
			folded.add ( base::lower ( *at ) );

		const char* text = folded.value ( );

		auto has = [ & ] ( const char* part ) {

			size_t size = base::length ( part );

			for ( size_t i = 0; i + size <= folded.size; i++ )
				if ( base::same_bytes ( text + i, part, size ) )
					return true;

			return false;

		};

		if ( !folded.size )
			return AFFINITY_BLOB;

		if ( has ( "int" ) )
			return AFFINITY_INTEGER;

		if ( has ( "char" ) || has ( "clob" ) || has ( "text" ) )
			return AFFINITY_TEXT;

		if ( has ( "blob" ) )
			return AFFINITY_BLOB;

		if ( has ( "real" ) || has ( "floa" ) || has ( "doub" ) )
			return AFFINITY_REAL;

		return AFFINITY_NUMERIC;

	}

	const char* operator_text ( operator_kind op ) {

		switch ( op ) {

			case OPERATOR_ADD: return "+";
			case OPERATOR_SUBTRACT: return "-";
			case OPERATOR_MULTIPLY: return "*";
			case OPERATOR_DIVIDE: return "/";
			case OPERATOR_MODULO: return "%";
			case OPERATOR_CONCAT: return "||";
			case OPERATOR_EQUAL: return "=";
			case OPERATOR_NOT_EQUAL: return "<>";
			case OPERATOR_LESS: return "<";
			case OPERATOR_LESS_EQUAL: return "<=";
			case OPERATOR_GREATER: return ">";
			case OPERATOR_GREATER_EQUAL: return ">=";
			case OPERATOR_IS: return "IS";
			case OPERATOR_IS_NOT: return "IS NOT";
			case OPERATOR_AND: return "AND";
			case OPERATOR_OR: return "OR";
			case OPERATOR_NOT: return "NOT";
			case OPERATOR_NEGATE: return "-";
			case OPERATOR_PLUS: return "+";
			case OPERATOR_BIT_AND: return "&";
			case OPERATOR_BIT_OR: return "|";
			case OPERATOR_BIT_NOT: return "~";
			case OPERATOR_SHIFT_LEFT: return "<<";
			case OPERATOR_SHIFT_RIGHT: return ">>";
			default: return "";

		}

	}

}
