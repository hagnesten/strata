#include "bench/harness/bench.hh"
#include "base/console/console.hh"
#include "base/file/folder.hh"
#include "base/math/math.hh"

namespace bench {

	static void add_grouped ( base::text& out, uint64_t value ) {

		char digits [ 32 ];
		uint32_t count = 0;

		do {

			digits [ count++ ] = static_cast < char > ( '0' + value % 10 );
			value /= 10;

		} while ( value );

		for ( uint32_t i = count; i > 0; i-- ) {

			out.add ( digits [ i - 1 ] );

			if ( i > 1 && ( i - 1 ) % 3 == 0 )
				out.add ( ',' );

		}

	}

	static uint64_t micros ( const bench_report& report, uint64_t ticks ) {

		return ticks * 1000000 / report.rate;

	}

	static void add_time ( base::text& out, const bench_report& report, const bench_timing& timing ) {

		if ( timing.failed ) {

			out.add ( "failed" );

			return;

		}

		uint64_t spent = micros ( report, timing.ticks );

		if ( spent >= 10000000 ) {

			add_grouped ( out, spent / 1000 );
			out.add ( " ms" );

			return;

		}

		out.decimal ( spent, 1000, spent < 10000 ? 2 : 1 ).add ( " ms" );

	}

	static bool speedup ( const bench_result& result, double& out ) {

		const bench_timing& strata = result.engines [ 0 ];
		const bench_timing& sqlite = result.engines [ 1 ];

		if ( strata.failed || sqlite.failed || !strata.ticks || !sqlite.ticks )
			return false;

		out = static_cast < double > ( sqlite.ticks ) / static_cast < double > ( strata.ticks );

		return true;

	}

	static void add_ratio ( base::text& out, double ratio ) {

		if ( ratio >= 1 )
			out.decimal ( static_cast < uint64_t > ( ratio * 100 + 0.5 ), 100, 2 ).add ( "x faster" );
		else
			out.decimal ( static_cast < uint64_t > ( 100 / ratio + 0.5 ), 100, 2 ).add ( "x slower" );

	}

	static bool same_result ( const bench_result& result ) {

		const bench_tally& a = result.engines [ 0 ].tally;
		const bench_tally& b = result.engines [ 1 ].tally;

		return a.rows == b.rows && a.hash == b.hash;

	}

	void print_line ( const bench_report& report, const bench_result& result ) {

		base::text line;
		double ratio = 0;

		line.add ( "  " ).add ( result.item->name ).pad_to ( 38 );
		add_grouped ( line, result.engines [ 0 ].operations );
		line.pad_to ( 50 );
		add_time ( line, report, result.engines [ 0 ] );
		line.pad_to ( 64 );
		add_time ( line, report, result.engines [ 1 ] );
		line.pad_to ( 78 );

		if ( speedup ( result, ratio ) )
			add_ratio ( line, ratio );

		line.pad_to ( 93 );
		line.add ( same_result ( result ) ? "same rows" : "ROWS DIFFER" );
		line.add ( '\n' );

		for ( uint32_t e = 0; e < BENCH_ENGINES; e++ )
			if ( result.engines [ e ].failed )
				line.add ( "      " ).add ( report.names [ e ] ).add ( ": " ).add ( result.engines [ e ].problem ).add ( '\n' );

		base::write ( line );

	}

	static void summarize ( const bench_report& report, uint32_t& wins, uint32_t& compared, double& mean, uint32_t& differ ) {

		double logs = 0;

		wins = 0;
		compared = 0;
		differ = 0;

		for ( uint32_t i = 0; i < report.count; i++ ) {

			double ratio = 0;

			if ( !same_result ( report.results [ i ] ) )
				differ++;

			if ( !speedup ( report.results [ i ], ratio ) )
				continue;

			compared++;
			logs += base::natural_log ( ratio );

			if ( ratio > 1 )
				wins++;

		}

		mean = compared ? base::natural_exp ( logs / compared ) : 0;

	}

	void print_report ( const bench_report& report ) {

		uint32_t wins = 0;
		uint32_t compared = 0;
		uint32_t differ = 0;
		double mean = 0;
		base::text line;

		summarize ( report, wins, compared, mean, differ );
		line.add ( "\n  Strata was faster in " ).number ( wins ).add ( " of " ).number ( compared ).add ( " workloads, " );

		if ( compared )
			add_ratio ( line, mean );

		line.add ( " on the geometric mean\n" );

		if ( differ )
			line.add ( "  " ).number ( differ ).add ( " workloads returned different rows\n" );
		else
			line.add ( "  every workload returned the same rows from both engines\n" );

		base::write ( line );

	}

	bool write_markdown ( const bench_report& report, const char* path ) {

		uint32_t wins = 0;
		uint32_t compared = 0;
		uint32_t differ = 0;
		double mean = 0;
		base::text out;

		summarize ( report, wins, compared, mean, differ );
		out.add ( "| Workload | Operations | Strata | SQLite " ).add ( report.versions [ 1 ] ).add ( " | Strata vs SQLite |\n" );
		out.add ( "|---|---:|---:|---:|---:|\n" );

		for ( uint32_t i = 0; i < report.count; i++ ) {

			const bench_result& result = report.results [ i ];
			double ratio = 0;

			out.add ( "| " ).add ( result.item->name ).add ( " | " );
			add_grouped ( out, result.engines [ 0 ].operations );
			out.add ( " | " );
			add_time ( out, report, result.engines [ 0 ] );
			out.add ( " | " );
			add_time ( out, report, result.engines [ 1 ] );
			out.add ( " | " );

			if ( speedup ( result, ratio ) )
				add_ratio ( out, ratio );

			out.add ( " |\n" );

		}

		out.add ( "\nStrata was faster in " ).number ( wins ).add ( " of " ).number ( compared ).add ( " workloads, " );

		if ( compared )
			add_ratio ( out, mean );

		out.add ( " on the geometric mean, with " );
		add_grouped ( out, report.rows );
		out.add ( " rows. " );
		out.add ( differ ? "Some workloads returned different rows.\n" : "Both engines returned the same rows for every workload.\n" );

		return base::write_whole ( path, out.data, out.size );

	}

}
