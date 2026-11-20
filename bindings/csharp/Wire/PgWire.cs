using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;

namespace Strata.Wire {

	internal sealed class PgException : Exception {

		public readonly string Code;

		public PgException ( string code, string message ) : base ( message + " (" + code + ")" ) {

			Code = code;

		}

	}

	internal sealed class PgColumn {

		public string Name;
		public uint Type;
		public short Format;

	}

	internal sealed class PgResult {

		public readonly List < PgColumn > Columns = new List < PgColumn > ( );
		public readonly List < object [ ] > Rows = new List < object [ ] > ( );
		public string Tag = "";

	}

	internal sealed class PgStatement {

		public string Name;
		public uint [ ] Parameters;
		public List < PgColumn > Columns = new List < PgColumn > ( );

	}

	internal sealed class PgWire : IDisposable {

		public const uint Bool = 16;
		public const uint Bytea = 17;
		public const uint Int8 = 20;
		public const uint Int2 = 21;
		public const uint Int4 = 23;
		public const uint Text = 25;
		public const uint Oid = 26;
		public const uint Float4 = 700;
		public const uint Float8 = 701;
		public const uint Varchar = 1043;
		public const uint Date = 1082;
		public const uint Timestamp = 1114;
		public const uint TimestampTz = 1184;
		public const uint Numeric = 1700;
		public const uint Uuid = 2950;

		private static readonly DateTime Epoch = new DateTime ( 2000, 1, 1, 0, 0, 0, DateTimeKind.Utc );

		private readonly TcpClient client;
		private readonly NetworkStream stream;
		private readonly BufferedStream reader;
		private readonly MemoryStream outgoing = new MemoryStream ( );
		private int named;

		public readonly Dictionary < string, string > Settings = new Dictionary < string, string > ( );
		public bool InTransaction;

		public PgWire ( string host, int port, string user, string password, string database ) {

			client = new TcpClient ( );
			client.NoDelay = true;
			client.Connect ( host, port );
			stream = client.GetStream ( );
			reader = new BufferedStream ( stream, 65536 );

			WriteInt32 ( 8 );
			WriteInt32 ( 80877103 );
			Flush ( );

			if ( ReadByte ( ) != 'N' )
				throw new PgException ( "08P01", "the server accepted TLS, which this check does not speak" );

			MemoryStream startup = new MemoryStream ( );

			WriteRawInt32 ( startup, 196608 );

			foreach ( string pair in new [ ] { "user", user, "database", database, "client_encoding", "UTF8", "application_name", "Strata wire check" } )
				WriteRawString ( startup, pair );

			startup.WriteByte ( 0 );
			WriteInt32 ( ( int ) startup.Length + 4 );
			outgoing.Write ( startup.ToArray ( ), 0, ( int ) startup.Length );
			Flush ( );
			SignIn ( user, password );

		}

		public void Dispose ( ) {

			try {

				Begin ( 'X' );
				End ( );
				Flush ( );

			} catch ( IOException ) {

			}

			client.Close ( );

		}

