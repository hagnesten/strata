#pragma once

#include "storage/record/record.hh"

#define DECIMAL_MAX_DIGITS 36
#define DECIMAL_MAX_SCALE 30
#define DECIMAL_DIVIDE_DIGITS 16

namespace engine {

	struct decimal_number {

		__int128 mantissa;
		uint32_t scale;

	};

	bool decimal_of ( const storage::value& item, decimal_number& out );

	storage::value decimal_result ( const decimal_number& item );

	bool parse_decimal ( const uint8_t* text, size_t size, decimal_number& out );

	bool decimal_from_real ( double number, decimal_number& out );

	double decimal_real ( const decimal_number& item );

	void decimal_text ( const decimal_number& item, base::text& out );

	int32_t decimal_compare ( const decimal_number& a, const decimal_number& b );

	bool decimal_rescale ( decimal_number& item, uint32_t scale );

	uint32_t decimal_whole_digits ( const decimal_number& item );

	void decimal_trim ( decimal_number& item );

	bool decimal_integral ( const decimal_number& item, int64_t& out );

	bool decimal_add ( const decimal_number& a, const decimal_number& b, bool subtract, decimal_number& out );

	bool decimal_multiply ( const decimal_number& a, const decimal_number& b, decimal_number& out );

	bool decimal_divide ( const decimal_number& a, const decimal_number& b, decimal_number& out );

	bool decimal_modulo ( const decimal_number& a, const decimal_number& b, decimal_number& out );

	bool decimal_round ( decimal_number& item, int32_t places );

	bool decimal_type ( const char* type, int32_t& precision, int32_t& scale );

}
