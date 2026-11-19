using System;
using System.Runtime.InteropServices;

namespace Strata.Data {

	public sealed unsafe class StrataBulkInserter : IDisposable {

		private StrataConnection connection;
		private IntPtr statement;
		private readonly int width;
		private readonly int room;
		private StrataValue* values;
		private byte* bytes;
		private long bytes_room;
		private long bytes_used;
		private int rows;
		private int column;
		private long total;

		internal StrataBulkInserter ( StrataConnection connection, string sql, int width, int rowsPerCall ) {

			if ( width <= 0 )
				throw new ArgumentOutOfRangeException ( "width" );

			this.connection = connection;
			this.width = width;
			room = Math.Max ( 1, rowsPerCall );

			byte [ ] text = Native.Bytes ( sql );
			byte* tail;
			int code;

			fixed ( byte* at = text )
				code = Native.strata_prepare ( connection.Handle, at, text.Length - 1, out statement, out tail );

			if ( code != Native.Ok || statement == IntPtr.Zero )
				throw connection.Failure ( code == Native.Ok ? Native.Misuse : code );

			values = ( StrataValue* ) Marshal.AllocHGlobal ( sizeof ( StrataValue ) * width * room );
			bytes_room = 1 << 16;
			bytes = ( byte* ) Marshal.AllocHGlobal ( new IntPtr ( bytes_room ) );

		}

		public long RowsWritten {

			get { return total; }

		}

		private StrataValue* Next ( ) {

			if ( statement == IntPtr.Zero )
				throw new ObjectDisposedException ( "StrataBulkInserter" );

			if ( column >= width )
				throw new InvalidOperationException ( "the row already has every column, call EndRow" );

			return values + rows * width + column++;

		}

		private long Keep ( void* data, long size ) {

			long aligned = ( size + 7 ) & ~7L;

			if ( bytes_used + aligned > bytes_room ) {

				long grown = Math.Max ( bytes_room * 2, bytes_used + aligned );

				bytes = ( byte* ) Marshal.ReAllocHGlobal ( new IntPtr ( bytes ), new IntPtr ( grown ) );
				bytes_room = grown;

			}

			long offset = bytes_used;

			if ( size > 0 )
				Buffer.MemoryCopy ( data, bytes + offset, bytes_room - offset, size );

			bytes_used += aligned;

			return offset;

		}

		public StrataBulkInserter Add ( long value ) {

			StrataValue* slot = Next ( );

			slot->Type = Native.TypeInteger;
			slot->Size = 0;
			slot->Integer = value;

			return this;

		}

		public StrataBulkInserter Add ( int value ) {

			return Add ( ( long ) value );

		}

		public StrataBulkInserter Add ( bool value ) {

			return Add ( value ? 1L : 0L );

		}

		public StrataBulkInserter Add ( double value ) {

			StrataValue* slot = Next ( );

			slot->Type = Native.TypeReal;
			slot->Size = 0;
			slot->Real = value;

			return this;

		}

		public StrataBulkInserter Add ( decimal value ) {

			return Add ( StrataCommand.Format ( value ) );

		}

		public StrataBulkInserter Add ( DateTime value ) {

			return Add ( StrataCommand.Format ( value ) );

		}

		public StrataBulkInserter Add ( string value ) {

			if ( value == null )
				return AddNull ( );

			StrataValue* slot = Next ( );
			long offset;

			fixed ( char* at = value )
				offset = Keep ( at, value.Length * 2L );

			slot->Type = Native.TypeText16;
			slot->Size = ( uint ) ( value.Length * 2 );
			slot->Integer = offset;

			return this;

		}

		public StrataBulkInserter Add ( byte [ ] value ) {

			if ( value == null )
				return AddNull ( );

			StrataValue* slot = Next ( );
			long offset;

			fixed ( byte* at = value )
				offset = Keep ( at, value.Length );

			slot->Type = Native.TypeBlob;
			slot->Size = ( uint ) value.Length;
			slot->Integer = offset;

			return this;

		}

		public StrataBulkInserter AddNull ( ) {

			StrataValue* slot = Next ( );

			slot->Type = Native.TypeNull;
			slot->Size = 0;
			slot->Integer = 0;

			return this;

		}

		public void EndRow ( ) {

			while ( column < width )
				AddNull ( );

			column = 0;
			rows++;

			if ( rows == room )
				Flush ( );

		}

		public void Flush ( ) {

			if ( column != 0 )
				throw new InvalidOperationException ( "finish the row with EndRow before flushing" );

			if ( rows == 0 )
				return;

			int count = rows * width;

			for ( int i = 0; i < count; i++ )
				if ( values [ i ].Type == Native.TypeText16 || values [ i ].Type == Native.TypeBlob )
					values [ i ].Bytes = new IntPtr ( bytes + values [ i ].Integer );

			int done;
			int code = Native.strata_run_batch ( statement, values, rows, width, out done );

			total += done;
			rows = 0;
			bytes_used = 0;

			if ( code != Native.Ok )
				throw connection.Failure ( code );

		}

		public void Dispose ( ) {

			try {

				if ( statement != IntPtr.Zero && connection.State == System.Data.ConnectionState.Open && column == 0 )
					Flush ( );

			} finally {

				if ( statement != IntPtr.Zero && connection.State == System.Data.ConnectionState.Open )
					Native.strata_finalize ( statement );

				statement = IntPtr.Zero;

				if ( values != null )
					Marshal.FreeHGlobal ( new IntPtr ( values ) );

				if ( bytes != null )
					Marshal.FreeHGlobal ( new IntPtr ( bytes ) );

				values = null;
				bytes = null;

			}

		}

	}

}