		private void SignIn ( string user, string password ) {

			string nonce = Convert.ToBase64String ( Guid.NewGuid ( ).ToByteArray ( ) ).TrimEnd ( '=' );
			string firstBare = "n=*,r=" + nonce;
			byte [ ] salted = null;
			string authMessage = null;

			for ( ;; ) {

				char type;
				byte [ ] body = ReadMessage ( out type );

				if ( type == 'E' )
					throw Error ( body );

				if ( type == 'R' ) {

					int code = Int32At ( body, 0 );

					if ( code == 0 )
						continue;

					if ( code == 10 ) {

						Begin ( 'p' );
						WriteString ( "SCRAM-SHA-256" );

						byte [ ] first = Encoding.UTF8.GetBytes ( "n,," + firstBare );

						WriteInt32 ( first.Length );
						outgoing.Write ( first, 0, first.Length );
						End ( );
						Flush ( );

						continue;

					}

					if ( code == 11 ) {

						string serverFirst = Encoding.UTF8.GetString ( body, 4, body.Length - 4 );
						Dictionary < string, string > fields = ScramFields ( serverFirst );

						if ( !fields [ "r" ].StartsWith ( nonce, StringComparison.Ordinal ) )
							throw new PgException ( "28000", "the server changed the client nonce" );

						salted = Pbkdf2 ( Encoding.UTF8.GetBytes ( password ), Convert.FromBase64String ( fields [ "s" ] ), int.Parse ( fields [ "i" ], CultureInfo.InvariantCulture ) );

						string finalBare = "c=biws,r=" + fields [ "r" ];
						byte [ ] clientKey = Hmac ( salted, "Client Key" );
						byte [ ] storedKey;

						using ( SHA256 sha = SHA256.Create ( ) )
							storedKey = sha.ComputeHash ( clientKey );

						authMessage = firstBare + "," + serverFirst + "," + finalBare;

						byte [ ] signature = Hmac ( storedKey, authMessage );
						byte [ ] proof = new byte [ clientKey.Length ];

						for ( int i = 0; i < proof.Length; i++ )
							proof [ i ] = ( byte ) ( clientKey [ i ] ^ signature [ i ] );

						byte [ ] answer = Encoding.UTF8.GetBytes ( finalBare + ",p=" + Convert.ToBase64String ( proof ) );

						Begin ( 'p' );
						outgoing.Write ( answer, 0, answer.Length );
						End ( );
						Flush ( );

						continue;

					}

					if ( code == 12 ) {

						string serverFinal = Encoding.UTF8.GetString ( body, 4, body.Length - 4 );
						string expected = Convert.ToBase64String ( Hmac ( Hmac ( salted, "Server Key" ), authMessage ) );

						if ( ScramFields ( serverFinal ) [ "v" ] != expected )
							throw new PgException ( "28000", "the server signature did not match" );

						continue;

					}

					throw new PgException ( "28000", "unsupported authentication request " + code );

				}

				if ( type == 'S' ) {

					int at = 0;
					string name = CString ( body, ref at );

					Settings [ name ] = CString ( body, ref at );

					continue;

				}

				if ( type == 'Z' )
					return;

			}

		}

		private static Dictionary < string, string > ScramFields ( string text ) {

			Dictionary < string, string > fields = new Dictionary < string, string > ( );

			foreach ( string part in text.Split ( ',' ) )
				if ( part.Length > 1 && part [ 1 ] == '=' )
					fields [ part.Substring ( 0, 1 ) ] = part.Substring ( 2 );

			return fields;

		}

		private static byte [ ] Hmac ( byte [ ] key, string text ) {

			using ( HMACSHA256 mac = new HMACSHA256 ( key ) )
				return mac.ComputeHash ( Encoding.UTF8.GetBytes ( text ) );

		}

		private static byte [ ] Pbkdf2 ( byte [ ] password, byte [ ] salt, int iterations ) {

			using ( HMACSHA256 mac = new HMACSHA256 ( password ) ) {

				byte [ ] block = new byte [ salt.Length + 4 ];

				Buffer.BlockCopy ( salt, 0, block, 0, salt.Length );
				block [ block.Length - 1 ] = 1;

				byte [ ] u = mac.ComputeHash ( block );
				byte [ ] result = ( byte [ ] ) u.Clone ( );

				for ( int i = 1; i < iterations; i++ ) {

					u = mac.ComputeHash ( u );

					for ( int k = 0; k < result.Length; k++ )
						result [ k ] ^= u [ k ];

				}

				return result;

			}

		}

		public List < PgResult > Query ( string sql ) {

			Begin ( 'Q' );
			WriteString ( sql );
			End ( );
			Flush ( );

			List < PgResult > results = new List < PgResult > ( );
			PgResult current = new PgResult ( );
			PgException failure = null;

			for ( ;; ) {

				char type;
				byte [ ] body = ReadMessage ( out type );

				switch ( type ) {

					case 'T':
						current.Columns.AddRange ( Describe ( body ) );
						break;

					case 'D':
						current.Rows.Add ( ReadRow ( body, current.Columns ) );
						break;

					case 'C':
						current.Tag = CString ( body );
						results.Add ( current );
						current = new PgResult ( );
						break;

					case 'I':
						results.Add ( current );
						current = new PgResult ( );
						break;

					case 'E':
						failure = Error ( body );
						break;

					case 'Z':
						InTransaction = body [ 0 ] != 'I';

						if ( failure != null )
							throw failure;

						return results;

				}

			}

		}

