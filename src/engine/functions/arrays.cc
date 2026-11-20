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

	static bool read_items ( run_context& ctx, const storage::value& list, array_item*& items, uint32_t& count ) {

		storage::value written = list.kind == storage::VALUE_TEXT ? list : text_of ( *ctx.scratch, list );

		if ( !split_array ( *ctx.scratch, written.bytes, written.size, items, count ) )
			return fail_run ( ctx, "malformed array literal" );

		return true;

	}

	bool split_array ( base::arena& memory, const uint8_t* bytes, size_t size, array_item*& items, uint32_t& count ) {

		items = nullptr;
		count = 0;

		const char* text = reinterpret_cast < const char* > ( bytes );
		size_t end = size;
		size_t at = skip_spaces ( text, 0, end );

		if ( at >= end || ( text [ at ] != '{' && text [ at ] != '[' ) )
			return false;

		bool json = text [ at ] == '[';
		char close = json ? ']' : '}';
		uint32_t room = 8;

		items = memory.make < array_item > ( room );

		if ( !items )
			return false;

		at = skip_spaces ( text, at + 1, end );

		if ( at < end && text [ at ] == close )
			return true;

		base::text element;

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
					return false;

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

			if ( count == room ) {

				array_item* grown = memory.make < array_item > ( room * 2 );

				if ( !grown )
					return false;

				base::copy ( grown, items, sizeof ( array_item ) * count );
				items = grown;
				room *= 2;

			}

			array_item& made = items [ count++ ];

			made.null = null;
			made.size = element.size;
			made.text = element.size ? memory.keep ( element.value ( ), element.size ) : "";

			if ( !made.text )
				return false;

			at = skip_spaces ( text, at, end );

			if ( at < end && text [ at ] == ',' ) {

				at++;

				continue;

			}

			if ( at < end && text [ at ] == close )
				return true;

			return false;

		}

		return false;

	}

	static bool same_item ( const array_item& a, const array_item& b ) {

		if ( a.null || b.null )
			return false;

		if ( a.size == b.size && ( !a.size || base::same_bytes ( a.text, b.text, a.size ) ) )
			return true;

		storage::value x;
		storage::value y;

		return read_number_text ( reinterpret_cast < const uint8_t* > ( a.text ), a.size, x, true ) && read_number_text ( reinterpret_cast < const uint8_t* > ( b.text ), b.size, y, true ) && storage::compare_values ( x, y ) == 0;

	}

	bool array_contains ( run_context& ctx, const storage::value& probe, const storage::value& list, storage::value& out ) {

		out = storage::null_value ( );

		if ( list.kind == storage::VALUE_NULL || probe.kind == storage::VALUE_NULL )
			return true;

		storage::value wanted = probe.kind == storage::VALUE_BLOB ? text_of ( *ctx.scratch, probe ) : probe;
		array_item* items = nullptr;
		uint32_t count = 0;
		bool saw_null = false;

		if ( !read_items ( ctx, list, items, count ) )
			return false;

		for ( uint32_t i = 0; i < count; i++ ) {

			if ( items [ i ].null ) {

				saw_null = true;

				continue;

			}

			bool matched = false;

			element_matches ( wanted, items [ i ].text, items [ i ].size, matched );

			if ( matched ) {

				out = storage::integer_value ( 1 );

				return true;

			}

		}

		if ( !saw_null )
			out = storage::integer_value ( 0 );

		return true;

	}

	bool array_relation ( run_context& ctx, const storage::value& a, const storage::value& b, bool overlap, storage::value& out ) {

		out = storage::null_value ( );

		if ( a.kind == storage::VALUE_NULL || b.kind == storage::VALUE_NULL )
			return true;

		array_item* left = nullptr;
		array_item* right = nullptr;
		uint32_t left_count = 0;
		uint32_t right_count = 0;

		if ( !read_items ( ctx, a, left, left_count ) || !read_items ( ctx, b, right, right_count ) )
			return false;

		bool result = !overlap;

		for ( uint32_t r = 0; r < right_count; r++ ) {

			bool found = false;

			for ( uint32_t l = 0; l < left_count && !found; l++ )
				found = same_item ( left [ l ], right [ r ] );

			if ( overlap && found ) {

				result = true;

				break;

			}

			if ( !overlap && !found ) {

				result = false;

				break;

			}

		}

		out = storage::integer_value ( result ? 1 : 0 );
		out.subtype = LITERAL_BOOLEAN;

		return true;

	}

	bool array_size ( run_context& ctx, const storage::value& list, bool empty_null, storage::value& out ) {

		out = storage::null_value ( );

		if ( list.kind == storage::VALUE_NULL )
			return true;

		array_item* items = nullptr;
		uint32_t count = 0;

		if ( !read_items ( ctx, list, items, count ) )
			return false;

		if ( !count && empty_null )
			return true;

		out = storage::integer_value ( count );

		return true;

	}

	static bool plain_element ( const uint8_t* bytes, size_t size ) {

		if ( !size || ( size == 4 && base::same_folded_size ( reinterpret_cast < const char* > ( bytes ), "null", 4 ) ) )
			return false;

		for ( size_t i = 0; i < size; i++ ) {

			uint8_t c = bytes [ i ];

			if ( c == ',' || c == '{' || c == '}' || c == '"' || c == '\\' || c == ' ' || c == '\t' || c == '\r' || c == '\n' )
				return false;

		}

		return true;

	}

	bool array_build ( run_context& ctx, const storage::value* args, uint32_t count, storage::value& out ) {

		base::text made;

		made.add ( '{' );

		for ( uint32_t i = 0; i < count; i++ ) {

			if ( i )
				made.add ( ',' );

			const storage::value& item = args [ i ];

			if ( item.kind == storage::VALUE_NULL ) {

				made.add ( "NULL" );

				continue;

			}

			storage::value text = item.kind == storage::VALUE_TEXT ? item : text_of ( *ctx.scratch, item );

			if ( item.kind != storage::VALUE_TEXT && item.kind != storage::VALUE_BLOB ) {

				made.add ( reinterpret_cast < const char* > ( text.bytes ), text.size );

				continue;

			}

			if ( plain_element ( text.bytes, text.size ) ) {

				made.add ( reinterpret_cast < const char* > ( text.bytes ), text.size );

				continue;

			}

			made.add ( '"' );

			for ( uint32_t k = 0; k < text.size; k++ ) {

				char c = static_cast < char > ( text.bytes [ k ] );

				if ( c == '"' || c == '\\' )
					made.add ( '\\' );

				made.add ( c );

			}

			made.add ( '"' );

		}

		made.add ( '}' );

		char* kept = ctx.scratch->keep ( made.value ( ), made.size );

		if ( !kept )
			return fail_run ( ctx, "out of memory" );

		out = storage::text_value ( kept, made.size );

		return true;

	}

}
