using System;
using System.Runtime.InteropServices;
using System.Security;
using System.Text;

namespace Strata.Data {

	[SuppressUnmanagedCodeSecurity]
	internal static unsafe class Native {

		private const string Library = "strata";

		public const int Ok = 0;
		public const int Misuse = 21;
		public const int Range = 25;
		public const int Row = 100;
		public const int Done = 101;

		public const int TypeNull = 0;
		public const int TypeInteger = 1;
		public const int TypeReal = 2;
		public const int TypeText = 3;
		public const int TypeBlob = 4;
		public const int TypeText16 = 5;

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern IntPtr strata_version ( );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_open ( byte* path, out IntPtr db );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_close ( IntPtr db );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_exec ( IntPtr db, byte* sql );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_busy_timeout ( IntPtr db, int milliseconds );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern IntPtr strata_error ( IntPtr db );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern long strata_changes ( IntPtr db );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern long strata_last_rowid ( IntPtr db );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_in_transaction ( IntPtr db );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_prepare ( IntPtr db, byte* sql, int size, out IntPtr stmt, out byte* tail );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_finalize ( IntPtr stmt );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_reset ( IntPtr stmt );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_clear_bindings ( IntPtr stmt );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_parameter_count ( IntPtr stmt );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern IntPtr strata_parameter_name ( IntPtr stmt, int index );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_bind_null ( IntPtr stmt, int index );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_bind_int64 ( IntPtr stmt, int index, long value );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_bind_double ( IntPtr stmt, int index, double value );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_bind_text16 ( IntPtr stmt, int index, char* value, int units );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_bind_blob ( IntPtr stmt, int index, byte* value, int size );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_step ( IntPtr stmt );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_column_count ( IntPtr stmt );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern IntPtr strata_column_name ( IntPtr stmt, int column );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern StrataValue* strata_row ( IntPtr stmt );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_fetch ( IntPtr stmt, StrataValue* output, int room, out int rows );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_fetch16 ( IntPtr stmt, StrataValue* output, int room, out int rows );

		[DllImport ( Library, CallingConvention = CallingConvention.Cdecl )]
		public static extern int strata_run_batch ( IntPtr stmt, StrataValue* values, int rows, int width, out int done );

		public static byte [ ] Bytes ( string text ) {

			int size = Encoding.UTF8.GetByteCount ( text );
			byte [ ] bytes = new byte [ size + 1 ];

			Encoding.UTF8.GetBytes ( text, 0, text.Length, bytes, 0 );

			return bytes;

		}

		public static string Text ( IntPtr text ) {

			if ( text == IntPtr.Zero )
				return null;

			byte* at = ( byte* ) text;
			int size = 0;

			while ( at [ size ] != 0 )
				size++;

			return Encoding.UTF8.GetString ( at, size );

		}

	}

}
