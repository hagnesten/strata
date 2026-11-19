using System;
using System.Collections.Generic;
using System.Data;
using System.Data.Common;
using System.Globalization;
using System.Text;

namespace Strata.Data {

	public sealed unsafe class StrataCommand : DbCommand {

		private string text = "";
		private StrataConnection connection;
		private StrataTransaction transaction;
		private readonly StrataParameterCollection parameters = new StrataParameterCollection ( );
		private readonly List < IntPtr > statements = new List < IntPtr > ( );
		private readonly List < string [ ] > statement_names = new List < string [ ] > ( );
		private readonly List < int [ ] > statement_slots = new List < int [ ] > ( );
		private readonly List < string [ ] > slot_names = new List < string [ ] > ( );
		private int slots_version = -1;
		private string prepared_text;
		private long prepared_epoch = -1;
		private int timeout = 30;
		private bool visible = true;
		private UpdateRowSource row_source = UpdateRowSource.None;

		public StrataCommand ( ) {

		}

		public StrataCommand ( string commandText ) {

			text = commandText ?? "";

		}

		public StrataCommand ( string commandText, StrataConnection connection ) {

			text = commandText ?? "";
			this.connection = connection;

		}

		public override string CommandText {

			get { return text; }
			set { text = value ?? ""; }

		}

		public override int CommandTimeout {

			get { return timeout; }
			set { timeout = value; }

		}

		public override CommandType CommandType {

			get { return CommandType.Text; }

			set {

				if ( value != CommandType.Text )
					throw new NotSupportedException ( "Strata runs SQL text" );

			}

		}

		public override bool DesignTimeVisible {

			get { return visible; }
			set { visible = value; }

		}

		public override UpdateRowSource UpdatedRowSource {

			get { return row_source; }
			set { row_source = value; }

		}

		public new StrataConnection Connection {

			get { return connection; }

			set {

				if ( value != connection )
					Unprepare ( );

				connection = value;

			}

		}

		protected override DbConnection DbConnection {

			get { return connection; }
			set { Connection = ( StrataConnection ) value; }

		}

		public new StrataParameterCollection Parameters {

			get { return parameters; }

		}

		protected override DbParameterCollection DbParameterCollection {

			get { return parameters; }

		}

		public new StrataTransaction Transaction {

			get { return transaction; }
			set { transaction = value; }

		}

		protected override DbTransaction DbTransaction {

			get { return transaction; }
			set { transaction = ( StrataTransaction ) value; }

		}

		internal StrataConnection Owner {

			get {

				if ( connection == null )
					throw new InvalidOperationException ( "the command has no connection" );

				return connection;

			}

		}

		public override void Prepare ( ) {

			Ready ( );

		}

		public override void Cancel ( ) {

		}

		protected override DbParameter CreateDbParameter ( ) {

			return new StrataParameter ( );

		}

		public new StrataParameter CreateParameter ( ) {

			return new StrataParameter ( );

		}

		private void Ready ( ) {

			IntPtr db = Owner.Handle;

			if ( prepared_text == text && prepared_epoch == connection.Epoch )
				return;

			Unprepare ( );

			byte [ ] bytes = Native.Bytes ( text );

			fixed ( byte* start = bytes ) {

				byte* at = start;
				byte* end = start + bytes.Length - 1;

				while ( at < end ) {

					IntPtr statement;
					byte* tail;
					int code = Native.strata_prepare ( db, at, ( int ) ( end - at ), out statement, out tail );

					if ( code != Native.Ok ) {

						StrataException failure = connection.Failure ( code );

						Unprepare ( );

						throw failure;

					}

					if ( statement != IntPtr.Zero ) {

						int count = Native.strata_parameter_count ( statement );
						string [ ] names = new string [ count ];

						for ( int i = 0; i < count; i++ )
							names [ i ] = Native.Text ( Native.strata_parameter_name ( statement, i + 1 ) );

						statements.Add ( statement );
						statement_names.Add ( names );
						statement_slots.Add ( null );
						slot_names.Add ( null );

					}

					if ( tail <= at )
						break;

					at = tail;

				}

			}

			prepared_text = text;
			prepared_epoch = connection.Epoch;

		}

		private void Unprepare ( ) {

			if ( connection != null && prepared_epoch == connection.Epoch && connection.State == ConnectionState.Open )
				foreach ( IntPtr statement in statements )
					Native.strata_finalize ( statement );

			statements.Clear ( );
			statement_names.Clear ( );
			statement_slots.Clear ( );
			slot_names.Clear ( );
			prepared_text = null;
			prepared_epoch = -1;

		}

		private int [ ] Slots ( int which ) {

			int [ ] slots = statement_slots [ which ];
			string [ ] seen = slot_names [ which ];

			if ( slots != null && slots_version == parameters.Version ) {

				bool same = true;

				for ( int i = 0; i < slots.Length && same; i++ )
					same = ( object ) parameters.At ( slots [ i ] ).ParameterName == ( object ) seen [ i ];

				if ( same )
					return slots;

			}

			string [ ] names = statement_names [ which ];

			slots = new int [ names.Length ];
			seen = new string [ names.Length ];

			for ( int i = 0; i < names.Length; i++ ) {

				int found = names [ i ] != null ? parameters.IndexOf ( names [ i ] ) : i < parameters.Count ? i : -1;

				if ( found < 0 )
					throw new InvalidOperationException ( "no value was given for parameter " + ( names [ i ] ?? ( i + 1 ).ToString ( CultureInfo.InvariantCulture ) ) );

				slots [ i ] = found;
				seen [ i ] = parameters.At ( found ).ParameterName;

			}

			if ( slots_version != parameters.Version ) {

				for ( int s = 0; s < statement_slots.Count; s++ ) {

					statement_slots [ s ] = null;
					slot_names [ s ] = null;

				}

			}

			statement_slots [ which ] = slots;
			slot_names [ which ] = seen;
			slots_version = parameters.Version;

			return slots;

		}