		public PgStatement Prepare ( string sql, params uint [ ] types ) {

			PgStatement statement = new PgStatement { Name = "_s" + ( ++named ).ToString ( CultureInfo.InvariantCulture ) };

			Begin ( 'P' );
			WriteString ( statement.Name );
			WriteString ( sql );
			WriteInt16 ( ( short ) types.Length );

			foreach ( uint type in types )
				WriteInt32 ( ( int ) type );

			End ( );
			Begin ( 'D' );
			outgoing.WriteByte ( ( byte ) 'S' );
			WriteString ( statement.Name );
			End ( );
			Begin ( 'S' );
			End ( );
			Flush ( );

			PgException failure = null;

			for ( ;; ) {

				char type;
				byte [ ] body = ReadMessage ( out type );

				if ( type == 't' ) {

					int count = Int16At ( body, 0 );

					statement.Parameters = new uint [ count ];

					for ( int i = 0; i < count; i++ )
						statement.Parameters [ i ] = ( uint ) Int32At ( body, 2 + i * 4 );

				} else if ( type == 'T' ) {

					statement.Columns = Describe ( body );

					foreach ( PgColumn column in statement.Columns )
						column.Format = 1;

				} else if ( type == 'E' )
					failure = Error ( body );
				else if ( type == 'Z' ) {

					InTransaction = body [ 0 ] != 'I';

					if ( failure != null )
						throw failure;

					return statement;

				}

			}

		}

		public PgResult Execute ( PgStatement statement, params object [ ] values ) {

			Begin ( 'B' );
			WriteString ( "" );
			WriteString ( statement.Name );
			WriteInt16 ( 1 );
			WriteInt16 ( 1 );
			WriteInt16 ( ( short ) values.Length );

			for ( int i = 0; i < values.Length; i++ ) {

				byte [ ] data = Encode ( values [ i ], statement.Parameters [ i ] );

				if ( data == null )
					WriteInt32 ( -1 );
				else {

					WriteInt32 ( data.Length );
					outgoing.Write ( data, 0, data.Length );

				}

			}

			WriteInt16 ( 1 );
			WriteInt16 ( 1 );
			End ( );
			Begin ( 'E' );
			WriteString ( "" );
			WriteInt32 ( 0 );
			End ( );
			Begin ( 'S' );
			End ( );
			Flush ( );

			PgResult result = new PgResult ( );
			PgException failure = null;

			result.Columns.AddRange ( statement.Columns );

			for ( ;; ) {

				char type;
				byte [ ] body = ReadMessage ( out type );

				if ( type == 'D' )
					result.Rows.Add ( ReadRow ( body, result.Columns ) );
				else if ( type == 'C' )
					result.Tag = CString ( body );
				else if ( type == 'E' )
					failure = Error ( body );
				else if ( type == 'Z' ) {

					InTransaction = body [ 0 ] != 'I';

					if ( failure != null )
						throw failure;

					return result;

				}

			}

		}

		private static List < PgColumn > Describe ( byte [ ] body ) {

			List < PgColumn > columns = new List < PgColumn > ( );
			int count = Int16At ( body, 0 );
			int at = 2;

			for ( int i = 0; i < count; i++ ) {

				PgColumn column = new PgColumn ( );

				column.Name = CString ( body, ref at );
				at += 6;
				column.Type = ( uint ) Int32At ( body, at );
				at += 10;
				column.Format = ( short ) Int16At ( body, at );
				at += 2;
				columns.Add ( column );

			}

			return columns;

		}

		private static object [ ] ReadRow ( byte [ ] body, List < PgColumn > columns ) {

			int count = Int16At ( body, 0 );
			int at = 2;
			object [ ] row = new object [ count ];

			for ( int i = 0; i < count; i++ ) {

				int size = Int32At ( body, at );

				at += 4;

				if ( size < 0 )
					continue;

				PgColumn column = i < columns.Count ? columns [ i ] : new PgColumn ( );

				row [ i ] = column.Format == 1 ? DecodeBinary ( body, at, size, column.Type ) : Encoding.UTF8.GetString ( body, at, size );
				at += size;

			}

			return row;

		}

		private static object DecodeBinary ( byte [ ] data, int at, int size, uint type ) {

