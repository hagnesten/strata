using System;
using System.Data;
using System.Data.Common;

namespace Strata.Data {

	public sealed class StrataTransaction : DbTransaction {

		private StrataConnection connection;
		private readonly IsolationLevel level;

		internal StrataTransaction ( StrataConnection connection, IsolationLevel level ) {

			this.connection = connection;
			this.level = level;

		}

		public new StrataConnection Connection {

			get { return connection; }

		}

		protected override DbConnection DbConnection {

			get { return connection; }

		}

		public override IsolationLevel IsolationLevel {

			get { return level; }

		}

		public override void Commit ( ) {

			if ( connection == null )
				throw new InvalidOperationException ( "the transaction has already finished" );

			connection.Execute ( "COMMIT" );
			connection = null;

		}

		public override void Rollback ( ) {

			if ( connection == null )
				throw new InvalidOperationException ( "the transaction has already finished" );

			connection.Execute ( "ROLLBACK" );
			connection = null;

		}

		protected override void Dispose ( bool disposing ) {

			if ( disposing && connection != null && connection.State == ConnectionState.Open && connection.InTransaction )
				Rollback ( );

			connection = null;
			base.Dispose ( disposing );

		}

	}

}
