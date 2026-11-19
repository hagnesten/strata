using System;
using System.Collections;
using System.Collections.Generic;
using System.Data.Common;

namespace Strata.Data {

	public sealed class StrataParameterCollection : DbParameterCollection {

		private readonly List < StrataParameter > items = new List < StrataParameter > ( );
		private int version;

		internal int Version {

			get { return version; }

		}

		public override int Count {

			get { return items.Count; }

		}

		public override bool IsFixedSize {

			get { return false; }

		}

		public override bool IsReadOnly {

			get { return false; }

		}

		public override bool IsSynchronized {

			get { return false; }

		}

		public override object SyncRoot {

			get { return items; }

		}

		public new StrataParameter this [ int index ] {

			get { return items [ index ]; }
			set { items [ index ] = value; version++; }

		}

		public new StrataParameter this [ string name ] {

			get { return items [ IndexOf ( name ) ]; }
			set { items [ IndexOf ( name ) ] = value; version++; }

		}

		internal StrataParameter At ( int index ) {

			return items [ index ];

		}

		public StrataParameter AddWithValue ( string name, object value ) {

			StrataParameter made = new StrataParameter ( name, value );

			items.Add ( made );
			version++;

			return made;

		}

		public override int Add ( object value ) {

			items.Add ( Cast ( value ) );
			version++;

			return items.Count - 1;

		}

		public override void AddRange ( Array values ) {

			foreach ( object value in values )
				Add ( value );

		}

		public override void Clear ( ) {

			items.Clear ( );
			version++;

		}

		public override bool Contains ( object value ) {

			return value is StrataParameter && items.Contains ( ( StrataParameter ) value );

		}

		public override bool Contains ( string value ) {

			return IndexOf ( value ) >= 0;

		}

		public override void CopyTo ( Array array, int index ) {

			( ( ICollection ) items ).CopyTo ( array, index );

		}

		public override IEnumerator GetEnumerator ( ) {

			return items.GetEnumerator ( );

		}

		protected override DbParameter GetParameter ( int index ) {

			return items [ index ];

		}

		protected override DbParameter GetParameter ( string parameterName ) {

			int index = IndexOf ( parameterName );

			return index >= 0 ? items [ index ] : null;

		}

		public override int IndexOf ( object value ) {

			return value is StrataParameter ? items.IndexOf ( ( StrataParameter ) value ) : -1;

		}

		public override int IndexOf ( string parameterName ) {

			if ( parameterName == null )
				return -1;

			int skip = Prefix ( parameterName );
			int size = parameterName == null ? 0 : parameterName.Length - skip;

			for ( int i = 0; i < items.Count; i++ ) {

				string name = items [ i ].ParameterName;
				int start = Prefix ( name );

				if ( name.Length - start == size && string.Compare ( name, start, parameterName, skip, size, StringComparison.OrdinalIgnoreCase ) == 0 )
					return i;

			}

			return -1;

		}

		public override void Insert ( int index, object value ) {

			items.Insert ( index, Cast ( value ) );
			version++;

		}

		public override void Remove ( object value ) {

			if ( value is StrataParameter )
				items.Remove ( ( StrataParameter ) value );

			version++;

		}

		public override void RemoveAt ( int index ) {

			items.RemoveAt ( index );
			version++;

		}

		public override void RemoveAt ( string parameterName ) {

			int index = IndexOf ( parameterName );

			if ( index >= 0 )
				items.RemoveAt ( index );

			version++;

		}

		protected override void SetParameter ( int index, DbParameter value ) {

			items [ index ] = Cast ( value );
			version++;

		}

		protected override void SetParameter ( string parameterName, DbParameter value ) {

			int index = IndexOf ( parameterName );

			if ( index < 0 )
				items.Add ( Cast ( value ) );
			else
				items [ index ] = Cast ( value );

			version++;

		}

		private static int Prefix ( string name ) {

			if ( string.IsNullOrEmpty ( name ) )
				return 0;

			char first = name [ 0 ];

			return first == ':' || first == '@' || first == '$' ? 1 : 0;

		}

		private static StrataParameter Cast ( object value ) {

			StrataParameter parameter = value as StrataParameter;

			if ( parameter == null )
				throw new InvalidCastException ( "a Strata command takes StrataParameter objects" );

			return parameter;

		}

	}

}
