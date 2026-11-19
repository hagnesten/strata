using System;
using System.Collections;
using System.Collections.Generic;
using System.Data;
using System.Data.Common;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;

namespace Strata.Data {

	public sealed unsafe class StrataDataReader : DbDataReader {

		private const int BlockValues = 4096;

		private StrataCommand command;
		private IntPtr statement;
		private readonly CommandBehavior behavior;
		private readonly int width;
		private readonly string [ ] names;
		private StrataValue* block;
		private readonly int room;
		private int rows;
		private int at = -1;
		private bool done;
		private bool closed;
		private bool first_block;
		private Dictionary < string, int > ordinals;

		internal StrataDataReader ( StrataCommand command, IntPtr statement, CommandBehavior behavior ) {

			this.command = command;
			this.statement = statement;
			this.behavior = behavior;
			width = Native.strata_column_count ( statement );
			names = new string [ width ];

			for ( int c = 0; c < width; c++ )
				names [ c ] = Native.Text ( Native.strata_column_name ( statement, c ) ) ?? ( "column" + c.ToString ( CultureInfo.InvariantCulture ) );

			room = width > 0 ? Math.Max ( 16, BlockValues / width ) : 1;
			block = ( StrataValue* ) Marshal.AllocHGlobal ( sizeof ( StrataValue ) * Math.Max ( width, 1 ) * room );

			try {

				Fill ( );

			} catch {

				Release ( );

				throw;

			}

			first_block = true;

		}

		private void Fill ( ) {

			int got;
			int code = Native.strata_fetch16 ( statement, block, room, out got );

			rows = got;
			at = -1;

			if ( code == Native.Done )
				done = true;
			else if ( code != Native.Row )
				throw command.Owner.Failure ( code );

		}

		private StrataValue* Cell ( int ordinal ) {

			if ( at < 0 || at >= rows )
				throw new InvalidOperationException ( "call Read before reading values" );

			if ( ordinal < 0 || ordinal >= width )
				throw new IndexOutOfRangeException ( "there is no column " + ordinal.ToString ( CultureInfo.InvariantCulture ) );

			return block + at * width + ordinal;

		}

		internal static object ValueOf ( StrataValue* value ) {

			switch ( value->Type ) {

				case Native.TypeInteger:
					return value->Integer;

				case Native.TypeReal:
					return value->Real;

				case Native.TypeText:
					return Encoding.UTF8.GetString ( ( byte* ) value->Bytes, ( int ) value->Size );

				case Native.TypeText16:
					return new string ( ( char* ) value->Bytes, 0, ( int ) ( value->Size / 2 ) );

				case Native.TypeBlob: {

					byte [ ] bytes = new byte [ value->Size ];

					if ( value->Size > 0 )
						Marshal.Copy ( value->Bytes, bytes, 0, ( int ) value->Size );

					return bytes;

				}

				default:
					return DBNull.Value;

			}

		}

		public override bool Read ( ) {

			if ( closed )
				return false;

			if ( first_block ) {

				first_block = false;

				if ( rows > 0 ) {

					at = 0;

					return true;

				}

				return false;

			}

			if ( at + 1 < rows ) {

				at++;

				return true;

			}

			if ( done ) {

				at = rows;

				return false;

			}

			Fill ( );

			if ( rows == 0 )
				return false;

			at = 0;

			return true;

		}

		public override int FieldCount {

			get { return width; }

		}

		public override int VisibleFieldCount {

			get { return width; }

		}

		public override bool HasRows {

			get { return rows > 0 || !done; }

		}

		public override bool IsClosed {

			get { return closed; }

		}

		public override int RecordsAffected {

			get { return -1; }

		}

		public override int Depth {

			get { return 0; }

		}

		public override object this [ int ordinal ] {

			get { return GetValue ( ordinal ); }

		}

		public override object this [ string name ] {

			get { return GetValue ( GetOrdinal ( name ) ); }

		}

		public override string GetName ( int ordinal ) {

			return names [ ordinal ];

		}

