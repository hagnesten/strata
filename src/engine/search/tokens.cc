#include "engine/search/search.hh"

namespace engine {

	static bool word_byte ( uint8_t byte ) {

		return ( byte >= '0' && byte <= '9' ) || ( byte >= 'a' && byte <= 'z' ) || ( byte >= 'A' && byte <= 'Z' ) || byte >= 0x80;

	}

	bool next_search_token ( const uint8_t* text, size_t size, size_t& at, search_token& out ) {

		while ( at < size && !word_byte ( text [ at ] ) )
			at++;

		if ( at >= size )
			return false;

		bool truncated = false;

		out.start = at;
		out.folded_size = 0;

		while ( at < size && word_byte ( text [ at ] ) ) {

			if ( out.folded_size < SEARCH_TOKEN_ROOM - 1 )
				out.folded [ out.folded_size++ ] = base::lower ( static_cast < char > ( text [ at ] ) );
			else
				truncated = true;

			at++;

		}

		if ( truncated ) {

			uint32_t back = out.folded_size;

			while ( back && ( static_cast < uint8_t > ( out.folded [ back - 1 ] ) & 0xC0 ) == 0x80 )
				back--;

			if ( back && static_cast < uint8_t > ( out.folded [ back - 1 ] ) >= 0xC0 ) {

				uint8_t lead = static_cast < uint8_t > ( out.folded [ back - 1 ] );
				uint32_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;

				if ( out.folded_size - ( back - 1 ) < need )
					out.folded_size = back - 1;

			}

		}

		out.folded [ out.folded_size ] = 0;
		out.size = at - out.start;

		return true;

	}

	bool same_search_term ( const search_token& token, const search_node& term ) {

		if ( term.prefix )
			return token.folded_size >= term.term_size && base::same_bytes ( token.folded, term.term, term.term_size );

		return token.folded_size == term.term_size && base::same_bytes ( token.folded, term.term, term.term_size );

	}

}