#include "bench/harness/bench.hh"
#include "bench/drivers/drivers.hh"
#include "bench/workloads/workloads.hh"
#include "base/clock/clock.hh"
#include "base/console/console.hh"
#include "base/file/file.hh"
#include "bench/profile/sampler.hh"

namespace bench {

	static sampler* profiling;
	static bool profile_case;

	static const char* const sqlite_places [ ] = {

		"C:\\Program Files\\Python314\\DLLs\\sqlite3.dll",
		"C:\\Program Files\\Python313\\DLLs\\sqlite3.dll",
		"C:\\Program Files\\Python312\\DLLs\\sqlite3.dll",
		"C:\\Program Files\\Python311\\DLLs\\sqlite3.dll",

	};

	static bool read_number ( const char* text, uint32_t& out ) {

		uint64_t value = 0;

		if ( !text [ 0 ] )
			return false;

		for ( const char* at = text; *at; at++ ) {

			if ( *at == ',' || *at == '_' )
				continue;

			if ( *at < '0' || *at > '9' )
				return false;

			value = value * 10 + static_cast < uint64_t > ( *at - '0' );

			if ( value > 100000000 )
				return false;

		}

		out = static_cast < uint32_t > ( value );

		return out >= 1000;

	}

	static bool read_options ( int32_t count, char** arguments, bench_options& out ) {

		out = { };
		out.rows = BENCH_DEFAULT_ROWS;

		for ( int32_t i = 0; i < count; i++ ) {

			const char* item = arguments [ i ];

			if ( base::same ( item, "--sqlite" ) && i + 1 < count )
				out.sqlite = arguments [ ++i ];
			else if ( base::same ( item, "--postgres" ) && i + 1 < count )
				out.postgres = arguments [ ++i ];
			else if ( base::same ( item, "--out" ) && i + 1 < count )
				out.out = arguments [ ++i ];
			else if ( base::same ( item, "--only" ) && i + 1 < count )
				out.only = arguments [ ++i ];
			else if ( base::same ( item, "--profile" ) && i + 1 < count )
				out.profile = arguments [ ++i ];
			else if ( !read_number ( item, out.rows ) ) {

				base::write_error ( "usage: strata --bench [rows] [--sqlite <sqlite3.dll> | --postgres <connection>] [--out <results.md>] [--only <words>]\n" );

				return false;

			}

		}

		if ( out.postgres )
			return true;

		if ( !out.sqlite ) {

			for ( const char* place : sqlite_places ) {

				if ( base::path_exists ( place ) ) {

					out.sqlite = place;

					break;

				}

			}

		}

		if ( !out.sqlite ) {

			base::write_error ( "no sqlite3.dll was found, pass one with --sqlite <path>\n" );

			return false;

		}

		return true;

	}

	static void erase_database ( const char* path ) {

		base::text log;
		base::text shared;

		log.add ( path ).add ( "-wal" );
		shared.add ( path ).add ( "-shm" );
		base::erase_path ( path );
		base::erase_path ( log.value ( ) );
		base::erase_path ( shared.value ( ) );

	}

	static bool mentions ( const char* text, const char* part ) {

		size_t size = base::length ( part );

		for ( size_t i = 0; text [ i ]; i++ )
			if ( base::same_bytes ( text + i, part, size ) )
				return true;

		return !size;

	}

	static void sort_samples ( uint64_t* items, uint32_t count ) {

		for ( uint32_t i = 1; i < count; i++ )
			for ( uint32_t k = i; k > 0 && items [ k - 1 ] > items [ k ]; k-- ) {

				uint64_t held = items [ k ];

				items [ k ] = items [ k - 1 ];
				items [ k - 1 ] = held;

			}

	}

	static void measure ( bench_driver* drivers, const bench_case& item, const bench_data& data, bench_result& result, uint32_t position ) {

		uint32_t repeats = item.repeat ? BENCH_REPEATS : 1;
		uint64_t samples [ BENCH_ENGINES ] [ BENCH_REPEATS ] = { };
		uint32_t taken [ BENCH_ENGINES ] = { };

		result.item = &item;

		for ( uint32_t r = 0; r < repeats; r++ ) {

			for ( uint32_t k = 0; k < BENCH_ENGINES; k++ ) {

				uint32_t e = ( r + position + k ) % BENCH_ENGINES;
				bench_driver& driver = drivers [ e ];
				bench_timing& timing = result.engines [ e ];

				if ( timing.failed )
					continue;

				bench_tally tally = { };
				uint64_t operations = 0;

				if ( profiling )
					profiling->paused = e != 0 || !profile_case;

				uint64_t start = base::precise_ticks ( );
				bool fine = item.run ( driver, data, tally, operations );
				uint64_t spent = base::precise_ticks ( ) - start;

				if ( profiling )
					profiling->paused = true;

				if ( !fine ) {

					timing.failed = true;
					base::copy_text ( timing.problem, sizeof ( timing.problem ), driver.problem ( driver.state ) );

					continue;

				}

				if ( !r ) {

					timing.tally = tally;
					timing.operations = operations;

				}

				samples [ e ] [ taken [ e ]++ ] = spent;

			}

		}

		for ( uint32_t e = 0; e < BENCH_ENGINES; e++ ) {

			sort_samples ( samples [ e ], taken [ e ] );
			result.engines [ e ].ticks = taken [ e ] ? samples [ e ] [ taken [ e ] / 2 ] : 0;

		}

	}

