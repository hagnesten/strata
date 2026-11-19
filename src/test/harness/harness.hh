#pragma once

#include "base/text/text.hh"
#include "base/file/file.hh"

namespace test {

	struct test_random {

		uint64_t state;

		uint64_t next ( );

		uint32_t below ( uint32_t limit );

	};

	struct test_counts {

		uint32_t checks;
		uint32_t failures;

	};

	extern test_counts counts;

	bool expect ( bool fine, const char* what );

	void section ( const char* name );

	bool scratch_path ( char* out, size_t room, const char* name );

	void forget_database ( const char* path );

	void flip_byte ( const char* path, uint64_t offset );

	int32_t run_all ( );

	void pager_tests ( );

	void btree_tests ( );

	void crash_tests ( );

}
