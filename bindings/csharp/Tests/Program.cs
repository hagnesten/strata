using System;
using System.Collections.Generic;
using System.Data;
using System.Diagnostics;
using System.Globalization;
using System.IO;

namespace Strata.Data.Tests {

	internal static unsafe class Program {

		private static int checks;
		private static int failures;

		private static void Expect ( bool fine, string what ) {

			checks++;

			if ( fine )
				return;

			failures++;
			Console.WriteLine ( "  failed  " + what );

		}

		private static string Fresh ( string name ) {

			string path = Path.Combine ( Path.GetTempPath ( ), name );

			foreach ( string extra in new [ ] { "", "-wal", "-shm" } )
				if ( File.Exists ( path + extra ) )
					File.Delete ( path + extra );

			return path;

		}

		private static void Behaviour ( ) {

			Console.WriteLine ( "provider" );

			string path = Fresh ( "strata-csharp-test.sdb" );

			using ( StrataConnection connection = new StrataConnection ( "Data Source=" + path ) ) {

				connection.Open ( );
				Expect ( StrataConnection.Version.Length > 0, "the native library reports a version" );
				connection.Execute ( "CREATE TABLE trades ( id INTEGER PRIMARY KEY, symbol TEXT NOT NULL, price TEXT, size INTEGER, at TEXT, UNIQUE ( symbol, at ) )" );

				using ( StrataCommand insert = new StrataCommand ( "INSERT INTO trades ( symbol, price, size, at ) VALUES ( @symbol, @price, @size, @at )", connection ) ) {

					insert.Parameters.AddWithValue ( "@symbol", "EURUSD" );
					insert.Parameters.AddWithValue ( "@price", 1.08425m );
					insert.Parameters.AddWithValue ( "@size", 1000000 );
					insert.Parameters.AddWithValue ( "@at", new DateTime ( 2026, 11, 19, 14, 30, 0, 250 ) );
					Expect ( insert.ExecuteNonQuery ( ) == 1, "an insert reports one change" );
					Expect ( connection.LastInsertRowId == 1, "the row id is reported" );

					bool raised = false;

					try {

						insert.ExecuteNonQuery ( );

					} catch ( StrataException failure ) {

						raised = failure.Code == 19 && failure.Message.Contains ( "UNIQUE" );

					}

					Expect ( raised, "a duplicate raises a constraint exception" );

				}

				using ( StrataCommand read = new StrataCommand ( "SELECT symbol, price, size, at FROM trades WHERE id = :id", connection ) ) {

					read.Parameters.AddWithValue ( "id", 1 );

					using ( StrataDataReader reader = read.ExecuteReader ( ) ) {

						Expect ( reader.FieldCount == 4 && reader.GetName ( 1 ) == "price", "a reader knows its columns" );
						Expect ( reader.Read ( ), "a reader finds the row" );
						Expect ( reader.GetString ( 0 ) == "EURUSD" && reader.GetDecimal ( 1 ) == 1.08425m && reader.GetInt32 ( 2 ) == 1000000, "values come back exactly" );
						Expect ( reader.GetDateTime ( 3 ) == new DateTime ( 2026, 11, 19, 14, 30, 0, 250 ), "dates round trip" );
						Expect ( !reader.Read ( ), "the reader ends" );

					}

				}

				using ( StrataTransaction transaction = connection.BeginTransaction ( ) ) {

					using ( StrataBulkInserter bulk = connection.CreateBulkInserter ( "INSERT INTO trades ( symbol, price, size, at ) VALUES ( ?, ?, ?, ? )", 4, 1000 ) ) {

						for ( int i = 0; i < 25000; i++ ) {

							bulk.Add ( i % 3 == 0 ? "GBPUSD" : "USDJPY" ).Add ( 100.5 + i ).Add ( i ).Add ( "t" + i.ToString ( CultureInfo.InvariantCulture ) );
							bulk.EndRow ( );

						}

					}

					transaction.Commit ( );

				}

				using ( StrataCommand count = new StrataCommand ( "SELECT count ( * ), sum ( size ) FROM trades", connection ) )
					Expect ( ( long ) count.ExecuteScalar ( ) == 25001, "the bulk rows are there" );

				using ( StrataTransaction transaction = connection.BeginTransaction ( ) ) {

					connection.Execute ( "DELETE FROM trades WHERE symbol = 'GBPUSD'" );
					transaction.Rollback ( );

				}

				using ( StrataCommand count = new StrataCommand ( "SELECT count ( * ) FROM trades WHERE symbol = 'GBPUSD'", connection ) )
					Expect ( ( long ) count.ExecuteScalar ( ) == 8334, "a rolled back delete changes nothing" );

				using ( StrataCommand scan = new StrataCommand ( "SELECT size FROM trades WHERE symbol = 'USDJPY' ORDER BY size", connection ) ) {

					long seen = 0;
					long sum = 0;

					using ( StrataDataReader reader = scan.ExecuteReader ( ) ) {

						while ( reader.Read ( ) ) {

							seen++;
							sum += reader.GetInt64 ( 0 );

						}

					}

					Expect ( seen == 16666, "a reader walks many blocks" );

					long wanted = 0;

					for ( int i = 0; i < 25000; i++ )
						if ( i % 3 != 0 )
							wanted += i;

					Expect ( sum == wanted, "every block holds the right values" );

				}

				using ( StrataCommand twice = new StrataCommand ( "UPDATE trades SET size = size + 1 WHERE id = 1; SELECT size FROM trades WHERE id = 1", connection ) )
					Expect ( ( long ) twice.ExecuteScalar ( ) == 1000001, "a command runs several statements" );

				DataTable table = new DataTable ( );

				using ( StrataCommand load = new StrataCommand ( "SELECT id, symbol FROM trades WHERE id <= 10", connection ) )
				using ( StrataDataReader reader = load.ExecuteReader ( ) )
					table.Load ( reader );

				Expect ( table.Rows.Count == 10 && table.Columns.Count == 2, "a DataTable loads from a reader" );

			}

			Threads ( path );
			Console.WriteLine ( "  " + ( checks - failures ).ToString ( CultureInfo.InvariantCulture ) + " of " + checks.ToString ( CultureInfo.InvariantCulture ) + " checks passed" );

		}

