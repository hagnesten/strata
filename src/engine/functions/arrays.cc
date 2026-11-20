#include "engine/run/run.hh"

namespace engine {

	static bool element_matches ( const storage::value& probe, const char* text, size_t size, bool& matched ) {

		matched = false;

		if ( probe.kind == storage::VALUE_INTEGER || probe.kind == storage::VALUE_REAL ) {

			storage::value number;

			if ( !read_number_text ( reinterpret_cast < const uint8_t* > ( text ), size, number, true ) )
				return true;

			matched = storage::compare_values ( probe, number ) == 0;

			return true;

		}

		matched = probe.size == size && ( !size || base::same_bytes ( probe.bytes, text, size ) );

		return true;

	}

	static size_t skip_spaces ( const char* text, size_t at, size_t end ) {

		while ( at < end && ( text [ at ] == ' ' || text [ at ] == '\t' || text [ at ] == '\r' || text [ at ] == '\n' ) )
			at++;

		return at;

	}

	bool array_contains ( run_context& ctx, const storage::value& probe, const storage::value& list, storage::value& out ) {

		out = storage::null_value ( );

		if ( list.kind == storage::VALUE_NULL || probe.kind == storage::VALUE_NULL )
			return true;

		storage::value written = list.kind == storage::VALUE_TEXT ? list : text_of ( *ctx.scratch, list );
		storage::value wanted = probe.kind == storage::VALUE_BLOB ? text_of ( *ctx.scratch, probe ) : probe;
		const char* text = reinterpret_cast < const char* > ( written.bytes );
		size_t end = written.size;
		size_t at = skip_spaces ( text, 0, end );

		if ( at >= end || ( text [ at ] != '{' && text [ at ] != '[' ) )
			return fail_run ( ctx, "malformed array literal" );

		bool json = text [ at ] == '[';
		char close = json ? ']' : '}';
		bool saw_null = false;
		base::text element;

		at = skip_spaces ( text, at + 1, end );

		if ( at < end && text [ at ] == close ) {

			out = storage::integer_value ( 0 );

			return true;

		}

		while ( at < end ) {

			element.clear ( );
			at = skip_spaces ( text, at, end );

			bool quoted = at < end && text [ at ] == '"';
			bool null = false;

			if ( quoted ) {

				at++;

				while ( at < end && text [ at ] != '"' ) {

					if ( text [ at ] == '\\' && at + 1 < end ) {

						at++;

						char c = text [ at ];

						if ( json && c == 'n' )
							c = '\n';
						else if ( json && c == 't' )
							c = '\t';
						else if ( json && c == 'r' )
							c = '\r';

						element.add ( c );
						at++;

						continue;

					}

					element.add ( text [ at++ ] );

				}

				if ( at >= end )
					return fail_run ( ctx, "malformed array literal" );

				at++;

			} else {

				size_t start = at;

				while ( at < end && text [ at ] != ',' && text [ at ] != close )
					at++;

				size_t stop = at;

				while ( stop > start && ( text [ stop - 1 ] == ' ' || text [ stop - 1 ] == '\t' ) )
					stop--;

				null = stop - start == 4 && base::same_folded_size ( text + start, "null", 4 );
				element.add ( text + start, stop - start );

			}

			if ( null )
				saw_null = true;
			else {

				bool matched = false;

				element_matches ( wanted, element.value ( ), element.size, matched );

				if ( matched ) {

					out = storage::integer_value ( 1 );

					return true;

				}

			}

			at = skip_spaces ( text, at, end );

			if ( at < end && text [ at ] == ',' ) {

				at++;

				continue;

			}

			if ( at < end && text [ at ] == close )
				break;

			return fail_run ( ctx, "malformed array literal" );

		}

		out = saw_null ? storage::null_value ( ) : storage::integer_value ( 0 );

		return true;

	}

}