			switch ( type ) {

				case Bool: return data [ at ] != 0;
				case Int2: return ( short ) Int16At ( data, at );
				case Int4: return Int32At ( data, at );
				case Oid: return ( uint ) Int32At ( data, at );
				case Int8: return Int64At ( data, at );
				case Float4: return BitConverter.ToSingle ( BitConverter.GetBytes ( Int32At ( data, at ) ), 0 );
				case Float8: return BitConverter.Int64BitsToDouble ( Int64At ( data, at ) );
				case Timestamp:
				case TimestampTz: return Epoch.AddTicks ( Int64At ( data, at ) * 10 );
				case Date: return Epoch.AddDays ( Int32At ( data, at ) );
				case Numeric: return DecodeNumeric ( data, at );
				case Uuid: return new Guid ( new [ ] { data [ at + 3 ], data [ at + 2 ], data [ at + 1 ], data [ at ], data [ at + 5 ], data [ at + 4 ], data [ at + 7 ], data [ at + 6 ], data [ at + 8 ], data [ at + 9 ], data [ at + 10 ], data [ at + 11 ], data [ at + 12 ], data [ at + 13 ], data [ at + 14 ], data [ at + 15 ] } );
				case Bytea: {

					byte [ ] copy = new byte [ size ];

					Buffer.BlockCopy ( data, at, copy, 0, size );

					return copy;

				}
				default: return Encoding.UTF8.GetString ( data, at, size );

			}

		}

		private static decimal DecodeNumeric ( byte [ ] data, int at ) {

			int digits = Int16At ( data, at );
			int weight = ( short ) Int16At ( data, at + 2 );
			int sign = Int16At ( data, at + 4 ) & 0xFFFF;
			int scale = Int16At ( data, at + 6 );
			decimal value = 0;

			for ( int i = 0; i < digits; i++ ) {

				decimal digit = Int16At ( data, at + 8 + i * 2 );
				int power = weight - i;
				decimal factor = 1;

				for ( int p = 0; p < Math.Abs ( power ); p++ )
					factor *= 10000;

				value += power >= 0 ? digit * factor : digit / factor;

			}

			value = Math.Round ( value, scale );

			return sign == 0x4000 ? -value : value;

		}

		private static byte [ ] Encode ( object value, uint type ) {

			if ( value == null || value is DBNull )
				return null;

			switch ( type ) {

				case Int8: return BigEndian ( Convert.ToInt64 ( value, CultureInfo.InvariantCulture ), 8 );
				case Int4: return BigEndian ( Convert.ToInt32 ( value, CultureInfo.InvariantCulture ), 4 );
				case Int2: return BigEndian ( Convert.ToInt16 ( value, CultureInfo.InvariantCulture ), 2 );
				case Bool: return new [ ] { ( byte ) ( ( bool ) value ? 1 : 0 ) };
				case Float8: return BigEndian ( BitConverter.DoubleToInt64Bits ( Convert.ToDouble ( value, CultureInfo.InvariantCulture ) ), 8 );
				case Timestamp:
				case TimestampTz: return BigEndian ( ( ( ( DateTime ) value ).ToUniversalTime ( ) - Epoch ).Ticks / 10, 8 );
				case Numeric: return EncodeNumeric ( Convert.ToDecimal ( value, CultureInfo.InvariantCulture ) );
				default: return Encoding.UTF8.GetBytes ( Convert.ToString ( value, CultureInfo.InvariantCulture ) );

			}

		}

		private static byte [ ] EncodeNumeric ( decimal value ) {

			bool negative = value < 0;
			string text = Math.Abs ( value ).ToString ( CultureInfo.InvariantCulture );
			int dot = text.IndexOf ( '.' );
			string whole = dot < 0 ? text : text.Substring ( 0, dot );
			string fraction = dot < 0 ? "" : text.Substring ( dot + 1 );
			int scale = fraction.Length;

			whole = whole.TrimStart ( '0' );
			whole = new string ( '0', ( 4 - whole.Length % 4 ) % 4 ) + whole;
			fraction = fraction + new string ( '0', ( 4 - fraction.Length % 4 ) % 4 );

			List < short > groups = new List < short > ( );

			for ( int i = 0; i < whole.Length; i += 4 )
				groups.Add ( short.Parse ( whole.Substring ( i, 4 ), CultureInfo.InvariantCulture ) );

			int weight = groups.Count - 1;

			for ( int i = 0; i < fraction.Length; i += 4 )
				groups.Add ( short.Parse ( fraction.Substring ( i, 4 ), CultureInfo.InvariantCulture ) );

			while ( groups.Count > 0 && groups [ 0 ] == 0 ) {

				groups.RemoveAt ( 0 );
				weight--;

			}

			while ( groups.Count > 0 && groups [ groups.Count - 1 ] == 0 )
				groups.RemoveAt ( groups.Count - 1 );

			if ( groups.Count == 0 )
				weight = 0;

			MemoryStream data = new MemoryStream ( );

			data.Write ( BigEndian ( groups.Count, 2 ), 0, 2 );
			data.Write ( BigEndian ( weight, 2 ), 0, 2 );
			data.Write ( BigEndian ( negative ? 0x4000 : 0, 2 ), 0, 2 );
			data.Write ( BigEndian ( scale, 2 ), 0, 2 );

			foreach ( short group in groups )
				data.Write ( BigEndian ( group, 2 ), 0, 2 );

			return data.ToArray ( );

		}