		public override int GetOrdinal ( string name ) {

			if ( ordinals == null ) {

				ordinals = new Dictionary < string, int > ( StringComparer.OrdinalIgnoreCase );

				for ( int c = 0; c < width; c++ )
					if ( !ordinals.ContainsKey ( names [ c ] ) )
						ordinals [ names [ c ] ] = c;

			}

			int found;

			if ( !ordinals.TryGetValue ( name, out found ) )
				throw new IndexOutOfRangeException ( "there is no column named " + name );

			return found;

		}

		public override string GetDataTypeName ( int ordinal ) {

			Type type = GetFieldType ( ordinal );

			return type == typeof ( long ) ? "INTEGER" : type == typeof ( double ) ? "REAL" : type == typeof ( byte [ ] ) ? "BLOB" : "TEXT";

		}

		public override Type GetFieldType ( int ordinal ) {

			if ( ordinal < 0 || ordinal >= width )
				throw new IndexOutOfRangeException ( "there is no column " + ordinal.ToString ( CultureInfo.InvariantCulture ) );

			int row = at >= 0 && at < rows ? at : rows > 0 ? 0 : -1;

			if ( row < 0 )
				return typeof ( string );

			switch ( block [ row * width + ordinal ].Type ) {

				case Native.TypeInteger:
					return typeof ( long );

				case Native.TypeReal:
					return typeof ( double );

				case Native.TypeBlob:
					return typeof ( byte [ ] );

				default:
					return typeof ( string );

			}

		}

		public override bool IsDBNull ( int ordinal ) {

			return Cell ( ordinal )->Type == Native.TypeNull;

		}

		public override object GetValue ( int ordinal ) {

			return ValueOf ( Cell ( ordinal ) );

		}

		public override int GetValues ( object [ ] values ) {

			int count = Math.Min ( values.Length, width );

			for ( int c = 0; c < count; c++ )
				values [ c ] = ValueOf ( Cell ( c ) );

			return count;

		}

		private static InvalidCastException NullCast ( ) {

			return new InvalidCastException ( "the value is NULL" );

		}

		public override long GetInt64 ( int ordinal ) {

			StrataValue* value = Cell ( ordinal );

			switch ( value->Type ) {

				case Native.TypeInteger:
					return value->Integer;

				case Native.TypeReal:
					return ( long ) value->Real;

				case Native.TypeText:
					return long.Parse ( GetString ( ordinal ), NumberStyles.Any, CultureInfo.InvariantCulture );

				case Native.TypeNull:
					throw NullCast ( );

				default:
					throw new InvalidCastException ( "a blob is not a number" );

			}

		}

		public override double GetDouble ( int ordinal ) {

			StrataValue* value = Cell ( ordinal );

			switch ( value->Type ) {

				case Native.TypeInteger:
					return value->Integer;

				case Native.TypeReal:
					return value->Real;

				case Native.TypeText:
					return double.Parse ( GetString ( ordinal ), NumberStyles.Any, CultureInfo.InvariantCulture );

				case Native.TypeNull:
					throw NullCast ( );

				default:
					throw new InvalidCastException ( "a blob is not a number" );

			}

		}

		public override string GetString ( int ordinal ) {

			StrataValue* value = Cell ( ordinal );

			switch ( value->Type ) {

				case Native.TypeText16:
					return new string ( ( char* ) value->Bytes, 0, ( int ) ( value->Size / 2 ) );

				case Native.TypeText:
				case Native.TypeBlob:
					return Encoding.UTF8.GetString ( ( byte* ) value->Bytes, ( int ) value->Size );

				case Native.TypeInteger:
					return value->Integer.ToString ( CultureInfo.InvariantCulture );

				case Native.TypeReal:
					return value->Real.ToString ( "R", CultureInfo.InvariantCulture );

				default:
					throw NullCast ( );

			}

		}

		public override int GetInt32 ( int ordinal ) {

			return checked ( ( int ) GetInt64 ( ordinal ) );

		}

		public override short GetInt16 ( int ordinal ) {

			return checked ( ( short ) GetInt64 ( ordinal ) );

		}

		public override byte GetByte ( int ordinal ) {

			return checked ( ( byte ) GetInt64 ( ordinal ) );

		}

		public override bool GetBoolean ( int ordinal ) {

			return GetInt64 ( ordinal ) != 0;

		}

		public override float GetFloat ( int ordinal ) {

			return ( float ) GetDouble ( ordinal );

		}

