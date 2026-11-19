#pragma once

#include "base/text/text.hh"

#define BENCH_REPEATS 5
#define BENCH_PARAMETERS 8
#define BENCH_ENGINES 2
#define BENCH_DEFAULT_ROWS 1000000
#define BENCH_PROBLEM_ROOM 256

namespace bench {

	enum bench_kind : uint8_t {

		BENCH_NULL,
		BENCH_INTEGER,
		BENCH_REAL,
		BENCH_TEXT,

	};

	struct bench_value {

		bench_kind kind;
		int64_t integer;
		double real;
		const char* text;
		uint32_t size;

	};

	struct bench_tally {

		uint64_t rows;
		uint64_t hash;

	};

	struct bench_driver {

		const char* name;
		const char* version;
		void* state;
		bool ( *open ) ( void* state, const char* path );
		void ( *close ) ( void* state );
		bool ( *exec ) ( void* state, const char* sql );
		void* ( *prepare ) ( void* state, const char* sql );
		bool ( *run ) ( void* state, void* statement, const bench_value* values, uint32_t count, bench_tally& tally );
		void ( *finish ) ( void* state, void* statement );
		const char* ( *problem ) ( void* state );

	};

	struct bench_data {

		uint32_t items;
		uint32_t orders;
		int64_t* item_a;
		uint32_t* item_b;
		uint8_t* item_b_size;
		double* item_c;
		int64_t* item_d;
		char* words;
		int64_t* order_item;
		int64_t* order_qty;
		uint32_t* probes;
		uint32_t probe_count;

	};

	struct bench_case {

		const char* name;
		bool repeat;
		bool ( *run ) ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	};

	struct bench_timing {

		bool failed;
		uint64_t ticks;
		uint64_t operations;
		bench_tally tally;
		char problem [ BENCH_PROBLEM_ROOM ];

	};

	struct bench_result {

		const bench_case* item;
		bench_timing engines [ BENCH_ENGINES ];

	};

	struct bench_report {

		bench_result* results;
		uint32_t count;
		uint32_t rows;
		uint64_t rate;
		const char* names [ BENCH_ENGINES ];
		const char* versions [ BENCH_ENGINES ];

	};

	struct bench_options {

		uint32_t rows;
		const char* sqlite;
		const char* out;
		const char* only;
		const char* profile;

	};

	int32_t run_bench ( int32_t count, char** arguments );

	void print_line ( const bench_report& report, const bench_result& result );

	bool make_data ( bench_data& out, uint32_t items );

	void free_data ( bench_data& item );

	const bench_case* bench_cases ( uint32_t& count );

	void print_report ( const bench_report& report );

	bool write_markdown ( const bench_report& report, const char* path );

	uint64_t mix_hash ( uint64_t value );

	uint64_t number_hash ( double value );

	uint64_t text_hash ( const void* bytes, size_t size );

	void count_row ( bench_tally& tally, uint64_t row );

	inline bench_value integer_of ( int64_t value ) {

		bench_value out = { };

		out.kind = BENCH_INTEGER;
		out.integer = value;

		return out;

	}

	inline bench_value real_of ( double value ) {

		bench_value out = { };

		out.kind = BENCH_REAL;
		out.real = value;

		return out;

	}

	inline bench_value text_of ( const char* value, uint32_t size ) {

		bench_value out = { };

		out.kind = BENCH_TEXT;
		out.text = value;
		out.size = size;

		return out;

	}

	inline bench_value word_of ( const bench_data& data, uint32_t row ) {

		return text_of ( data.words + data.item_b [ row ], data.item_b_size [ row ] );

	}

	template < typename filler > bool repeat_statement ( bench_driver& driver, const char* sql, uint32_t times, bench_tally& tally, filler fill ) {

		void* statement = driver.prepare ( driver.state, sql );

		if ( !statement )
			return false;

		bench_value values [ BENCH_PARAMETERS ];
		bool fine = true;

		for ( uint32_t i = 0; i < times && fine; i++ ) {

			uint32_t count = fill ( i, values );

			fine = driver.run ( driver.state, statement, values, count, tally );

		}

		driver.finish ( driver.state, statement );

		return fine;

	}

	inline bool run_once ( bench_driver& driver, const char* sql, bench_tally& tally ) {

		return repeat_statement ( driver, sql, 1, tally, [ ] ( uint32_t, bench_value* ) { return 0u; } );

	}

}
