using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;

namespace Strata.Replay {

	internal sealed class LogEntry {

		public string Session;
		public string Sql;
		public int Line;

	}

	internal static class LogReader {

		private static readonly Regex Prefixed = new Regex ( @"^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}", RegexOptions.Compiled );
		private static readonly Regex Session = new Regex ( @"\[(\d+)\]", RegexOptions.Compiled );
		private static readonly Regex Statement = new Regex ( @"(?:LOG|ERROR|WARNING):\s+(?:duration: [\d.]+ ms\s+)?(?:statement|execute [^:]*):\s?(.*)$", RegexOptions.Compiled | RegexOptions.Singleline );
		private static readonly Regex Parameters = new Regex ( @"DETAIL:\s+Parameters: (.*)$", RegexOptions.Compiled | RegexOptions.Singleline );
		private static readonly Regex Placeholder = new Regex ( @"\$(\d+)", RegexOptions.Compiled );

		public static List < LogEntry > Read ( string path, string format ) {

			if ( format == "auto" )
				format = path.EndsWith ( ".csv", StringComparison.OrdinalIgnoreCase ) ? "csv" : path.EndsWith ( ".sql", StringComparison.OrdinalIgnoreCase ) ? "sql" : "stderr";

			switch ( format ) {

				case "csv": return ReadCsv ( path );
				case "sql": return ReadSql ( path );
				default: return ReadStderr ( path );

			}

		}

		private static List < LogEntry > ReadStderr ( string path ) {

			List < string > records = new List < string > ( );
			List < int > lines = new List < int > ( );
			int number = 0;

			foreach ( string raw in File.ReadLines ( path ) ) {

				number++;

				if ( Prefixed.IsMatch ( raw ) || records.Count == 0 ) {

					records.Add ( raw );
					lines.Add ( number );

				} else
					records [ records.Count - 1 ] += "\n" + ( raw.StartsWith ( "\t" ) ? raw.Substring ( 1 ) : raw );

			}

			List < LogEntry > entries = new List < LogEntry > ( );

			for ( int i = 0; i < records.Count; i++ ) {

				Match found = Statement.Match ( records [ i ] );

				if ( !found.Success )
					continue;

				Match session = Session.Match ( records [ i ] );
				string sql = found.Groups [ 1 ].Value;

				if ( i + 1 < records.Count ) {

					Match values = Parameters.Match ( records [ i + 1 ] );

					if ( values.Success )
						sql = Bind ( sql, values.Groups [ 1 ].Value );

				}

				entries.Add ( new LogEntry { Session = session.Success ? session.Groups [ 1 ].Value : "0", Sql = sql.Trim ( ), Line = lines [ i ] } );

			}

			return entries;

		}

		private static List < LogEntry > ReadCsv ( string path ) {

			List < LogEntry > entries = new List < LogEntry > ( );
			string text = File.ReadAllText ( path );
			int at = 0;
			int line = 1;

			while ( at < text.Length ) {

				int start = line;
				List < string > fields = CsvRecord ( text, ref at, ref line );

				if ( fields.Count < 15 )
					continue;

				Match found = Statement.Match ( "LOG:  " + fields [ 13 ] );

				if ( !found.Success )
					continue;

				string sql = found.Groups [ 1 ].Value;
				Match values = Parameters.Match ( "DETAIL:  " + fields [ 14 ] );

				if ( values.Success )
					sql = Bind ( sql, values.Groups [ 1 ].Value );

				entries.Add ( new LogEntry { Session = fields [ 5 ], Sql = sql.Trim ( ), Line = start } );

			}

			return entries;

		}

		private static List < string > CsvRecord ( string text, ref int at, ref int line ) {

			List < string > fields = new List < string > ( );
			StringBuilder field = new StringBuilder ( );
			bool quoted = false;

			while ( at < text.Length ) {

				char c = text [ at++ ];

				if ( quoted ) {

					if ( c == '"' && at < text.Length && text [ at ] == '"' ) {

						field.Append ( '"' );
						at++;

					} else if ( c == '"' )
						quoted = false;
					else {

						if ( c == '\n' )
							line++;

						field.Append ( c );

					}

					continue;

				}

				if ( c == '"' )
					quoted = true;
				else if ( c == ',' ) {

					fields.Add ( field.ToString ( ) );
					field.Clear ( );

				} else if ( c == '\n' ) {

					line++;

					break;

				} else if ( c != '\r' )
					field.Append ( c );

			}

			fields.Add ( field.ToString ( ) );

			return fields;

		}

		private static List < LogEntry > ReadSql ( string path ) {

			List < LogEntry > entries = new List < LogEntry > ( );
			string text = File.ReadAllText ( path );
			StringBuilder current = new StringBuilder ( );
			int line = 1;
			int start = 1;
			char quote = '\0';

			for ( int i = 0; i < text.Length; i++ ) {

				char c = text [ i ];

				if ( c == '\n' )
					line++;

				if ( quote != '\0' ) {

					current.Append ( c );

					if ( c == quote )
						quote = '\0';

					continue;

				}

				if ( c == '-' && i + 1 < text.Length && text [ i + 1 ] == '-' ) {

					while ( i < text.Length && text [ i ] != '\n' )
						i++;

					line++;
					current.Append ( '\n' );

					continue;

				}

				if ( c == '\'' || c == '"' ) {

					quote = c;
					current.Append ( c );

					continue;

				}

				if ( c == ';' ) {

					string sql = current.ToString ( ).Trim ( );

					if ( sql.Length > 0 )
						entries.Add ( new LogEntry { Session = "0", Sql = sql, Line = start } );

					current.Clear ( );
					start = line;

					continue;

				}

				if ( current.Length == 0 && char.IsWhiteSpace ( c ) ) {

					start = line;

					continue;

				}

				current.Append ( c );

			}

			string last = current.ToString ( ).Trim ( );

			if ( last.Length > 0 )
				entries.Add ( new LogEntry { Session = "0", Sql = last, Line = start } );

			return entries;

		}

		private static string Bind ( string sql, string list ) {

			Dictionary < string, string > values = new Dictionary < string, string > ( );
			int at = 0;

			while ( at < list.Length ) {

				while ( at < list.Length && ( list [ at ] == ' ' || list [ at ] == ',' ) )
					at++;

				if ( at >= list.Length || list [ at ] != '$' )
					break;

				int name_start = ++at;

				while ( at < list.Length && char.IsDigit ( list [ at ] ) )
					at++;

				string name = list.Substring ( name_start, at - name_start );

				while ( at < list.Length && ( list [ at ] == ' ' || list [ at ] == '=' ) )
					at++;

				if ( at < list.Length && list [ at ] == '\'' ) {

					StringBuilder value = new StringBuilder ( "'" );

					at++;

					while ( at < list.Length ) {

						if ( list [ at ] == '\'' && at + 1 < list.Length && list [ at + 1 ] == '\'' ) {

							value.Append ( "''" );
							at += 2;

							continue;

						}

						if ( list [ at ] == '\'' ) {

							at++;

							break;

						}

						value.Append ( list [ at++ ] );

					}

					values [ name ] = value.Append ( '\'' ).ToString ( );

				} else {

					int value_start = at;

					while ( at < list.Length && list [ at ] != ',' )
						at++;

					values [ name ] = list.Substring ( value_start, at - value_start ).Trim ( );

				}

			}

			return Placeholder.Replace ( sql, match => values.TryGetValue ( match.Groups [ 1 ].Value, out string value ) ? value : match.Value );

		}

	}

}
