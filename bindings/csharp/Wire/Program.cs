using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Threading;

namespace Strata.Wire {

	internal static class Program {

		private const string NpgsqlTypes =
			"/*** Load all supported types ***/\n" +
			"SELECT ns.nspname, t.oid, t.typname, t.typtype, t.typnotnull, t.elemtypoid\n" +
			"FROM (\n" +
			"    -- Arrays have typtype=b - this subquery identifies them by their typreceive and converts their typtype to a\n" +
			"    SELECT\n" +
			"        typ.oid, typ.typnamespace, typ.typname, typ.typtype, typ.typrelid, typ.typnotnull, typ.relkind,\n" +
			"        elemtyp.oid AS elemtypoid, elemtyp.typname AS elemtypname, elemcls.relkind AS elemrelkind,\n" +
			"        CASE WHEN elemproc.proname='array_recv' THEN 'a' ELSE elemtyp.typtype END AS elemtyptype\n" +
			"    FROM (\n" +
			"        SELECT typ.oid, typnamespace, typname, typrelid, typnotnull, relkind, typelem AS elemoid,\n" +
			"            CASE WHEN proc.proname='array_recv' THEN 'a' ELSE typ.typtype END AS typtype,\n" +
			"            CASE\n" +
			"                WHEN proc.proname='array_recv' THEN typ.typelem\n" +
			"                WHEN typ.typtype='r' THEN rngsubtype\n" +
			"                WHEN typ.typtype='m' THEN (SELECT rngtypid FROM pg_range WHERE rngmultitypid = typ.oid)\n" +
			"                WHEN typ.typtype='d' THEN typ.typbasetype\n" +
			"            END AS elemtypoid\n" +
			"        FROM pg_type AS typ\n" +
			"        LEFT JOIN pg_class AS cls ON (cls.oid = typ.typrelid)\n" +
			"        LEFT JOIN pg_proc AS proc ON proc.oid = typ.typreceive\n" +
			"        LEFT JOIN pg_range ON (pg_range.rngtypid = typ.oid)\n" +
			"    ) AS typ\n" +
			"    LEFT JOIN pg_type AS elemtyp ON elemtyp.oid = elemtypoid\n" +
			"    LEFT JOIN pg_class AS elemcls ON (elemcls.oid = elemtyp.typrelid)\n" +
			"    LEFT JOIN pg_proc AS elemproc ON elemproc.oid = elemtyp.typreceive\n" +
			") AS t\n" +
			"JOIN pg_namespace AS ns ON (ns.oid = typnamespace)\n" +
			"WHERE\n" +
			"    typtype IN ('b', 'r', 'm', 'e', 'd') OR\n" +
			"    (typtype = 'c' AND relkind='c') OR\n" +
			"    (typtype = 'p' AND typname IN ('record', 'void', 'unknown')) OR\n" +
			"    (typtype = 'a' AND (\n" +
			"        elemtyptype IN ('b', 'r', 'm', 'e', 'd') OR\n" +
			"        (elemtyptype = 'p' AND elemtypname IN ('record', 'void')) OR\n" +
			"        (elemtyptype = 'c' AND elemrelkind='c')\n" +
			"    ))\n" +
			"ORDER BY CASE\n" +
			"       WHEN typtype IN ('b', 'e', 'p') THEN 0\n" +
			"       WHEN typtype = 'r' THEN 1\n" +
			"       WHEN typtype = 'm' THEN 2\n" +
			"       WHEN typtype = 'c' THEN 3\n" +
			"       WHEN typtype = 'd' AND elemtyptype <> 'a' THEN 4\n" +
			"       WHEN typtype = 'a' THEN 5\n" +
			"       WHEN typtype = 'd' AND elemtyptype = 'a' THEN 6\n" +
			"END;\n" +
			"\n" +
			"/*** Load field definitions for (free-standing) composite types ***/\n" +
			"SELECT typ.oid, att.attname, att.atttypid\n" +
			"FROM pg_type AS typ\n" +
			"JOIN pg_namespace AS ns ON (ns.oid = typ.typnamespace)\n" +
			"JOIN pg_class AS cls ON (cls.oid = typ.typrelid)\n" +
			"JOIN pg_attribute AS att ON (att.attrelid = typ.typrelid)\n" +
			"WHERE\n" +
			"  (typ.typtype = 'c' AND cls.relkind='c') AND\n" +
			"  attnum > 0 AND\n" +
			"  NOT attisdropped\n" +
			"ORDER BY typ.oid, att.attnum;\n" +
			"\n" +
			"/*** Load enum fields ***/\n" +
			"SELECT pg_type.oid, enumlabel\n" +
			"FROM pg_enum\n" +
			"JOIN pg_type ON pg_type.oid=enumtypid\n" +
			"ORDER BY oid, enumsortorder;\n";

