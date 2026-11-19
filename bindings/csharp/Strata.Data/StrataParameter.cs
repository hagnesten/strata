using System;
using System.Data;
using System.Data.Common;

namespace Strata.Data {

	public sealed class StrataParameter : DbParameter {

		private string name = "";
		private string source = "";
		private object value;
		private DbType type = DbType.Object;
		private ParameterDirection direction = ParameterDirection.Input;
		private int size;
		private bool nullable = true;
		private bool null_mapping;
		private DataRowVersion version = DataRowVersion.Current;

		public StrataParameter ( ) {

		}

		public StrataParameter ( string name, object value ) {

			this.name = name ?? "";
			this.value = value;

		}

		public override DbType DbType {

			get { return type; }
			set { type = value; }

		}

		public override ParameterDirection Direction {

			get { return direction; }

			set {

				if ( value != ParameterDirection.Input )
					throw new NotSupportedException ( "Strata parameters are input only" );

				direction = value;

			}

		}

		public override bool IsNullable {

			get { return nullable; }
			set { nullable = value; }

		}

		public override string ParameterName {

			get { return name; }
			set { name = value ?? ""; }

		}

		public override int Size {

			get { return size; }
			set { size = value; }

		}

		public override string SourceColumn {

			get { return source; }
			set { source = value ?? ""; }

		}

		public override bool SourceColumnNullMapping {

			get { return null_mapping; }
			set { null_mapping = value; }

		}

		public override DataRowVersion SourceVersion {

			get { return version; }
			set { version = value; }

		}

		public override object Value {

			get { return value; }
			set { this.value = value; }

		}

		public override void ResetDbType ( ) {

			type = DbType.Object;

		}

	}

}
