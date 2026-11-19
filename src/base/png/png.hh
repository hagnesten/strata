#pragma once

#include "base/text/text.hh"

#define PNG_WINDOW 32768
#define PNG_HASH 65536
#define PNG_CHAIN 48

namespace base {

	struct bit_writer {

		text* out;
		uint64_t bits;
		uint32_t count;

	};

	bool encode_png ( const uint8_t* bgra, uint32_t width, uint32_t height, uint32_t stride, text& out );

	uint32_t crc32 ( const uint8_t* data, size_t size, uint32_t seed );

}
