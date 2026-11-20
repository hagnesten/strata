using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using Strata.Wire;

namespace Strata.Replay {

	internal sealed class Endpoint {

		public string Host;
		public int Port;
		public string User;
		public string Password;
		public string Database;

		public static Endpoint Parse ( string text ) {

			string [ ] parts = text.Split ( ':' );

			if ( parts.Length != 5 )
				throw new ArgumentException ( "an endpoint is host:port:user:password:database" );

			return new Endpoint { Host = parts [ 0 ], Port = int.Parse ( parts [ 1 ], CultureInfo.InvariantCulture ), User = parts [ 2 ], Password = parts [ 3 ], Database = parts [ 4 ] };

		}

		public PgWire Open ( ) => new PgWire ( Host, Port, User, Password, Database );

	}

	internal sealed class Outcome {

		public string Error;
		public List < PgResult > Results;
		public double Milliseconds;

	}

	internal static class Program {

		private static readonly Regex Reading = new Regex ( @"^\s*(\(\s*)*(select|with|values|show|table)\b", RegexOptions.IgnoreCase | RegexOptions.Compiled );
		private static readonly Regex Writing = new Regex ( @"\b(insert|update|delete|merge|nextval|setval|create|drop|alter|truncate|copy|grant|revoke|vacuum|lock)\b", RegexOptions.IgnoreCase | RegexOptions.Compiled );
		private static readonly Regex Ordered = new Regex ( @"\border\s+by\b", RegexOptions.IgnoreCase | RegexOptions.Compiled );
		private static readonly Regex Volatile = new Regex ( @"\b(now|random|clock_timestamp|statement_timestamp|transaction_timestamp|current_timestamp|current_date|current_time|localtimestamp|gen_random_uuid|uuid_generate_v4|pg_backend_pid|version|txid_current|inet_server_port)\b", RegexOptions.IgnoreCase | RegexOptions.Compiled );

		private static int Usage ( ) {

			Console.Error.WriteLine ( "Strata.Replay --log FILE --postgres host:port:user:password:db --strata host:port:user:password:db" );
			Console.Error.WriteLine ( "  [--format auto|stderr|csv|sql] [--copy] [--writes] [--limit N] [--report FILE] [--show N]" );
			Console.Error.WriteLine ( "Replays statements from a Postgres log against both servers and reports every difference." );
			Console.Error.WriteLine ( "Only reads are replayed unless --writes is given; point --writes at copies, never at production." );

			return 2;

		}

		private static Outcome Run ( PgWire wire, string sql ) {

			Stopwatch clock = Stopwatch.StartNew ( );
			Outcome outcome = new Outcome ( );

			try {

				outcome.Results = wire.Query ( sql );

			} catch ( PgException error ) {

				outcome.Error = error.Code;

			} catch ( Exception error ) {

				outcome.Error = "client: " + error.Message;

			}

			outcome.Milliseconds = clock.Elapsed.TotalMilliseconds;

			return outcome;

		}

		private static bool SameCell ( object a, object b, uint type ) {

			string x = a as string;
			string y = b as string;

			if ( x == null || y == null )
				return x == y;

			if ( x == y )
				return true;

			if ( ( type == PgWire.Float4 || type == PgWire.Float8 ) && double.TryParse ( x, NumberStyles.Float, CultureInfo.InvariantCulture, out double p ) && double.TryParse ( y, NumberStyles.Float, CultureInfo.InvariantCulture, out double q ) )
				return Math.Abs ( p - q ) <= 1e-9 * Math.Max ( 1.0, Math.Max ( Math.Abs ( p ), Math.Abs ( q ) ) );

			return false;

		}

		private static string RowKey ( object [ ] row ) => string.Join ( "\u0001", row.Select ( cell => cell as string ?? "\u0000" ) );

		private static string Compare ( string sql, Outcome expected, Outcome actual ) {

			if ( expected.Error != null || actual.Error != null )
				return expected.Error == actual.Error ? null : "error " + ( expected.Error ?? "none" ) + " on Postgres, " + ( actual.Error ?? "none" ) + " on Strata";

			if ( expected.Results.Count != actual.Results.Count )
				return "result count " + expected.Results.Count + " vs " + actual.Results.Count;

			bool ordered = Ordered.IsMatch ( sql );

			for ( int r = 0; r < expected.Results.Count; r++ ) {

				PgResult a = expected.Results [ r ];
				PgResult b = actual.Results [ r ];

				if ( a.Tag != b.Tag )
					return "tag " + a.Tag + " vs " + b.Tag;

				if ( a.Columns.Count != b.Columns.Count )
					return "column count " + a.Columns.Count + " vs " + b.Columns.Count;

				List < object [ ] > left = a.Rows;
				List < object [ ] > right = b.Rows;

				if ( !ordered ) {

					left = left.OrderBy ( RowKey, StringComparer.Ordinal ).ToList ( );
					right = right.OrderBy ( RowKey, StringComparer.Ordinal ).ToList ( );

				}

				for ( int i = 0; i < left.Count; i++ )
					for ( int c = 0; c < a.Columns.Count; c++ )
						if ( !SameCell ( left [ i ] [ c ], right [ i ] [ c ], a.Columns [ c ].Type ) )
							return "row " + i + " column " + a.Columns [ c ].Name + ": " + ( left [ i ] [ c ] ?? "NULL" ) + " vs " + ( right [ i ] [ c ] ?? "NULL" );

				for ( int c = 0; c < a.Columns.Count; c++ )
					if ( a.Columns [ c ].Name != b.Columns [ c ].Name )
						return "column name " + a.Columns [ c ].Name + " vs " + b.Columns [ c ].Name;

			}

			return null;

		}