		private static void Threads ( string path ) {

			bool backwards = false;
			bool failed = false;
			long last = 0;
			bool writing = true;

			System.Threading.Thread reader = new System.Threading.Thread ( delegate ( ) {

				try {

					using ( StrataConnection connection = new StrataConnection ( "Data Source=" + path ) ) {

						connection.Open ( );

						using ( StrataCommand count = new StrataCommand ( "SELECT count ( * ) FROM trades", connection ) ) {

							while ( System.Threading.Volatile.Read ( ref writing ) ) {

								long seen = ( long ) count.ExecuteScalar ( );

								if ( seen < last )
									backwards = true;

								last = seen;

							}

						}

					}

				} catch ( Exception ) {

					failed = true;

				}

			} );

			reader.Start ( );

			using ( StrataConnection writer = new StrataConnection ( "Data Source=" + path + ";Busy Timeout=10000" ) ) {

				writer.Open ( );

				for ( int batch = 0; batch < 50; batch++ ) {

					using ( StrataTransaction transaction = writer.BeginTransaction ( ) ) {

						using ( StrataBulkInserter bulk = writer.CreateBulkInserter ( "INSERT INTO trades ( symbol, price, size, at ) VALUES ( ?, ?, ?, ? )", 4, 500 ) ) {

							for ( int i = 0; i < 400; i++ ) {

								bulk.Add ( "THREAD" ).Add ( i * 0.5 ).Add ( i ).Add ( "b" + batch.ToString ( CultureInfo.InvariantCulture ) + "r" + i.ToString ( CultureInfo.InvariantCulture ) );
								bulk.EndRow ( );

							}

						}

						transaction.Commit ( );

					}

				}

			}

			System.Threading.Volatile.Write ( ref writing, false );
			reader.Join ( );
			Expect ( !failed, "a reader thread runs beside a writer thread" );
			Expect ( !backwards, "a reader never sees the row count go backwards" );

			using ( StrataConnection check = new StrataConnection ( "Data Source=" + path ) ) {

				check.Open ( );

				using ( StrataCommand count = new StrataCommand ( "SELECT count ( * ) FROM trades WHERE symbol = 'THREAD'", check ) )
					Expect ( ( long ) count.ExecuteScalar ( ) == 20000, "every row from the writer thread is there" );

			}

		}

