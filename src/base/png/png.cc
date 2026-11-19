#include "base/png/png.hh"

namespace base {

	static uint32_t crc_table [ 256 ];
	static bool crc_ready;

	uint32_t crc32 ( const uint8_t* data, size_t size, uint32_t seed ) {

		if ( !crc_ready ) {

			for ( uint32_t i = 0; i < 256; i++ ) {

				uint32_t value = i;

				for ( uint32_t k = 0; k < 8; k++ )
					value = value & 1 ? 0xEDB88320u ^ ( value >> 1 ) : value >> 1;

				crc_table [ i ] = value;

			}

			crc_ready = true;

		}

		uint32_t crc = ~seed;

		for ( size_t i = 0; i < size; i++ )
			crc = crc_table [ ( crc ^ data [ i ] ) & 0xFF ] ^ ( crc >> 8 );

		return ~crc;

	}

	static void put_bits ( bit_writer& writer, uint32_t value, uint32_t count ) {

		writer.bits |= static_cast < uint64_t > ( value ) << writer.count;
		writer.count += count;

		while ( writer.count >= 8 ) {

			writer.out->add ( static_cast < char > ( writer.bits & 0xFF ) );
			writer.bits >>= 8;
			writer.count -= 8;

		}

	}

	static void put_code ( bit_writer& writer, uint32_t code, uint32_t length ) {

		uint32_t reversed = 0;

		for ( uint32_t i = 0; i < length; i++ )
			reversed |= ( ( code >> i ) & 1 ) << ( length - 1 - i );

		put_bits ( writer, reversed, length );

	}

	static void put_symbol ( bit_writer& writer, uint32_t symbol ) {

		if ( symbol < 144 )
			put_code ( writer, 0x30 + symbol, 8 );
		else if ( symbol < 256 )
			put_code ( writer, 0x190 + symbol - 144, 9 );
		else if ( symbol < 280 )
			put_code ( writer, symbol - 256, 7 );
		else
			put_code ( writer, 0xC0 + symbol - 280, 8 );

	}

	static const uint16_t length_base [ 29 ] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
	static const uint8_t length_extra [ 29 ] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
	static const uint16_t distance_base [ 30 ] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
	static const uint8_t distance_extra [ 30 ] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

	static void put_match ( bit_writer& writer, uint32_t length, uint32_t distance ) {

		uint32_t l = 28;

		while ( length_base [ l ] > length )
			l--;

		put_symbol ( writer, 257 + l );
		put_bits ( writer, length - length_base [ l ], length_extra [ l ] );

		uint32_t d = 29;

		while ( distance_base [ d ] > distance )
			d--;

		put_code ( writer, d, 5 );
		put_bits ( writer, distance - distance_base [ d ], distance_extra [ d ] );

	}

