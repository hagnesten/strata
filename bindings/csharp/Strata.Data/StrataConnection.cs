using System;
using System.Data;
using System.Data.Common;

namespace Strata.Data {

	public sealed unsafe class StrataConnection : DbConnection {

		private IntPtr handle;
		private string connection_string = "";
		private string path = "";
		private long epoch;

		public StrataConnection ( ) {

		}

		public StrataConnection ( string connectionString ) {

			ConnectionString = connectionString;

		}

		public static string Version {

			get { return Native.Text ( Native.strata_version ( ) ); }

		}

		public override string ConnectionString {

			get { return connection_string; }

			set {

				if ( handle != IntPtr.Zero )
					throw new InvalidOperationException ( "close the connection before changing its connection string" );

				connection_string = value ?? "";
				path = ParsePath ( connection_string );

			}

		}

		public override string Database {

			get { return "main"; }

		}

		public override string DataSource {

			get { return path; }

		}

		public override string ServerVersion {

			get { return Version; }

		}

		public override ConnectionState State {

			get { return handle != IntPtr.Zero ? ConnectionState.Open : ConnectionState.Closed; }

		}

		public long LastInsertRowId {

			get { return Native.strata_last_rowid ( Handle ); }

		}

		public long Changes {

			get { return Native.strata_changes ( Handle ); }

		}

		public bool InTransaction {

			get { return Native.strata_in_transaction ( Handle ) != 0; }

		}

		internal long Epoch {

			get { return epoch; }

		}

		internal IntPtr Handle {

			get {

				if ( handle == IntPtr.Zero )
					throw new InvalidOperationException ( "the connection is not open" );

				return handle;

			}

		}

		private static string ParsePath ( string text ) {

			string trimmed = text.Trim ( );

			if ( trimmed.IndexOf ( '=' ) < 0 )
				return trimmed;

			foreach ( string part in trimmed.Split ( ';' ) ) {

				int equals = part.IndexOf ( '=' );

				if ( equals < 0 )
					continue;

				string key = part.Substring ( 0, equals ).Trim ( ).ToLowerInvariant ( );

				if ( key == "data source" || key == "datasource" || key == "filename" || key == "path" )
					return part.Substring ( equals + 1 ).Trim ( );

			}

			return "";

		}

		public override void Open ( ) {

			if ( handle != IntPtr.Zero )
				return;

			if ( path.Length == 0 )
				throw new InvalidOperationException ( "the connection string has no Data Source" );

			byte [ ] bytes = Native.Bytes ( path );
			IntPtr made;
			int code;

			fixed ( byte* at = bytes )
				code = Native.strata_open ( at, out made );

			if ( code != Native.Ok ) {

				string message = made != IntPtr.Zero ? Native.Text ( Native.strata_error ( made ) ) : "the database could not be opened";

				Native.strata_close ( made );

				throw new StrataException ( code, message );

			}

			handle = made;
			epoch++;
			OnStateChange ( new StateChangeEventArgs ( ConnectionState.Closed, ConnectionState.Open ) );

		}

		public override void Close ( ) {

			if ( handle == IntPtr.Zero )
				return;

			Native.strata_close ( handle );
			handle = IntPtr.Zero;
			epoch++;
			OnStateChange ( new StateChangeEventArgs ( ConnectionState.Open, ConnectionState.Closed ) );

		}

		public override void ChangeDatabase ( string databaseName ) {

			throw new NotSupportedException ( "Strata keeps one database per connection" );

		}

		public int Execute ( string sql ) {

			byte [ ] bytes = Native.Bytes ( sql );
			int code;

			fixed ( byte* at = bytes )
				code = Native.strata_exec ( Handle, at );

			if ( code != Native.Ok )
				throw Failure ( code );

			return ( int ) Native.strata_changes ( handle );

		}

		public new StrataTransaction BeginTransaction ( ) {

			return ( StrataTransaction ) BeginDbTransaction ( IsolationLevel.Serializable );

		}

		protected override DbTransaction BeginDbTransaction ( IsolationLevel isolationLevel ) {

			Execute ( "BEGIN" );

			return new StrataTransaction ( this, isolationLevel );

		}

		public new StrataCommand CreateCommand ( ) {

			return new StrataCommand ( "", this );

		}

		protected override DbCommand CreateDbCommand ( ) {

			return CreateCommand ( );

		}

		public StrataBulkInserter CreateBulkInserter ( string sql, int columns ) {

			return new StrataBulkInserter ( this, sql, columns, 4096 );

		}

		public StrataBulkInserter CreateBulkInserter ( string sql, int columns, int rowsPerCall ) {

			return new StrataBulkInserter ( this, sql, columns, rowsPerCall );

		}

		internal StrataException Failure ( int code ) {

			return new StrataException ( code, handle != IntPtr.Zero ? Native.Text ( Native.strata_error ( handle ) ) : "the connection is not open" );

		}

		protected override void Dispose ( bool disposing ) {

			Close ( );
			base.Dispose ( disposing );

		}

	}

}