		private const string NpgsqlReset = "SET SESSION AUTHORIZATION DEFAULT;RESET ALL;CLOSE ALL;UNLISTEN *;SELECT pg_advisory_unlock_all();DISCARD SEQUENCES;DISCARD TEMP";

		private static readonly string [ ] Symbols = { "AAPL", "MSFT", "NVDA", "AMZN", "GOOG", "META", "TSLA", "JPM", "XOM", "EURUSD", "GBPUSD", "USDJPY" };

		private static int checks;
		private static int failures;

		private static void Expect ( bool fine, string what ) {

			checks++;

			if ( fine )
				return;

			failures++;
			Console.WriteLine ( "  failed  " + what );

		}

		private static string Option ( string [ ] args, string name, string fallback ) {

			for ( int i = 0; i + 1 < args.Length; i++ )
				if ( args [ i ] == name )
					return args [ i + 1 ];

			return fallback;

		}

		private static PgWire Connect ( string [ ] args ) {

			return new PgWire ( Option ( args, "--host", "127.0.0.1" ), int.Parse ( Option ( args, "--port", "5432" ), CultureInfo.InvariantCulture ), Option ( args, "--user", "strata" ), Option ( args, "--password", "strata" ), Option ( args, "--database", "strata" ) );

		}

		private static string Rate ( int count, Stopwatch clock ) {

			double seconds = clock.Elapsed.TotalSeconds;

			return string.Format ( CultureInfo.InvariantCulture, "{0,8:N0} ops  {1,10:N0} ops/s  {2,8:N1} us each", count, count / Math.Max ( seconds, 1e-9 ), seconds * 1e6 / Math.Max ( count, 1 ) );

		}

		private static string Percentiles ( List < double > samples ) {

			samples.Sort ( );

			Func < double, double > at = q => samples [ Math.Min ( samples.Count - 1, ( int ) ( q * samples.Count ) ) ];

			return string.Format ( CultureInfo.InvariantCulture, "p50 {0:N1} us  p99 {1:N1} us  max {2:N1} us", at ( 0.50 ), at ( 0.99 ), samples [ samples.Count - 1 ] );

		}