		private void Bind ( int which ) {

			IntPtr statement = statements [ which ];
			int [ ] slots = Slots ( which );

			for ( int i = 0; i < slots.Length; i++ )
				Bind ( statement, i + 1, parameters.At ( slots [ i ] ).Value );

		}

		internal void Bind ( IntPtr statement, int index, object value ) {

			int code;

			if ( value == null || value is DBNull )
				code = Native.strata_bind_null ( statement, index );
			else if ( value is string ) {

				string given = ( string ) value;

				fixed ( char* at = given )
					code = Native.strata_bind_text16 ( statement, index, at, given.Length );

			} else if ( value is long || value is int || value is short || value is byte || value is sbyte || value is ushort || value is uint )
				code = Native.strata_bind_int64 ( statement, index, Convert.ToInt64 ( value, CultureInfo.InvariantCulture ) );
			else if ( value is ulong )
				code = Native.strata_bind_int64 ( statement, index, checked ( ( long ) ( ulong ) value ) );
			else if ( value is bool )
				code = Native.strata_bind_int64 ( statement, index, ( bool ) value ? 1 : 0 );
			else if ( value is double || value is float )
				code = Native.strata_bind_double ( statement, index, Convert.ToDouble ( value, CultureInfo.InvariantCulture ) );
			else if ( value is Enum )
				code = Native.strata_bind_int64 ( statement, index, Convert.ToInt64 ( value, CultureInfo.InvariantCulture ) );
			else if ( value is byte [ ] ) {

				byte [ ] given = ( byte [ ] ) value;

				fixed ( byte* at = given )
					code = Native.strata_bind_blob ( statement, index, at, given.Length );

			} else
				code = BindText ( statement, index, Format ( value ) );

			if ( code != Native.Ok )
				throw Owner.Failure ( code );

		}

		private static int BindText ( IntPtr statement, int index, string value ) {

			fixed ( char* at = value )
				return Native.strata_bind_text16 ( statement, index, at, value.Length );

		}

		internal static string Format ( object value ) {

			if ( value is decimal )
				return ( ( decimal ) value ).ToString ( CultureInfo.InvariantCulture );

			if ( value is DateTime )
				return ( ( DateTime ) value ).ToString ( "yyyy-MM-dd HH:mm:ss.FFFFFFF", CultureInfo.InvariantCulture );

			if ( value is DateTimeOffset )
				return ( ( DateTimeOffset ) value ).ToString ( "yyyy-MM-dd HH:mm:ss.FFFFFFFzzz", CultureInfo.InvariantCulture );

			if ( value is TimeSpan )
				return ( ( TimeSpan ) value ).ToString ( "c", CultureInfo.InvariantCulture );

			if ( value is Guid )
				return ( ( Guid ) value ).ToString ( "D" );

			return Convert.ToString ( value, CultureInfo.InvariantCulture );

		}

		private void Finish ( IntPtr statement, int code ) {

			if ( code == Native.Done ) {

				Native.strata_reset ( statement );

				return;

			}

			StrataException failure = Owner.Failure ( code );

			Native.strata_reset ( statement );

			throw failure;

		}

		private int RunQuietly ( int which ) {

			IntPtr statement = statements [ which ];

			Bind ( which );

			int code;

			while ( ( code = Native.strata_step ( statement ) ) == Native.Row ) {

			}

			Finish ( statement, code );

			return Native.strata_column_count ( statement ) == 0 ? ( int ) Native.strata_changes ( connection.Handle ) : 0;

		}

		public override int ExecuteNonQuery ( ) {

			Ready ( );

			int changes = 0;

			for ( int i = 0; i < statements.Count; i++ )
				changes += RunQuietly ( i );

			return changes;

		}

		public override object ExecuteScalar ( ) {

			Ready ( );

			object result = null;

			for ( int i = 0; i < statements.Count; i++ ) {

				IntPtr statement = statements [ i ];

				Bind ( i );

				int code = Native.strata_step ( statement );

				if ( code == Native.Row ) {

					if ( result == null ) {

						StrataValue* row = Native.strata_row ( statement );

						result = row != null && Native.strata_column_count ( statement ) > 0 ? StrataDataReader.ValueOf ( row ) : DBNull.Value;

					}

					int reset = Native.strata_reset ( statement );

					code = reset == Native.Ok ? Native.Done : reset;

				}

				Finish ( statement, code );

			}

			return result;

		}

		public new StrataDataReader ExecuteReader ( ) {

			return ( StrataDataReader ) ExecuteDbDataReader ( CommandBehavior.Default );

		}

		public new StrataDataReader ExecuteReader ( CommandBehavior behavior ) {

			return ( StrataDataReader ) ExecuteDbDataReader ( behavior );

		}

		protected override DbDataReader ExecuteDbDataReader ( CommandBehavior behavior ) {

			Ready ( );

			if ( statements.Count == 0 )
				throw new InvalidOperationException ( "the command has no SQL to run" );

			for ( int i = 0; i + 1 < statements.Count; i++ )
				RunQuietly ( i );

			IntPtr last = statements [ statements.Count - 1 ];

			Bind ( statements.Count - 1 );

			return new StrataDataReader ( this, last, behavior );

		}

		protected override void Dispose ( bool disposing ) {

			Unprepare ( );
			base.Dispose ( disposing );

		}

	}

}
