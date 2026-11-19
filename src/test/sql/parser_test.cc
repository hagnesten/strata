#include "test/harness/harness.hh"
#include "sql/parser/parser.hh"
#include "sql/print/print.hh"

namespace test {

	struct parse_case {

		const char* input;
		const char* printed;

	};

	struct error_case {

		const char* input;
		uint32_t line;
		uint32_t column;

	};

	static bool print_statement ( const sql::statement* item, base::text& out ) {

		if ( item->kind == sql::STATEMENT_SELECT ) {

			sql::print_select ( out, item->select );

			return true;

		}

		if ( item->kind == sql::STATEMENT_CREATE_TABLE ) {

			sql::print_create_table ( out, item->create_table );

			return true;

		}

		return false;

	}

	void parser_tests ( ) {

		section ( "sql parser" );

		static const parse_case cases [ ] = {

			{ "select 1", "SELECT 1" },
			{ "SELECT a, b AS bee FROM t WHERE a > 1 AND b < 2 OR c = 3", "SELECT a, b AS bee FROM t WHERE (((a > 1) AND (b < 2)) OR (c = 3))" },
			{ "select 1 + 2 * 3 - 4 / 2", "SELECT ((1 + (2 * 3)) - (4 / 2))" },
			{ "select 'it''s', x'00ff', null, -5, 2.5e3", "SELECT 'it''s', X'00FF', NULL, -5, 2500.0" },
			{ "select * from a join b on a.id = b.a_id left join c using (id)", "SELECT * FROM a JOIN b ON (a.id = b.a_id) LEFT JOIN c USING (id)" },
			{ "select t.* from t as x, u", "SELECT t.* FROM t AS x, u" },
			{ "select count(*), sum(distinct price) from orders group by customer having count(*) > 2", "SELECT count(*), sum(DISTINCT price) FROM orders GROUP BY customer HAVING (count(*) > 2)" },
			{ "select a from t order by a desc, b limit 10 offset 5", "SELECT a FROM t ORDER BY a DESC, b LIMIT 10 OFFSET 5" },
			{ "select a from t limit 5, 10", "SELECT a FROM t LIMIT 10 OFFSET 5" },
			{ "select x between 1 and 10, y not in (1, 2), z is not null, w like 'a%'", "SELECT x BETWEEN 1 AND 10, y NOT IN (1, 2), z IS NOT NULL, w LIKE 'a%'" },
			{ "select case when a = 1 then 'one' else 'many' end, cast(b as integer)", "SELECT CASE WHEN (a = 1) THEN 'one' ELSE 'many' END, CAST(b AS integer)" },
			{ "select * from t where id in (select id from u where u.x = t.x)", "SELECT * FROM t WHERE id IN (SELECT id FROM u WHERE (u.x = t.x))" },
			{ "select exists (select 1 from t)", "SELECT EXISTS (SELECT 1 FROM t)" },
			{ "select a from t union all select b from u union select c from v", "SELECT a FROM t UNION ALL SELECT b FROM u UNION SELECT c FROM v" },
			{ "values (1, 'a'), (2, 'b')", "VALUES (1, 'a'), (2, 'b')" },
			{ "select \"select\" from \"my table\"", "SELECT \"select\" FROM \"my table\"" },
			{ "select not a, ~b, a || b || c", "SELECT NOT a, ~b, ((a || b) || c)" },
			{ "select ?, ?3, :name", "SELECT ?1, ?3, :name" },
			{ "select a from t where b = 1 -- trailing comment\n", "SELECT a FROM t WHERE (b = 1)" },
			{ "create table people (id integer primary key autoincrement, name text not null, age int default 0 check (age >= 0), team int references teams(id) on delete cascade, unique (name, age))", "CREATE TABLE people (id integer PRIMARY KEY AUTOINCREMENT, name text NOT NULL, age int DEFAULT 0 CHECK ((age >= 0)), team int REFERENCES teams(id) ON DELETE CASCADE, UNIQUE (name, age))" },
			{ "create table if not exists t (a, b varchar(20), c decimal(10, 2))", "CREATE TABLE t (a, b varchar(20), c decimal(10,2))" },

		};

		base::arena memory;
		uint32_t matched = 0;

		for ( const parse_case& one : cases ) {

			sql::statement* parsed = nullptr;
			sql::parse_error error;
			size_t used = 0;
			base::text printed;
			bool fine = sql::parse_one ( memory, one.input, used, parsed, error ) && parsed && print_statement ( parsed, printed ) && base::same ( printed.value ( ), one.printed );

			if ( !fine ) {

				base::text line;

				line.add ( "parsing \"" ).add ( one.input ).add ( "\" gave \"" ).add ( error.failed ? error.message.value ( ) : printed.value ( ) ).add ( '"' );
				expect ( false, line.value ( ) );

			} else
				matched++;

		}

		base::text summary;

		summary.number ( matched ).add ( " of " ).number ( sizeof ( cases ) / sizeof ( cases [ 0 ] ) ).add ( " statements parse and print back exactly" );
		expect ( matched == sizeof ( cases ) / sizeof ( cases [ 0 ] ), summary.value ( ) );

		static const error_case errors [ ] = {

			{ "select from t", 1, 8 },
			{ "select a from", 1, 14 },
			{ "select a,\nfrom t", 2, 1 },
			{ "insert into t values (1, 2", 1, 27 },
			{ "create table t ()", 1, 17 },
			{ "select 'unclosed", 1, 8 },
			{ "selec 1", 1, 1 },
			{ "select a from t where", 1, 22 },
			{ "values (1, 2), (3)", 1, 16 },

		};

		uint32_t placed = 0;

		for ( const error_case& one : errors ) {

			sql::statement* parsed = nullptr;
			sql::parse_error error;
			size_t used = 0;
			bool failed = !sql::parse_one ( memory, one.input, used, parsed, error ) && error.failed;

			if ( failed && error.line == one.line && error.column == one.column )
				placed++;
			else {

				base::text line;

				line.add ( "\"" ).add ( one.input ).add ( "\" should fail at " ).number ( one.line ).add ( ':' ).number ( one.column ).add ( ", got " ).number ( error.line ).add ( ':' ).number ( error.column ).add ( ' ' ).add ( error.message.value ( ) );
				expect ( false, line.value ( ) );

			}

		}

		expect ( placed == sizeof ( errors ) / sizeof ( errors [ 0 ] ), "bad SQL fails at the right line and column" );

		base::text deep;

		deep.add ( "select " );
		deep.repeat ( '(', 5000 );
		deep.add ( '1' );
		deep.repeat ( ')', 5000 );

		sql::statement* parsed = nullptr;
		sql::parse_error error;
		size_t used = 0;

		expect ( !sql::parse_one ( memory, deep.value ( ), used, parsed, error ) && error.failed, "5000 nested brackets give an error, not a crash" );

		const char* script = "create table a (x); insert into a values (1);; select x from a";
		const char* at = script;
		uint32_t statements = 0;

		while ( *at ) {

			if ( !sql::parse_one ( memory, at, used, parsed, error ) || !used )
				break;

			if ( parsed )
				statements++;

			at += used;

		}

		expect ( statements == 3 && !*at, "a script splits into its statements" );

		static const char* const seeds [ ] = {

			"select a, b from t where x = 1 and y in (1, 2, 3) order by a desc limit 5",
			"insert into t (a, b) values (1, 'x'), (2, 'y') on conflict (a) do update set b = 'z'",
			"create table t (id integer primary key, name text not null default 'n', check (id > 0))",
			"update t set a = a + 1, b = case when b is null then 0 else b end where c between 1 and 2",
			"select count(*) from (select distinct a from t) as s join u on s.a = u.a group by u.b having count(*) > 1",

		};

		test_random random = { 0xBADC0DEull };
		uint32_t rounds = 100000;

		for ( uint32_t round = 0; round < rounds; round++ ) {

			const char* seed = seeds [ random.below ( 5 ) ];
			base::text mangled;
			size_t size = base::length ( seed );

			mangled.add ( seed, size );

			for ( uint32_t edit = 0; edit < 1 + random.below ( 5 ); edit++ ) {

				uint32_t kind = random.below ( 4 );
				size_t place = random.below ( static_cast < uint32_t > ( mangled.size ? mangled.size : 1 ) );

				if ( kind == 0 && mangled.size )
					mangled.data [ place ] = static_cast < char > ( 32 + random.below ( 95 ) );
				else if ( kind == 1 && mangled.size ) {

					mangled.size = place;
					mangled.data [ place ] = 0;

				} else if ( kind == 2 ) {

					static const char inserts [ ] = "()',;\"*?";

					if ( mangled.size ) {

						mangled.data [ place ] = inserts [ random.below ( 8 ) ];

					}

				} else if ( mangled.size )
					mangled.data [ place ] = static_cast < char > ( random.below ( 256 ) | 1 );

			}

			base::arena scratch;

			sql::parse_one ( scratch, mangled.value ( ), used, parsed, error );

		}

		base::text fuzzed;

		fuzzed.number ( rounds ).add ( " mangled statements parse or fail without crashing" );
		expect ( true, fuzzed.value ( ) );
		section ( fuzzed.value ( ) );

	}

}
