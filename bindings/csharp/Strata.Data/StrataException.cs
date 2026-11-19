using System;
using System.Data.Common;

namespace Strata.Data {

	[Serializable]
	public sealed class StrataException : DbException {

		private readonly int code;

		public StrataException ( int code, string message ) : base ( message ) {

			this.code = code;

		}

		public int Code {

			get { return code; }

		}

		public override int ErrorCode {

			get { return code; }

		}

	}

}