		private static void Handshake ( PgWire wire ) {

			Console.WriteLine ( "Npgsql handshake" );
			Expect ( wire.Settings.ContainsKey ( "server_version" ) && wire.Settings [ "server_version" ].StartsWith ( "17", StringComparison.Ordinal ), "the server reports a Postgres 17 version" );
			Expect ( wire.Settings.ContainsKey ( "integer_datetimes" ) && wire.Settings [ "integer_datetimes" ] == "on", "the server uses integer timestamps" );

			List < PgResult > types = wire.Query ( NpgsqlTypes );

			Expect ( types.Count == 3, "the type batch returns three result sets" );
			Expect ( types [ 0 ].Rows.Count > 60, "Npgsql gets the backend types" );

			Dictionary < string, object [ ] > byName = types [ 0 ].Rows.ToDictionary ( row => ( string ) row [ 2 ], row => row );

			foreach ( string name in new [ ] { "bool", "int2", "int4", "int8", "text", "varchar", "numeric", "float8", "timestamptz", "timestamp", "date", "uuid", "jsonb", "bytea", "record", "void", "unknown", "_int4", "_text", "_timestamptz" } )
				Expect ( byName.ContainsKey ( name ), "the type list has " + name );

			Expect ( byName.ContainsKey ( "_int8" ) && ( string ) byName [ "_int8" ] [ 3 ] == "a" && ( string ) byName [ "_int8" ] [ 5 ] == "20", "_int8 is an array of int8" );
			Expect ( byName.ContainsKey ( "int4" ) && ( string ) byName [ "int4" ] [ 4 ] == "f", "typnotnull arrives as Postgres text f" );
			Expect ( ( string ) types [ 0 ].Rows [ 0 ] [ 3 ] != "a", "base types come before arrays" );

			wire.Query ( NpgsqlReset );
			Expect ( !wire.InTransaction, "the Npgsql pool reset leaves the session idle" );

			PgResult version = wire.Query ( "SELECT version(), current_database(), current_schema(), current_setting('server_version_num')::int" ) [ 0 ];

			Expect ( ( ( string ) version.Rows [ 0 ] [ 0 ] ).StartsWith ( "PostgreSQL 17", StringComparison.Ordinal ), "version() looks like Postgres" );
			Expect ( ( string ) version.Rows [ 0 ] [ 3 ] == "170000", "current_setting casts to int" );

		}

		private static void Schema ( PgWire wire ) {

			Console.WriteLine ( "schema" );
			wire.Query ( "SET client_min_messages = warning; DROP SCHEMA IF EXISTS wire_check CASCADE; CREATE SCHEMA wire_check; SET search_path TO wire_check" );
			wire.Query (
				"CREATE TABLE accounts ( id BIGINT GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY, name TEXT NOT NULL, cash NUMERIC(20,4) NOT NULL );" +
				"CREATE TABLE orders ( id BIGINT GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY, account_id BIGINT NOT NULL REFERENCES accounts ( id ), symbol VARCHAR(16) NOT NULL, " +
				"side CHAR(1) NOT NULL CHECK ( side IN ( 'B', 'S' ) ), quantity INTEGER NOT NULL CHECK ( quantity > 0 ), limit_price NUMERIC(18,4) NOT NULL, filled INTEGER NOT NULL DEFAULT 0, " +
				"status TEXT NOT NULL DEFAULT 'open', placed TIMESTAMPTZ NOT NULL DEFAULT now() );" +
				"CREATE INDEX orders_symbol_status ON orders ( symbol, status );" +
				"CREATE INDEX orders_account ON orders ( account_id );" +
				"CREATE TABLE fills ( id BIGINT GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY, order_id BIGINT NOT NULL REFERENCES orders ( id ), quantity INTEGER NOT NULL, " +
				"price NUMERIC(18,4) NOT NULL, filled_at TIMESTAMPTZ NOT NULL );" +
				"CREATE INDEX fills_order ON fills ( order_id )" );

			PgResult exists = wire.Query ( "SELECT EXISTS (SELECT 1 FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace WHERE n.nspname='wire_check' AND c.relname='orders')" ) [ 0 ];

			Expect ( ( string ) exists.Rows [ 0 ] [ 0 ] == "t", "EF Core's table check finds orders" );

			PgResult columns = wire.Query ( "SELECT column_name, data_type FROM information_schema.columns WHERE table_schema = 'wire_check' AND table_name = 'orders' ORDER BY ordinal_position" ) [ 0 ];

			Expect ( columns.Rows.Count == 9 && ( string ) columns.Rows [ 6 ] [ 0 ] == "filled", "information_schema lists the order columns" );

		}

		private static long [ ] Accounts ( PgWire wire, int count ) {

			PgStatement insert = wire.Prepare ( "INSERT INTO accounts ( name, cash ) VALUES ( $1, $2 ) RETURNING id, cash", PgWire.Text, PgWire.Numeric );
			long [ ] ids = new long [ count ];

			for ( int i = 0; i < count; i++ ) {

				PgResult made = wire.Execute ( insert, "desk-" + i, 1000000.25m + i );

				ids [ i ] = ( long ) made.Rows [ 0 ] [ 0 ];

				if ( i == 0 )
					Expect ( made.Rows [ 0 ] [ 1 ] is decimal && ( decimal ) made.Rows [ 0 ] [ 1 ] == 1000000.25m, "numeric round-trips exactly through binary" );

			}

			return ids;

		}