		public override decimal GetDecimal ( int ordinal ) {

			StrataValue* value = Cell ( ordinal );

			if ( value->Type == Native.TypeInteger )
				return value->Integer;

			if ( value->Type == Native.TypeReal )
				return ( decimal ) value->Real;

			return decimal.Parse ( GetString ( ordinal ), NumberStyles.Any, CultureInfo.InvariantCulture );

		}

		public override char GetChar ( int ordinal ) {

			string text = GetString ( ordinal );

			if ( text.Length == 0 )
				throw new InvalidCastException ( "the text is empty" );

			return text [ 0 ];

		}

		public override DateTime GetDateTime ( int ordinal ) {

			StrataValue* value = Cell ( ordinal );

			if ( value->Type == Native.TypeInteger )
				return new DateTime ( 1970, 1, 1, 0, 0, 0, DateTimeKind.Utc ).AddSeconds ( value->Integer );

			if ( value->Type == Native.TypeReal )
				return DateTime.FromOADate ( value->Real - 2415018.5 );

			return DateTime.Parse ( GetString ( ordinal ), CultureInfo.InvariantCulture, DateTimeStyles.AllowWhiteSpaces );

		}

		public override Guid GetGuid ( int ordinal ) {

			StrataValue* value = Cell ( ordinal );

			if ( value->Type == Native.TypeBlob && value->Size == 16 ) {

				byte [ ] bytes = new byte [ 16 ];

				Marshal.Copy ( value->Bytes, bytes, 0, 16 );

				return new Guid ( bytes );

			}

			return new Guid ( GetString ( ordinal ) );

		}

		public override long GetBytes ( int ordinal, long dataOffset, byte [ ] buffer, int bufferOffset, int length ) {

			StrataValue* value = Cell ( ordinal );

			if ( value->Type != Native.TypeBlob && value->Type != Native.TypeText && value->Type != Native.TypeText16 )
				throw new InvalidCastException ( "the value is not a blob" );

			if ( buffer == null )
				return value->Size;

			long count = Math.Max ( 0, Math.Min ( length, value->Size - dataOffset ) );

			if ( count > 0 )
				Marshal.Copy ( new IntPtr ( ( byte* ) value->Bytes + dataOffset ), buffer, bufferOffset, ( int ) count );

			return count;

		}

		public override long GetChars ( int ordinal, long dataOffset, char [ ] buffer, int bufferOffset, int length ) {

			string text = GetString ( ordinal );

			if ( buffer == null )
				return text.Length;

			int count = ( int ) Math.Max ( 0, Math.Min ( length, text.Length - dataOffset ) );

			text.CopyTo ( ( int ) dataOffset, buffer, bufferOffset, count );

			return count;

		}

		public override IEnumerator GetEnumerator ( ) {

			return new DbEnumerator ( this, ( behavior & CommandBehavior.CloseConnection ) != 0 );

		}

		public override DataTable GetSchemaTable ( ) {

			DataTable table = new DataTable ( "SchemaTable" );

			table.Columns.Add ( "ColumnName", typeof ( string ) );
			table.Columns.Add ( "ColumnOrdinal", typeof ( int ) );
			table.Columns.Add ( "ColumnSize", typeof ( int ) );
			table.Columns.Add ( "DataType", typeof ( Type ) );
			table.Columns.Add ( "AllowDBNull", typeof ( bool ) );

			for ( int c = 0; c < width; c++ )
				table.Rows.Add ( names [ c ], c, -1, GetFieldType ( c ), true );

			return table;

		}

		public override bool NextResult ( ) {

			return false;

		}

		private void Release ( ) {

			if ( block != null ) {

				Marshal.FreeHGlobal ( new IntPtr ( block ) );
				block = null;

			}

		}

		public override void Close ( ) {

			if ( closed )
				return;

			closed = true;

			if ( command != null && command.Connection != null && command.Connection.State == ConnectionState.Open )
				Native.strata_reset ( statement );

			Release ( );

			if ( ( behavior & CommandBehavior.CloseConnection ) != 0 && command != null && command.Connection != null )
				command.Connection.Close ( );

			command = null;
			statement = IntPtr.Zero;

		}

		protected override void Dispose ( bool disposing ) {

			Close ( );
			base.Dispose ( disposing );

		}

	}

}