	static bool deflate ( const uint8_t* data, size_t size, text& out ) {

		int32_t* head = make < int32_t > ( PNG_HASH );
		int32_t* previous = make < int32_t > ( PNG_WINDOW );

		if ( !head || !previous ) {

			release ( head );
			release ( previous );

			return false;

		}

		fill ( head, 0xFF, sizeof ( int32_t ) * PNG_HASH );

		bit_writer writer = { &out, 0, 0 };

		put_bits ( writer, 1, 1 );
		put_bits ( writer, 1, 2 );

		auto hash_at = [ & ] ( size_t at ) {

			return ( ( static_cast < uint32_t > ( data [ at ] ) << 16 | static_cast < uint32_t > ( data [ at + 1 ] ) << 8 | data [ at + 2 ] ) * 2654435761u ) >> 16;

		};

		auto remember = [ & ] ( size_t at ) {

			if ( at + 2 >= size )
				return;

			uint32_t hash = hash_at ( at );

			previous [ at % PNG_WINDOW ] = head [ hash ];
			head [ hash ] = static_cast < int32_t > ( at );

		};

		size_t at = 0;

		while ( at < size ) {

			uint32_t best = 0;
			uint32_t best_distance = 0;

			if ( at + 2 < size ) {

				int32_t candidate = head [ hash_at ( at ) ];
				uint32_t chain = 0;
				size_t limit = size - at < 258 ? size - at : 258;

				while ( candidate >= 0 && at - static_cast < size_t > ( candidate ) <= PNG_WINDOW - 1 && chain++ < PNG_CHAIN ) {

					const uint8_t* a = data + candidate;
					const uint8_t* b = data + at;
					uint32_t length = 0;

					while ( length < limit && a [ length ] == b [ length ] )
						length++;

					if ( length > best ) {

						best = length;
						best_distance = static_cast < uint32_t > ( at - static_cast < size_t > ( candidate ) );

						if ( length == limit )
							break;

					}

					int32_t next = previous [ candidate % PNG_WINDOW ];

					if ( next >= candidate )
						break;

					candidate = next;

				}

			}

			if ( best >= 3 ) {

				put_match ( writer, best, best_distance );

				for ( uint32_t i = 0; i < best; i++ )
					remember ( at + i );

				at += best;

			} else {

				put_symbol ( writer, data [ at ] );
				remember ( at );
				at++;

			}

		}

		put_symbol ( writer, 256 );

		if ( writer.count )
			put_bits ( writer, 0, 8 - writer.count );

		release ( head );
		release ( previous );

		return true;

	}

	static void put32 ( text& out, uint32_t value ) {

		out.add ( static_cast < char > ( value >> 24 ) ).add ( static_cast < char > ( value >> 16 ) ).add ( static_cast < char > ( value >> 8 ) ).add ( static_cast < char > ( value ) );

	}

	static void put_chunk ( text& out, const char* kind, const text& body ) {

		put32 ( out, static_cast < uint32_t > ( body.size ) );

		size_t start = out.size;

		out.add ( kind, 4 ).add ( body.value ( ), body.size );
		put32 ( out, crc32 ( reinterpret_cast < const uint8_t* > ( out.value ( ) + start ), out.size - start, 0 ) );

	}

	bool encode_png ( const uint8_t* bgra, uint32_t width, uint32_t height, uint32_t stride, text& out ) {

		size_t row = static_cast < size_t > ( width ) * 3 + 1;
		size_t total = row * height;
		uint8_t* raw = make < uint8_t > ( total );

		if ( !raw )
			return false;

		for ( uint32_t y = 0; y < height; y++ ) {

			uint8_t* to = raw + y * row;
			const uint8_t* from = bgra + static_cast < size_t > ( y ) * stride;

			to [ 0 ] = 0;

			for ( uint32_t x = 0; x < width; x++ ) {

				to [ 1 + x * 3 ] = from [ x * 4 + 2 ];
				to [ 2 + x * 3 ] = from [ x * 4 + 1 ];
				to [ 3 + x * 3 ] = from [ x * 4 ];

			}

		}

		text packed;

		packed.add ( static_cast < char > ( 0x78 ) ).add ( static_cast < char > ( 0x01 ) );

		if ( !deflate ( raw, total, packed ) ) {

			release ( raw );

			return false;

		}

		uint32_t a = 1;
		uint32_t b = 0;

		for ( size_t i = 0; i < total; i++ ) {

			a = ( a + raw [ i ] ) % 65521;
			b = ( b + a ) % 65521;

		}

		put32 ( packed, b << 16 | a );
		release ( raw );

		text header;

		put32 ( header, width );
		put32 ( header, height );
		header.add ( static_cast < char > ( 8 ) ).add ( static_cast < char > ( 2 ) ).add ( '\0' ).add ( '\0' ).add ( '\0' );

		text empty;

		out.clear ( );
		out.add ( "\x89PNG\r\n\x1A\n", 8 );
		put_chunk ( out, "IHDR", header );
		put_chunk ( out, "IDAT", packed );
		put_chunk ( out, "IEND", empty );

		return true;

	}

}
