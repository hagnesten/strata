#pragma once

#include "base/arena/arena.hh"
#include "sql/syntax/syntax.hh"

namespace engine {

	enum truth_kind : int8_t {

		TRUTH_NULL = -1,
		TRUTH_FALSE = 0,
		TRUTH_TRUE = 1,

	};

	truth_kind truth_of ( const storage::value& item );

	storage::value truth_value ( truth_kind truth );

	bool read_number_text ( const uint8_t* bytes, size_t size, storage::value& out, bool whole );

	storage::value numeric_of ( const storage::value& item );

	bool real_of ( const storage::value& item, double& out );

	bool integer_of ( const storage::value& item, int64_t& out );

	storage::value keep_value ( base::arena& memory, const storage::value& item );

	storage::value text_of ( base::arena& memory, const storage::value& item );

	storage::value apply_affinity ( base::arena& memory, const storage::value& item, sql::affinity_kind affinity );

	storage::value arithmetic ( sql::operator_kind op, const storage::value& a, const storage::value& b );

	int32_t order_values ( const storage::value& a, const storage::value& b );

	int32_t order_folded ( const storage::value& a, const storage::value& b );

	truth_kind compare_with ( sql::operator_kind op, const storage::value& a, const storage::value& b, bool folded = false );

	bool like_match ( const uint8_t* pattern, size_t pattern_size, const uint8_t* text, size_t text_size, uint32_t escape, bool glob );

	uint32_t hash_value ( const storage::value& item, uint32_t seed );

	storage::value folded_value ( base::arena& memory, const storage::value& item );

	bool same_value ( const storage::value& a, const storage::value& b );

}
