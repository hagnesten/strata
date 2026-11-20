#include "server/server.hh"

namespace server {

	static bool space_at ( const char* text, size_t at, size_t end ) {

		return at < end && ( text [ at ] == ' ' || text [ at ] == '\t' || text [ at ] == '\r' || text [ at ] == '\n' );

	}

	static size_t skip_blank ( const char* text, size_t at, size_t end ) {

		for ( ;; ) {

			while ( space_at ( text, at, end ) )
				at++;

			if ( at + 1 < end && text [ at ] == '-' && text [ at + 1 ] == '-' ) {

				while ( at < end && text [ at ] != '\n' )
					at++;

				continue;

			}

			if ( at + 1 < end && text [ at ] == '/' && text [ at + 1 ] == '*' ) {

				at += 2;

				while ( at + 1 < end && !( text [ at ] == '*' && text [ at + 1 ] == '/' ) )
					at++;

				at = at + 2 < end ? at + 2 : end;

				continue;

			}

			return at;

		}

	}

	static bool word_part ( char c ) {

		return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_';

	}

	static bool word_here ( const char* text, size_t at, size_t end, const char* word ) {

		size_t size = base::length ( word );

		if ( at + size > end || !base::same_folded_size ( text + at, word, size ) )
			return false;

		return at + size == end || !word_part ( text [ at + size ] );

	}

	static size_t statement_end ( const char* text, size_t at, size_t end, const char* stop ) {

		uint32_t depth = 0;

		while ( at < end ) {

			char c = text [ at ];

			if ( c == '\'' || c == '"' ) {

				at++;

				while ( at < end && !( text [ at ] == c && !( at + 1 < end && text [ at + 1 ] == c ) ) )
					at += text [ at ] == c ? 2 : 1;

				at++;

				continue;

			}

			if ( c == '(' )
				depth++;
			else if ( c == ')' && depth )
				depth--;
			else if ( !depth && c == ';' && !stop )
				return at;
			else if ( !depth && stop && ( at == 0 || !word_part ( text [ at - 1 ] ) ) && word_here ( text, at, end, stop ) )
				return at;

			at++;

		}

		return end;

	}

	static bool run_text ( server_connection& link, const char* text, size_t size, base::text& problem ) {

		base::text sql;

		sql.add ( text, size );

		if ( strata_exec ( link.db, sql.value ( ) ) == STRATA_OK )
			return true;

		problem.add ( strata_error ( link.db ) );

		return false;

	}

	static bool test_condition ( server_connection& link, const char* text, size_t size, bool& truth, base::text& problem ) {

		base::text sql;
		strata_stmt* stmt = nullptr;

		sql.add ( "SELECT CASE WHEN ( " ).add ( text, size ).add ( " ) THEN 1 ELSE 0 END" );

		if ( strata_prepare ( link.db, sql.value ( ), -1, &stmt, nullptr ) != STRATA_OK ) {

			problem.add ( strata_error ( link.db ) );

			return false;

		}

		int code = strata_step ( stmt );

		truth = code == STRATA_ROW && strata_column_int64 ( stmt, 0 ) != 0;

		if ( code != STRATA_ROW && code != STRATA_DONE )
			problem.add ( strata_error ( link.db ) );

		strata_finalize ( stmt );

		return code == STRATA_ROW || code == STRATA_DONE;

	}

	static bool run_statements ( server_connection& link, const char* text, size_t& at, size_t end, bool live, base::text& problem, uint32_t depth );

	static bool run_if ( server_connection& link, const char* text, size_t& at, size_t end, bool live, base::text& problem, uint32_t depth ) {

		bool taken = false;

		for ( ;; ) {

			size_t then = statement_end ( text, at, end, "then" );

			if ( then >= end ) {

				problem.add ( "IF without THEN in a DO block" );

				return false;

			}

			bool truth = false;

			if ( live && !taken && !test_condition ( link, text + at, then - at, truth, problem ) )
				return false;

			at = then + 4;

			if ( !run_statements ( link, text, at, end, live && !taken && truth, problem, depth + 1 ) )
				return false;

			taken = taken || truth;
			at = skip_blank ( text, at, end );

			if ( word_here ( text, at, end, "elsif" ) || word_here ( text, at, end, "elseif" ) ) {

				at += word_here ( text, at, end, "elsif" ) ? 5 : 6;

				continue;

			}

			if ( word_here ( text, at, end, "else" ) ) {

				at += 4;

				if ( !run_statements ( link, text, at, end, live && !taken, problem, depth + 1 ) )
					return false;

				at = skip_blank ( text, at, end );

			}

			if ( !word_here ( text, at, end, "end" ) ) {

				problem.add ( "IF without END IF in a DO block" );

				return false;

			}

			at = skip_blank ( text, at + 3, end );

			if ( !word_here ( text, at, end, "if" ) ) {

				problem.add ( "IF without END IF in a DO block" );

				return false;

			}

			at = skip_blank ( text, at + 2, end );

			if ( at < end && text [ at ] == ';' )
				at++;

			return true;

		}

	}

