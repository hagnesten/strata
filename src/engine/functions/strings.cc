#include "engine/run/run.hh"

#define STRING_RESULT_LIMIT ( 256u * 1024u * 1024u )

namespace engine {

	static bool char_offsets ( run_context& ctx, const storage::value& text, size_t*& offsets, size_t& count ) {

		offsets = ctx.scratch->make < size_t > ( text.size + 1 );

		if ( !offsets )
			return fail_run ( ctx, "out of memory" );

		count = 0;

		size_t at = 0;

		while ( at < text.size ) {

			offsets [ count++ ] = at;
			base::next_code ( text.bytes, text.size, at );

		}

		offsets [ count ] = text.size;

		return true;

	}

	static bool finish ( run_context& ctx, const base::text& made, storage::value& out ) {

		if ( made.size > STRING_RESULT_LIMIT )
			return fail_run ( ctx, "the requested string is too long" );

		char* kept = ctx.scratch->keep ( made.value ( ), made.size );

		if ( !kept && made.size )
			return fail_run ( ctx, "out of memory" );

		out = storage::text_value ( kept ? kept : "", made.size );

		return true;

	}

	static void add_chars ( base::text& out, const storage::value& text, const size_t* offsets, size_t from, size_t to ) {

		if ( to > from )
			out.add ( reinterpret_cast < const char* > ( text.bytes ) + offsets [ from ], offsets [ to ] - offsets [ from ] );

	}

	static bool pad ( run_context& ctx, const storage::value* args, uint32_t count, bool left, storage::value& out ) {

		storage::value text = text_of ( *ctx.scratch, args [ 0 ] );
		storage::value fill = count > 2 ? text_of ( *ctx.scratch, args [ 2 ] ) : storage::text_value ( " ", 1 );
		int64_t wanted = 0;
		size_t* offsets = nullptr;
		size_t* fill_offsets = nullptr;
		size_t length = 0;
		size_t fill_length = 0;
		base::text made;

		integer_of ( args [ 1 ], wanted );

		if ( wanted <= 0 )
			return finish ( ctx, made, out );

		if ( static_cast < uint64_t > ( wanted ) > STRING_RESULT_LIMIT )
			return fail_run ( ctx, "the requested string is too long" );

		if ( !char_offsets ( ctx, text, offsets, length ) || !char_offsets ( ctx, fill, fill_offsets, fill_length ) )
			return false;

		size_t target = static_cast < size_t > ( wanted );

		if ( length >= target || !fill_length ) {

			add_chars ( made, text, offsets, 0, length < target ? length : target );

			return finish ( ctx, made, out );

		}

		if ( !left )
			add_chars ( made, text, offsets, 0, length );

		for ( size_t added = 0; added < target - length; added++ )
			add_chars ( made, fill, fill_offsets, added % fill_length, added % fill_length + 1 );

		if ( left )
			add_chars ( made, text, offsets, 0, length );

		return finish ( ctx, made, out );

	}

	static bool split_part ( run_context& ctx, const storage::value* args, storage::value& out ) {

		storage::value text = text_of ( *ctx.scratch, args [ 0 ] );
		storage::value delimiter = text_of ( *ctx.scratch, args [ 1 ] );
		int64_t field = 0;
		base::text made;

		integer_of ( args [ 2 ], field );

		if ( !field )
			return fail_run ( ctx, "field position must not be zero" );

		if ( !delimiter.size ) {

			if ( field == 1 || field == -1 )
				made.add ( reinterpret_cast < const char* > ( text.bytes ), text.size );

			return finish ( ctx, made, out );

		}

		size_t starts [ 1024 ];
		size_t ends [ 1024 ];
		size_t fields = 0;
		size_t start = 0;

		for ( size_t at = 0; at + delimiter.size <= text.size && fields < 1023; ) {

			if ( base::same_bytes ( text.bytes + at, delimiter.bytes, delimiter.size ) ) {

				starts [ fields ] = start;
				ends [ fields++ ] = at;
				at += delimiter.size;
				start = at;

			} else
				at++;

		}

		starts [ fields ] = start;
		ends [ fields++ ] = text.size;

		int64_t chosen = field > 0 ? field - 1 : static_cast < int64_t > ( fields ) + field;

		if ( chosen >= 0 && chosen < static_cast < int64_t > ( fields ) )
			made.add ( reinterpret_cast < const char* > ( text.bytes ) + starts [ chosen ], ends [ chosen ] - starts [ chosen ] );

		return finish ( ctx, made, out );

	}

	static bool initcap ( run_context& ctx, const storage::value& text, storage::value& out ) {

		base::text made;
		bool word = false;

		for ( size_t i = 0; i < text.size; i++ ) {

			char c = static_cast < char > ( text.bytes [ i ] );
			bool letter = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || static_cast < uint8_t > ( c ) >= 0x80;

			made.add ( letter ? ( word ? base::lower ( c ) : base::upper ( c ) ) : c );
			word = letter;

		}

		return finish ( ctx, made, out );

	}