	static bool open_engines ( bench_driver* drivers, char paths [ BENCH_ENGINES ] [ FILE_PATH_ROOM ] ) {

		static const char* const names [ BENCH_ENGINES ] = { "strata-bench.db", "sqlite-bench.db" };

		for ( uint32_t e = 0; e < BENCH_ENGINES; e++ ) {

			if ( !base::temp_path ( paths [ e ], FILE_PATH_ROOM, names [ e ] ) ) {

				base::write_error ( "the temporary folder could not be found\n" );

				return false;

			}

			erase_database ( paths [ e ] );

			if ( !drivers [ e ].open ( drivers [ e ].state, paths [ e ] ) || !drivers [ e ].exec ( drivers [ e ].state, drivers [ e ].schema ? drivers [ e ].schema : BENCH_SCHEMA ) ) {

				base::text line;

				line.add ( drivers [ e ].name ).add ( " could not create its database: " ).add ( drivers [ e ].problem ( drivers [ e ].state ) ).add ( '\n' );
				base::write_error ( line.value ( ) );

				return false;

			}

		}

		return true;

	}

	static void print_heading ( const bench_report& report, const bench_driver* drivers, char paths [ BENCH_ENGINES ] [ FILE_PATH_ROOM ] ) {

		base::text line;
		const bench_driver& other = drivers [ 1 ];

		line.add ( "strata benchmark, " ).number ( report.rows ).add ( " rows\n" );
		line.add ( "  Strata " ).add ( report.versions [ 0 ] ).add ( "  " ).add ( paths [ 0 ] ).add ( '\n' );
		line.add ( "  " ).add ( other.name ).add ( ' ' ).add ( other.version ).add ( "  " );

		if ( other.schema )
			line.add ( other.detail ).add ( '\n' );
		else
			line.add ( paths [ 1 ] ).add ( "\n         " ).add ( other.detail ).add ( '\n' );

		line.add ( "  " ).add ( other.note ).add ( "\n\n" );
		line.add ( "  workload" ).pad_to ( 38 ).add ( "operations" ).pad_to ( 50 ).add ( "Strata" ).pad_to ( 64 ).add ( other.name ).pad_to ( 78 ).add ( "Strata vs " ).add ( other.name ).add ( '\n' );
		base::write ( line );

	}

	int32_t run_bench ( int32_t count, char** arguments ) {

		bench_options options;

		base::use_utf8 ( );

		if ( !read_options ( count, arguments, options ) )
			return 2;

		bench_driver drivers [ BENCH_ENGINES ] = { };

		bool other = options.postgres ? make_postgres_driver ( drivers [ 1 ], options.postgres ) : make_sqlite_driver ( drivers [ 1 ], options.sqlite );

		if ( !make_strata_driver ( drivers [ 0 ] ) || !other ) {

			base::text line;

			if ( options.postgres )
				line.add ( "the Postgres connection could not be read: use host=... port=... user=... password=... dbname=... or postgresql://user:password@host:port/database\n" );
			else
				line.add ( "SQLite could not be loaded from " ).add ( options.sqlite ).add ( '\n' );

			base::write_error ( line.value ( ) );
			free_driver ( drivers [ 0 ] );
			free_driver ( drivers [ 1 ] );

			return 1;

		}

		bench_data data = { };
		uint32_t case_count = 0;
		const bench_case* cases = bench_cases ( case_count );
		bench_report report = { };

		report.results = base::make < bench_result > ( case_count );
		report.rows = options.rows;
		report.rate = base::tick_rate ( );

		for ( uint32_t e = 0; e < BENCH_ENGINES; e++ ) {

			report.names [ e ] = drivers [ e ].name;
			report.versions [ e ] = drivers [ e ].version;

		}

		char paths [ BENCH_ENGINES ] [ FILE_PATH_ROOM ];
		int32_t code = 1;

		if ( !report.results || !make_data ( data, options.rows ) )
			base::write_error ( "out of memory for the benchmark data\n" );
		else if ( open_engines ( drivers, paths ) ) {

			print_heading ( report, drivers, paths );

			sampler watch;

			if ( options.profile && start_sampler ( watch ) ) {

				watch.paused = true;
				profiling = &watch;

			}

			for ( uint32_t i = 0; i < case_count; i++ ) {

				profile_case = !options.only || mentions ( cases [ i ].name, options.only );

				if ( !profile_case && cases [ i ].repeat )
					continue;

				measure ( drivers, cases [ i ], data, report.results [ report.count ], i );
				print_line ( report, report.results [ report.count ] );
				report.count++;

			}

			if ( profiling ) {

				profiling = nullptr;

				if ( !stop_sampler ( watch, options.profile ) )
					base::write_error ( "the profile could not be written\n" );

			}

			print_report ( report );

			if ( options.out && !write_markdown ( report, options.out ) )
				base::write_error ( "the results file could not be written\n" );

			code = 0;

		}

		for ( uint32_t e = 0; e < BENCH_ENGINES; e++ )
			free_driver ( drivers [ e ] );

		for ( uint32_t e = 0; e < BENCH_ENGINES && code == 0; e++ )
			erase_database ( paths [ e ] );

		free_data ( data );
		base::release ( report.results );

		return code;

	}

}
