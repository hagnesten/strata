using System;
using System.Runtime.InteropServices;
using System.Security;

namespace Strata.Data.Tests {

	[SuppressUnmanagedCodeSecurity]
	internal static unsafe class SqliteRaw {

		private const string Library = "sqlite3";

		public const int Row = 100;
		public const int Done = 101;

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int sqlite3_initialize ( );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int sqlite3_open_v2 ( byte* path, out IntPtr db, int flags, IntPtr vfs );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int sqlite3_close_v2 ( IntPtr db );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int sqlite3_exec ( IntPtr db, byte* sql, IntPtr callback, IntPtr context, IntPtr error );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int sqlite3_prepare_v2 ( IntPtr db, byte* sql, int size, out IntPtr stmt, IntPtr tail );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int sqlite3_bind_int64 ( IntPtr stmt, int index, long value );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int sqlite3_bind_double ( IntPtr stmt, int index, double value );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int sqlite3_bind_text16 ( IntPtr stmt, int index, char* value, int bytes, IntPtr destructor );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int sqlite3_step ( IntPtr stmt );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int sqlite3_reset ( IntPtr stmt );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int sqlite3_finalize ( IntPtr stmt );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern long sqlite3_column_int64 ( IntPtr stmt, int column );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern double sqlite3_column_double ( IntPtr stmt, int column );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern char* sqlite3_column_text16 ( IntPtr stmt, int column );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int sqlite3_column_bytes16 ( IntPtr stmt, int column );

		public static byte [ ] Bytes ( string text ) {

			int size = System.Text.Encoding.UTF8.GetByteCount ( text );
			byte [ ] bytes = new byte [ size + 1 ];

			System.Text.Encoding.UTF8.GetBytes ( text, 0, text.Length, bytes, 0 );

			return bytes;

		}

		public static IntPtr Open ( string path ) {

			byte [ ] bytes = Bytes ( path );
			IntPtr db;

			sqlite3_initialize ( );

			fixed ( byte* at = bytes )
				sqlite3_open_v2 ( at, out db, 0x2 | 0x4 | 0x8000, IntPtr.Zero );

			Exec ( db, "PRAGMA page_size = 4096; PRAGMA journal_mode = WAL; PRAGMA synchronous = FULL; PRAGMA cache_size = 2048; PRAGMA temp_store = MEMORY" );

			return db;

		}

		public static void Exec ( IntPtr db, string sql ) {

			byte [ ] bytes = Bytes ( sql );

			fixed ( byte* at = bytes )
				sqlite3_exec ( db, at, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero );

		}

		public static IntPtr Prepare ( IntPtr db, string sql ) {

			byte [ ] bytes = Bytes ( sql );
			IntPtr stmt;

			fixed ( byte* at = bytes )
				sqlite3_prepare_v2 ( db, at, -1, out stmt, IntPtr.Zero );

			return stmt;

		}

		public static string Text ( IntPtr stmt, int column ) {

			char* at = sqlite3_column_text16 ( stmt, column );

			return at == null ? null : new string ( at, 0, sqlite3_column_bytes16 ( stmt, column ) / 2 );

		}

	}

}