		private static readonly string [ ] Symbols = { "EURUSD", "GBPUSD", "USDJPY", "AUDUSD", "USDCHF", "NZDUSD", "USDCAD", "EURGBP" };

		private static void Line ( string what, double strata, double sqlite ) {

			double ratio = sqlite / strata;
			string verdict = ratio >= 1 ? ratio.ToString ( "0.00", CultureInfo.InvariantCulture ) + "x faster" : ( 1 / ratio ).ToString ( "0.00", CultureInfo.InvariantCulture ) + "x slower";

			Console.WriteLine ( "  " + what.PadRight ( 44 ) + ( strata.ToString ( "0.0", CultureInfo.InvariantCulture ) + " ms" ).PadLeft ( 12 ) + ( sqlite.ToString ( "0.0", CultureInfo.InvariantCulture ) + " ms" ).PadLeft ( 12 ) + "   " + verdict );

		}

		private static double Time ( Action work ) {

			Stopwatch clock = Stopwatch.StartNew ( );

			work ( );

			return clock.Elapsed.TotalMilliseconds;

		}

		private static void Race ( int rows ) {

			Console.WriteLine ( );
			Console.WriteLine ( "C# race, " + rows.ToString ( "N0", CultureInfo.InvariantCulture ) + " ticks" );
			Console.WriteLine ( "  " + "workload".PadRight ( 44 ) + "Strata".PadLeft ( 12 ) + "SQLite".PadLeft ( 12 ) );

			string strata_path = Fresh ( "strata-csharp-race.sdb" );
			string sqlite_path = Fresh ( "sqlite-csharp-race.db" );
			const string schema = "CREATE TABLE ticks ( id INTEGER PRIMARY KEY, symbol TEXT, price REAL, size INTEGER, at INTEGER )";
			const string insert_sql = "INSERT INTO ticks ( symbol, price, size, at ) VALUES ( ?, ?, ?, ? )";
			StrataConnection connection = new StrataConnection ( "Data Source=" + strata_path );
			IntPtr sqlite = SqliteRaw.Open ( sqlite_path );

			connection.Open ( );
			connection.Execute ( schema );
			SqliteRaw.Exec ( sqlite, schema );

			double strata_bulk = Time ( delegate {

				using ( StrataTransaction transaction = connection.BeginTransaction ( ) ) {

					using ( StrataBulkInserter bulk = connection.CreateBulkInserter ( insert_sql, 4 ) ) {

						for ( int i = 0; i < rows; i++ ) {

							bulk.Add ( Symbols [ i & 7 ] ).Add ( 1.0 + i * 0.0001 ).Add ( 100 + ( i % 900 ) ).Add ( 1700000000000L + i );
							bulk.EndRow ( );

						}

					}

					transaction.Commit ( );

				}

			} );

			double sqlite_bulk = Time ( delegate {

				SqliteRaw.Exec ( sqlite, "BEGIN" );

				IntPtr stmt = SqliteRaw.Prepare ( sqlite, insert_sql );

				for ( int i = 0; i < rows; i++ ) {

					string symbol = Symbols [ i & 7 ];

					fixed ( char* at = symbol )
						SqliteRaw.sqlite3_bind_text16 ( stmt, 1, at, symbol.Length * 2, new IntPtr ( -1 ) );

					SqliteRaw.sqlite3_bind_double ( stmt, 2, 1.0 + i * 0.0001 );
					SqliteRaw.sqlite3_bind_int64 ( stmt, 3, 100 + ( i % 900 ) );
					SqliteRaw.sqlite3_bind_int64 ( stmt, 4, 1700000000000L + i );
					SqliteRaw.sqlite3_step ( stmt );
					SqliteRaw.sqlite3_reset ( stmt );

				}

				SqliteRaw.sqlite3_finalize ( stmt );
				SqliteRaw.Exec ( sqlite, "COMMIT" );

			} );

			Line ( "insert ticks in one transaction", strata_bulk, sqlite_bulk );

			double strata_rows = Time ( delegate {

				using ( StrataCommand command = new StrataCommand ( "SELECT symbol, price, size FROM ticks", connection ) )
				using ( StrataDataReader reader = command.ExecuteReader ( ) ) {

					double sum = 0;

					while ( reader.Read ( ) ) {

						string symbol = reader.GetString ( 0 );

						sum += reader.GetDouble ( 1 ) * reader.GetInt64 ( 2 ) + symbol.Length;

					}

					Expect ( sum > 0, "the Strata read saw rows" );

				}

			} );

			double sqlite_rows = Time ( delegate {

				IntPtr stmt = SqliteRaw.Prepare ( sqlite, "SELECT symbol, price, size FROM ticks" );
				double sum = 0;

				while ( SqliteRaw.sqlite3_step ( stmt ) == SqliteRaw.Row ) {

					string symbol = SqliteRaw.Text ( stmt, 0 );

					sum += SqliteRaw.sqlite3_column_double ( stmt, 1 ) * SqliteRaw.sqlite3_column_int64 ( stmt, 2 ) + symbol.Length;

				}

				SqliteRaw.sqlite3_finalize ( stmt );
				Expect ( sum > 0, "the SQLite read saw rows" );

			} );

			Line ( "read every tick into C# values", strata_rows, sqlite_rows );

			int probes = Math.Min ( rows, 100000 );

			double strata_points = Time ( delegate {

				using ( StrataCommand command = new StrataCommand ( "SELECT price FROM ticks WHERE id = @id", connection ) ) {

					StrataParameter id = command.Parameters.AddWithValue ( "@id", 0L );
					double sum = 0;

					for ( int i = 0; i < probes; i++ ) {

						id.Value = ( long ) ( ( i * 7919L ) % rows + 1 );
						sum += ( double ) command.ExecuteScalar ( );

					}

					Expect ( sum > 0, "the Strata lookups found rows" );

				}

			} );

			double sqlite_points = Time ( delegate {

				IntPtr stmt = SqliteRaw.Prepare ( sqlite, "SELECT price FROM ticks WHERE id = ?" );
				double sum = 0;

				for ( int i = 0; i < probes; i++ ) {

					SqliteRaw.sqlite3_bind_int64 ( stmt, 1, ( i * 7919L ) % rows + 1 );

					if ( SqliteRaw.sqlite3_step ( stmt ) == SqliteRaw.Row )
						sum += SqliteRaw.sqlite3_column_double ( stmt, 0 );

					SqliteRaw.sqlite3_reset ( stmt );

				}

				SqliteRaw.sqlite3_finalize ( stmt );
				Expect ( sum > 0, "the SQLite lookups found rows" );

			} );

			Line ( "look up ticks by id", strata_points, sqlite_points );

			double strata_groups = Time ( delegate {

				using ( StrataCommand command = new StrataCommand ( "SELECT symbol, count ( * ), avg ( price ), max ( size ) FROM ticks GROUP BY symbol", connection ) )
				using ( StrataDataReader reader = command.ExecuteReader ( ) )
					while ( reader.Read ( ) ) {

					}

			} );

			double sqlite_groups = Time ( delegate {

				IntPtr stmt = SqliteRaw.Prepare ( sqlite, "SELECT symbol, count ( * ), avg ( price ), max ( size ) FROM ticks GROUP BY symbol" );

				while ( SqliteRaw.sqlite3_step ( stmt ) == SqliteRaw.Row ) {

				}

				SqliteRaw.sqlite3_finalize ( stmt );

			} );

			Line ( "group ticks by symbol", strata_groups, sqlite_groups );
			connection.Close ( );
			SqliteRaw.sqlite3_close_v2 ( sqlite );

		}

		private static int Main ( string [ ] arguments ) {

			Behaviour ( );

			int rows = 1000000;

			if ( arguments.Length > 0 && !int.TryParse ( arguments [ 0 ], NumberStyles.Integer, CultureInfo.InvariantCulture, out rows ) )
				rows = 1000000;

			if ( File.Exists ( Path.Combine ( AppDomain.CurrentDomain.BaseDirectory, "sqlite3.dll" ) ) || File.Exists ( Path.Combine ( AppDomain.CurrentDomain.BaseDirectory, "libsqlite3.so" ) ) )
				Race ( rows );
			else
				Console.WriteLine ( "put sqlite3.dll next to the tests to race against SQLite" );

			return failures == 0 ? 0 : 1;

		}

	}

}