		private static void Trading ( PgWire wire, int orderCount ) {

			Console.WriteLine ( "orders and fills, " + orderCount.ToString ( "N0", CultureInfo.InvariantCulture ) + " orders" );

			long [ ] accounts = Accounts ( wire, 20 );
			Random random = new Random ( 7 );
			PgStatement place = wire.Prepare ( "INSERT INTO orders ( account_id, symbol, side, quantity, limit_price ) VALUES ( $1, $2, $3, $4, $5 ) RETURNING id, placed, status", PgWire.Int8, PgWire.Varchar, PgWire.Text, PgWire.Int4, PgWire.Numeric );
			PgStatement fill = wire.Prepare ( "INSERT INTO fills ( order_id, quantity, price, filled_at ) VALUES ( $1, $2, $3, $4 ) RETURNING id", PgWire.Int8, PgWire.Int4, PgWire.Numeric, PgWire.TimestampTz );
			PgStatement apply = wire.Prepare ( "UPDATE orders SET filled = filled + $2, status = CASE WHEN filled + $2 >= quantity THEN 'filled' ELSE 'partial' END WHERE id = $1 RETURNING filled, status", PgWire.Int8, PgWire.Int4 );
			PgStatement book = wire.Prepare ( "SELECT id, side, quantity - filled, limit_price FROM orders WHERE symbol = $1 AND status = 'open' ORDER BY limit_price DESC, id LIMIT 20", PgWire.Varchar );
			PgStatement cancel = wire.Prepare ( "UPDATE orders SET status = 'cancelled' WHERE symbol = $1 AND status = 'partial' RETURNING id", PgWire.Varchar );
			long [ ] orders = new long [ orderCount ];
			int [ ] sizes = new int [ orderCount ];
			decimal [ ] prices = new decimal [ orderCount ];
			List < double > latency = new List < double > ( );
			Stopwatch clock = Stopwatch.StartNew ( );

			for ( int i = 0; i < orderCount; i++ ) {

				long started = Stopwatch.GetTimestamp ( );

				sizes [ i ] = 1 + random.Next ( 1000 );
				prices [ i ] = Math.Round ( 50m + ( decimal ) random.NextDouble ( ) * 450m, 4 );

				PgResult placed = wire.Execute ( place, accounts [ i % accounts.Length ], Symbols [ i % Symbols.Length ], i % 2 == 0 ? "B" : "S", sizes [ i ], prices [ i ] );

				orders [ i ] = ( long ) placed.Rows [ 0 ] [ 0 ];
				latency.Add ( ( Stopwatch.GetTimestamp ( ) - started ) * 1e6 / Stopwatch.Frequency );

				if ( i == 0 ) {

					Expect ( placed.Rows [ 0 ] [ 1 ] is DateTime && Math.Abs ( ( ( DateTime ) placed.Rows [ 0 ] [ 1 ] - DateTime.UtcNow ).TotalMinutes ) < 5, "the placed default is now() as a timestamptz" );
					Expect ( ( string ) placed.Rows [ 0 ] [ 2 ] == "open", "the status default comes back through RETURNING" );

				}

			}

			clock.Stop ( );
			Console.WriteLine ( "  place order (autocommit)      " + Rate ( orderCount, clock ) + "  " + Percentiles ( latency ) );
			Expect ( orders.Distinct ( ).Count ( ) == orderCount && orders [ orderCount - 1 ] > orders [ 0 ], "every order gets its own increasing id" );

			latency.Clear ( );
			clock.Restart ( );

			long filledTotal = 0;
			int fillCount = 0;

			for ( int i = 0; i < orderCount; i += 2 ) {

				long started = Stopwatch.GetTimestamp ( );
				int quantity = i % 4 == 0 ? sizes [ i ] : Math.Max ( 1, sizes [ i ] / 2 );

				wire.Query ( "BEGIN" );
				wire.Execute ( fill, orders [ i ], quantity, prices [ i ], DateTime.UtcNow );

				PgResult applied = wire.Execute ( apply, orders [ i ], quantity );

				wire.Query ( "COMMIT" );
				filledTotal += quantity;
				fillCount++;
				latency.Add ( ( Stopwatch.GetTimestamp ( ) - started ) * 1e6 / Stopwatch.Frequency );

				if ( i == 0 )
					Expect ( ( int ) applied.Rows [ 0 ] [ 0 ] == quantity && ( string ) applied.Rows [ 0 ] [ 1 ] == "filled", "a full fill marks the order filled" );

				if ( i == 2 )
					Expect ( ( string ) applied.Rows [ 0 ] [ 1 ] == ( quantity >= sizes [ i ] ? "filled" : "partial" ), "a half fill marks the order partial" );

			}

			clock.Stop ( );
			Console.WriteLine ( "  fill (BEGIN, 2 writes, COMMIT)" + Rate ( fillCount, clock ) + "  " + Percentiles ( latency ) );

			clock.Restart ( );

			int bookReads = 0;

			for ( int r = 0; r < 2000; r++ ) {

				PgResult top = wire.Execute ( book, Symbols [ r % Symbols.Length ] );

				bookReads++;

				if ( r == 1 )
					Expect ( top.Rows.Count > 0 && top.Rows.Count <= 20 && top.Rows [ 0 ] [ 3 ] is decimal && top.Rows [ 0 ] [ 2 ] is int && ( int ) top.Rows [ 0 ] [ 2 ] > 0, "the order book read returns typed rows" );

			}

			clock.Stop ( );
			Console.WriteLine ( "  order book top 20             " + Rate ( bookReads, clock ) );

			clock.Restart ( );

			PgResult positions = wire.Query (
				"SELECT o.account_id, o.symbol, sum ( CASE WHEN o.side = 'B' THEN f.quantity ELSE -f.quantity END ) AS position, " +
				"sum ( f.quantity * f.price ) / sum ( f.quantity ) AS vwap FROM fills f JOIN orders o ON o.id = f.order_id GROUP BY o.account_id, o.symbol ORDER BY o.account_id, o.symbol" ) [ 0 ];

			clock.Stop ( );
			Console.WriteLine ( string.Format ( CultureInfo.InvariantCulture, "  positions and VWAP            {0,8:N0} rows in {1:N2} ms", positions.Rows.Count, clock.Elapsed.TotalMilliseconds ) );
			Expect ( positions.Rows.Count > 0, "positions come back" );

			int cancelled = 0;

			foreach ( string symbol in Symbols )
				cancelled += wire.Execute ( cancel, symbol ).Rows.Count;

			PgResult totals = wire.Query ( "SELECT ( SELECT sum ( quantity ) FROM fills ), ( SELECT sum ( filled ) FROM orders ), ( SELECT count ( * ) FROM fills ), ( SELECT count ( * ) FROM orders WHERE status = 'cancelled' ), ( SELECT count ( * ) FROM orders WHERE status = 'open' )" ) [ 0 ];

			Expect ( long.Parse ( ( string ) totals.Rows [ 0 ] [ 0 ], CultureInfo.InvariantCulture ) == filledTotal, "fill quantities add up" );
			Expect ( long.Parse ( ( string ) totals.Rows [ 0 ] [ 1 ], CultureInfo.InvariantCulture ) == filledTotal, "order fill counters match the fills" );
			Expect ( int.Parse ( ( string ) totals.Rows [ 0 ] [ 2 ], CultureInfo.InvariantCulture ) == fillCount, "every fill was stored once" );
			Expect ( int.Parse ( ( string ) totals.Rows [ 0 ] [ 3 ], CultureInfo.InvariantCulture ) == cancelled, "cancels report the rows they changed" );

			bool refused = false;

			try {

				wire.Execute ( fill, 999999999L, 1, 1m, DateTime.UtcNow );

			} catch ( PgException error ) {

				refused = error.Code == "23503";

			}

			Expect ( refused, "a fill for a missing order fails with SQLSTATE 23503" );
			Expect ( !wire.InTransaction, "the failed statement does not leave a transaction open" );

			refused = false;

			try {

				wire.Execute ( place, accounts [ 0 ], "AAPL", "X", 10, 1m );

			} catch ( PgException error ) {

				refused = error.Code == "23514";

			}

			Expect ( refused, "a bad side fails its CHECK with SQLSTATE 23514" );

			wire.Query ( "BEGIN" );
			wire.Execute ( place, accounts [ 0 ], "AAPL", "B", 5, 1m );
			wire.Query ( "ROLLBACK" );
			Expect ( wire.Query ( "SELECT count ( * ) FROM orders WHERE quantity = 5 AND limit_price = 1" ) [ 0 ].Rows [ 0 ] [ 0 ].ToString ( ) == "0", "ROLLBACK discards an order" );

		}

