using System;
using System.Runtime.InteropServices;

namespace Strata.Data {

	[StructLayout ( LayoutKind.Explicit, Size = 16 )]
	public struct StrataValue {

		[FieldOffset ( 0 )]
		public int Type;

		[FieldOffset ( 4 )]
		public uint Size;

		[FieldOffset ( 8 )]
		public long Integer;

		[FieldOffset ( 8 )]
		public double Real;

		[FieldOffset ( 8 )]
		public IntPtr Bytes;

	}

}