	static bool translate ( run_context& ctx, const storage::value* args, storage::value& out ) {

		storage::value text = text_of ( *ctx.scratch, args [ 0 ] );
		storage::value from = text_of ( *ctx.scratch, args [ 1 ] );
		storage::value to = text_of ( *ctx.scratch, args [ 2 ] );
		size_t *text_at = nullptr, *from_at = nullptr, *to_at = nullptr;
		size_t text_count = 0, from_count = 0, to_count = 0;
		base::text made;

		if ( !char_offsets ( ctx, text, text_at, text_count ) || !char_offsets ( ctx, from, from_at, from_count ) || !char_offsets ( ctx, to, to_at, to_count ) )
			return false;

		for ( size_t i = 0; i < text_count; i++ ) {

			size_t size = text_at [ i + 1 ] - text_at [ i ];
			size_t hit = from_count;

			for ( size_t k = 0; k < from_count && hit == from_count; k++ )
				if ( from_at [ k + 1 ] - from_at [ k ] == size && base::same_bytes ( from.bytes + from_at [ k ], text.bytes + text_at [ i ], size ) )
					hit = k;

			if ( hit == from_count )
				add_chars ( made, text, text_at, i, i + 1 );
			else if ( hit < to_count )
				add_chars ( made, to, to_at, hit, hit + 1 );

		}

		return finish ( ctx, made, out );

	}

	bool string_function ( run_context& ctx, int32_t kind, const storage::value* args, uint32_t count, storage::value& out ) {

		out = storage::null_value ( );

		for ( uint32_t i = 0; i < count; i++ )
			if ( args [ i ].kind == storage::VALUE_NULL )
				return true;

		switch ( kind ) {

			case FUNCTION_REPEAT: {

				storage::value text = text_of ( *ctx.scratch, args [ 0 ] );
				int64_t times = 0;
				base::text made;

				integer_of ( args [ 1 ], times );

				if ( times > 0 && text.size && static_cast < uint64_t > ( times ) * text.size > STRING_RESULT_LIMIT )
					return fail_run ( ctx, "the requested string is too long" );

				for ( int64_t i = 0; i < times; i++ )
					made.add ( reinterpret_cast < const char* > ( text.bytes ), text.size );

				return finish ( ctx, made, out );

			}

			case FUNCTION_LPAD:
			case FUNCTION_RPAD:
				return pad ( ctx, args, count, kind == FUNCTION_LPAD, out );

			case FUNCTION_LEFT:
			case FUNCTION_RIGHT: {

				storage::value text = text_of ( *ctx.scratch, args [ 0 ] );
				int64_t n = 0;
				size_t* offsets = nullptr;
				size_t length = 0;
				base::text made;

				integer_of ( args [ 1 ], n );

				if ( !char_offsets ( ctx, text, offsets, length ) )
					return false;

				int64_t size = static_cast < int64_t > ( length );
				int64_t keep = n >= 0 ? ( n < size ? n : size ) : ( size + n > 0 ? size + n : 0 );

				if ( kind == FUNCTION_LEFT )
					add_chars ( made, text, offsets, 0, static_cast < size_t > ( keep ) );
				else
					add_chars ( made, text, offsets, static_cast < size_t > ( size - keep ), length );

				return finish ( ctx, made, out );

			}

			case FUNCTION_REVERSE: {

				storage::value text = text_of ( *ctx.scratch, args [ 0 ] );
				size_t* offsets = nullptr;
				size_t length = 0;
				base::text made;

				if ( !char_offsets ( ctx, text, offsets, length ) )
					return false;

				for ( size_t i = length; i-- > 0; )
					add_chars ( made, text, offsets, i, i + 1 );

				return finish ( ctx, made, out );

			}

			case FUNCTION_SPLIT_PART:
				return split_part ( ctx, args, out );

			case FUNCTION_STARTS_WITH: {

				storage::value text = text_of ( *ctx.scratch, args [ 0 ] );
				storage::value prefix = text_of ( *ctx.scratch, args [ 1 ] );

				out = storage::integer_value ( prefix.size <= text.size && ( !prefix.size || base::same_bytes ( text.bytes, prefix.bytes, prefix.size ) ) ? 1 : 0 );
				out.subtype = LITERAL_BOOLEAN;

				return true;

			}

			case FUNCTION_INITCAP:
				return initcap ( ctx, text_of ( *ctx.scratch, args [ 0 ] ), out );

			case FUNCTION_TRANSLATE:
				return translate ( ctx, args, out );

			default:
				return fail_run ( ctx, "this string function is not known" );

		}

	}

}