		private static void Concurrent ( string [ ] args, int clients, int each ) {

			Console.WriteLine ( "concurrent clients, " + clients + " connections" );

			Exception problem = null;
			Stopwatch clock = Stopwatch.StartNew ( );
			Thread [ ] threads = new Thread [ clients ];

			for ( int c = 0; c < clients; c++ ) {

				int client = c;

				threads [ c ] = new Thread ( ( ) => {

					try {

						using ( PgWire wire = Connect ( args ) ) {

							wire.Query ( "SET search_path TO wire_check" );

							PgStatement place = wire.Prepare ( "INSERT INTO orders ( account_id, symbol, side, quantity, limit_price ) VALUES ( ( SELECT min ( id ) FROM accounts ), $1, 'B', $2, $3 ) RETURNING id", PgWire.Varchar, PgWire.Int4, PgWire.Numeric );
							PgStatement read = wire.Prepare ( "SELECT count ( * ) FROM orders WHERE symbol = $1 AND status = 'open'", PgWire.Varchar );

							for ( int i = 0; i < each; i++ ) {

								wire.Execute ( place, "C" + client, 1 + i, 10.5m );
								wire.Execute ( read, "C" + client );

							}

						}

					} catch ( Exception error ) {

						problem = error;

					}

				} );

				threads [ c ].Start ( );

			}

			foreach ( Thread thread in threads )
				thread.Join ( );

			clock.Stop ( );
			Expect ( problem == null, "every client finished" + ( problem == null ? "" : ": " + problem.Message ) );
			Console.WriteLine ( "  place + read per client       " + Rate ( clients * each * 2, clock ) );

			using ( PgWire wire = Connect ( args ) ) {

				PgResult count = wire.Query ( "SELECT count ( * ), count ( DISTINCT id ) FROM wire_check.orders WHERE symbol LIKE 'C%'" ) [ 0 ];

				Expect ( count.Rows [ 0 ] [ 0 ].ToString ( ) == ( clients * each ).ToString ( CultureInfo.InvariantCulture ) && count.Rows [ 0 ] [ 1 ].Equals ( count.Rows [ 0 ] [ 0 ] ), "every concurrent order landed once with its own id" );
				wire.Query ( "DROP SCHEMA wire_check CASCADE" );

			}

		}

		private static int Main ( string [ ] args ) {

			int orders = int.Parse ( Option ( args, "--orders", "5000" ), CultureInfo.InvariantCulture );
			int clients = int.Parse ( Option ( args, "--clients", "8" ), CultureInfo.InvariantCulture );

			try {

				using ( PgWire wire = Connect ( args ) ) {

					Handshake ( wire );
					Schema ( wire );
					Trading ( wire, orders );

				}

				Concurrent ( args, clients, Math.Max ( 1, orders / clients / 4 ) );

			} catch ( Exception error ) {

				failures++;
				Console.WriteLine ( "  stopped  " + error.GetType ( ).Name + ": " + error.Message );

			}

			Console.WriteLine ( ( checks - failures ) + " of " + checks + " checks passed" );

			return failures == 0 ? 0 : 1;

		}

	}

}