		private static byte [ ] BigEndian ( long value, int size ) {

			byte [ ] data = new byte [ size ];

			for ( int i = size - 1; i >= 0; i-- ) {

				data [ i ] = ( byte ) value;
				value >>= 8;

			}

			return data;

		}

		private static PgException Error ( byte [ ] body ) {

			string code = "";
			string message = "";
			int at = 0;

			while ( at < body.Length && body [ at ] != 0 ) {

				char field = ( char ) body [ at++ ];
				string value = CString ( body, ref at );

				if ( field == 'C' )
					code = value;
				else if ( field == 'M' )
					message = value;

			}

			return new PgException ( code, message );

		}

		private byte [ ] ReadMessage ( out char type ) {

			for ( ;; ) {

				type = ( char ) ReadByte ( );

				int size = ReadInt32 ( ) - 4;
				byte [ ] body = new byte [ size ];
				int got = 0;

				while ( got < size ) {

					int read = reader.Read ( body, got, size - got );

					if ( read <= 0 )
						throw new IOException ( "the server closed the connection" );

					got += read;

				}

				if ( type == 'N' )
					continue;

				return body;

			}

		}

		private int ReadByte ( ) {

			int value = reader.ReadByte ( );

			if ( value < 0 )
				throw new IOException ( "the server closed the connection" );

			return value;

		}

		private int ReadInt32 ( ) {

			return ReadByte ( ) << 24 | ReadByte ( ) << 16 | ReadByte ( ) << 8 | ReadByte ( );

		}

		private static int Int16At ( byte [ ] data, int at ) {

			return ( short ) ( data [ at ] << 8 | data [ at + 1 ] );

		}

		private static int Int32At ( byte [ ] data, int at ) {

			return data [ at ] << 24 | data [ at + 1 ] << 16 | data [ at + 2 ] << 8 | data [ at + 3 ];

		}

		private static long Int64At ( byte [ ] data, int at ) {

			return ( long ) ( uint ) Int32At ( data, at ) << 32 | ( uint ) Int32At ( data, at + 4 );

		}

		private static string CString ( byte [ ] data ) {

			int at = 0;

			return CString ( data, ref at );

		}

		private static string CString ( byte [ ] data, ref int at ) {

			int start = at;

			while ( at < data.Length && data [ at ] != 0 )
				at++;

			string value = Encoding.UTF8.GetString ( data, start, at - start );

			at++;

			return value;

		}

		private long messageStart;

		private void Begin ( char type ) {

			outgoing.WriteByte ( ( byte ) type );
			messageStart = outgoing.Position;
			WriteInt32 ( 0 );

		}

		private void End ( ) {

			long end = outgoing.Position;
			int size = ( int ) ( end - messageStart );

			outgoing.Position = messageStart;
			WriteInt32 ( size );
			outgoing.Position = end;

		}

		private void Flush ( ) {

			stream.Write ( outgoing.GetBuffer ( ), 0, ( int ) outgoing.Length );
			outgoing.SetLength ( 0 );

		}

		private void WriteInt16 ( short value ) {

			outgoing.WriteByte ( ( byte ) ( value >> 8 ) );
			outgoing.WriteByte ( ( byte ) value );

		}

		private void WriteInt32 ( int value ) {

			WriteRawInt32 ( outgoing, value );

		}

		private void WriteString ( string value ) {

			WriteRawString ( outgoing, value );

		}

		private static void WriteRawInt32 ( Stream target, int value ) {

			target.WriteByte ( ( byte ) ( value >> 24 ) );
			target.WriteByte ( ( byte ) ( value >> 16 ) );
			target.WriteByte ( ( byte ) ( value >> 8 ) );
			target.WriteByte ( ( byte ) value );

		}

		private static void WriteRawString ( Stream target, string value ) {

			byte [ ] data = Encoding.UTF8.GetBytes ( value );

			target.Write ( data, 0, data.Length );
			target.WriteByte ( 0 );

		}

	}

}