	static bool run_statements ( server_connection& link, const char* text, size_t& at, size_t end, bool live, base::text& problem, uint32_t depth ) {

		if ( depth > 32 ) {

			problem.add ( "a DO block nests too deeply" );

			return false;

		}

		for ( ;; ) {

			at = skip_blank ( text, at, end );

			if ( at >= end || word_here ( text, at, end, "end" ) || word_here ( text, at, end, "else" ) || word_here ( text, at, end, "elsif" ) || word_here ( text, at, end, "elseif" ) || word_here ( text, at, end, "exception" ) )
				return true;

			if ( text [ at ] == ';' ) {

				at++;

				continue;

			}

			if ( word_here ( text, at, end, "if" ) ) {

				at += 2;

				if ( !run_if ( link, text, at, end, live, problem, depth ) )
					return false;

				continue;

			}

			if ( word_here ( text, at, end, "begin" ) ) {

				at += 5;

				if ( !run_statements ( link, text, at, end, live, problem, depth + 1 ) )
					return false;

				at = skip_blank ( text, at, end );

				if ( !word_here ( text, at, end, "end" ) ) {

					problem.add ( "BEGIN without END in a DO block" );

					return false;

				}

				at += 3;

				continue;

			}

			size_t stop = statement_end ( text, at, end, nullptr );
			bool quiet = word_here ( text, at, end, "raise" ) || word_here ( text, at, end, "null" ) || word_here ( text, at, end, "perform" );

			if ( live && !quiet && !run_text ( link, text + at, stop - at, problem ) )
				return false;

			if ( live && word_here ( text, at, end, "perform" ) ) {

				base::text select;

				select.add ( "SELECT " ).add ( text + at + 7, stop - at - 7 );

				if ( !run_text ( link, select.value ( ), select.size, problem ) )
					return false;

			}

			at = stop < end ? stop + 1 : end;

		}

	}

	bool dollar_body ( const char* sql, size_t size, size_t& body, size_t& body_size, size_t& after ) {

		size_t at = skip_blank ( sql, 0, size );

		if ( !word_here ( sql, at, size, "do" ) )
			return false;

		at = skip_blank ( sql, at + 2, size );

		if ( word_here ( sql, at, size, "language" ) ) {

			at = skip_blank ( sql, at + 8, size );

			while ( at < size && word_part ( sql [ at ] ) )
				at++;

			at = skip_blank ( sql, at, size );

		}

		if ( at >= size || sql [ at ] != '$' )
			return false;

		size_t tag = at;

		at++;

		while ( at < size && word_part ( sql [ at ] ) )
			at++;

		if ( at >= size || sql [ at ] != '$' )
			return false;

		size_t tag_size = at + 1 - tag;

		body = at + 1;

		for ( size_t i = body; i + tag_size <= size; i++ ) {

			if ( base::same_bytes ( sql + i, sql + tag, tag_size ) ) {

				body_size = i - body;
				after = skip_blank ( sql, i + tag_size, size );

				if ( word_here ( sql, after, size, "language" ) ) {

					after = skip_blank ( sql, after + 8, size );

					while ( after < size && word_part ( sql [ after ] ) )
						after++;

				}

				return true;

			}

		}

		return false;

	}

	bool run_do_block ( server_connection& link, const char* body, size_t size, base::text& problem ) {

		size_t at = skip_blank ( body, 0, size );

		if ( word_here ( body, at, size, "declare" ) ) {

			problem.add ( "DECLARE sections in DO blocks are not supported" );

			return false;

		}

		bool outer = !strata_in_transaction ( link.db );

		if ( outer && strata_exec ( link.db, "BEGIN" ) != STRATA_OK ) {

			problem.add ( strata_error ( link.db ) );

			return false;

		}

		bool fine = run_statements ( link, body, at, size, true, problem, 0 );

		if ( fine ) {

			at = skip_blank ( body, at, size );

			if ( at < size && !word_here ( body, at, size, "end" ) ) {

				problem.add ( "this part of the DO block is not supported" );
				fine = false;

			}

		}

		if ( outer )
			strata_exec ( link.db, fine ? "COMMIT" : "ROLLBACK" );

		return fine;

	}

}