		public static int Main ( string [ ] args ) {

			string path = null;
			string format = "auto";
			string report = null;
			Endpoint postgres = null;
			Endpoint strata = null;
			bool copy = false;
			bool writes = false;
			int limit = int.MaxValue;
			int show = 20;

			for ( int i = 0; i < args.Length; i++ ) {

				string next = i + 1 < args.Length ? args [ i + 1 ] : null;

				switch ( args [ i ] ) {

					case "--log": path = next; i++; break;
					case "--format": format = next; i++; break;
					case "--report": report = next; i++; break;
					case "--postgres": postgres = Endpoint.Parse ( next ); i++; break;
					case "--strata": strata = Endpoint.Parse ( next ); i++; break;
					case "--limit": limit = int.Parse ( next, CultureInfo.InvariantCulture ); i++; break;
					case "--show": show = int.Parse ( next, CultureInfo.InvariantCulture ); i++; break;
					case "--copy": copy = true; break;
					case "--writes": writes = true; break;
					default: return Usage ( );

				}

			}

			if ( path == null || postgres == null || strata == null )
				return Usage ( );

			List < LogEntry > entries = LogReader.Read ( path, format );
			TextWriter output = Console.Out;

			output.WriteLine ( "read " + entries.Count + " statements from " + path );

			if ( copy ) {

				using PgWire source = postgres.Open ( );
				using PgWire target = strata.Open ( );

				Copier.Copy ( source, target, output );

			}

			Dictionary < string, PgWire [ ] > sessions = new Dictionary < string, PgWire [ ] > ( );
			StringBuilder details = new StringBuilder ( );
			int replayed = 0, same = 0, different = 0, skipped = 0, volatile_skips = 0;
			double postgres_ms = 0, strata_ms = 0;

			try {

				foreach ( LogEntry entry in entries ) {

					if ( replayed >= limit )
						break;

					bool read = Reading.IsMatch ( entry.Sql ) && !Writing.IsMatch ( entry.Sql );
					bool control = Regex.IsMatch ( entry.Sql, @"^\s*(begin|start|commit|end|rollback|savepoint|release|set|reset|discard|deallocate)\b", RegexOptions.IgnoreCase );

					if ( !read && !writes && !control ) {

						skipped++;

						continue;

					}

					if ( !sessions.TryGetValue ( entry.Session, out PgWire [ ] pair ) ) {

						pair = new [ ] { postgres.Open ( ), strata.Open ( ) };
						pair [ 0 ].Query ( "SET TimeZone = 'UTC'" );
						sessions [ entry.Session ] = pair;

					}

					Outcome expected = Run ( pair [ 0 ], entry.Sql );
					Outcome actual = Run ( pair [ 1 ], entry.Sql );

					if ( control ) {

						if ( Regex.IsMatch ( entry.Sql, @"^\s*(discard|reset)\b", RegexOptions.IgnoreCase ) )
							Run ( pair [ 0 ], "SET TimeZone = 'UTC'" );

						continue;

					}

					replayed++;
					postgres_ms += expected.Milliseconds;
					strata_ms += actual.Milliseconds;

					if ( Volatile.IsMatch ( entry.Sql ) && expected.Error == null && actual.Error == null ) {

						volatile_skips++;
						same++;

						continue;

					}

					string difference = Compare ( entry.Sql, expected, actual );

					if ( difference == null ) {

						same++;

						continue;

					}

					different++;

					string line = "line " + entry.Line + " session " + entry.Session + ": " + difference + "\n    " + ( entry.Sql.Length > 400 ? entry.Sql.Substring ( 0, 400 ) + "..." : entry.Sql ).Replace ( "\n", "\n    " );

					details.AppendLine ( line );

					if ( different <= show )
						output.WriteLine ( line );

				}

			} finally {

				foreach ( PgWire [ ] pair in sessions.Values ) {

					pair [ 0 ].Dispose ( );
					pair [ 1 ].Dispose ( );

				}

			}

			output.WriteLine ( );
			output.WriteLine ( "replayed " + replayed + ", same " + same + ", different " + different + ", skipped writes " + skipped + ", time-dependent compared by outcome only " + volatile_skips );
			output.WriteLine ( "total time: Postgres " + postgres_ms.ToString ( "0", CultureInfo.InvariantCulture ) + " ms, Strata " + strata_ms.ToString ( "0", CultureInfo.InvariantCulture ) + " ms" );

			if ( report != null )
				File.WriteAllText ( report, details.ToString ( ) );

			return different == 0 ? 0 : 1;

		}

	}

}
