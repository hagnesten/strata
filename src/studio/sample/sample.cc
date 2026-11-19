#include "studio/app/studio.hh"

namespace studio {

	static const char* const first_names [ ] = { "Ada", "Alan", "Grace", "Linus", "Barbara", "Edsger", "Margaret", "Dennis", "Ken", "Frances", "Donald", "Radia", "Bjarne", "Hedy", "John", "Katherine", "Niklaus", "Sophie", "Tim", "Anita", "Leslie", "Shafi", "Guido", "Joan" };
	static const char* const last_names [ ] = { "Lovelace", "Turing", "Hopper", "Torvalds", "Liskov", "Dijkstra", "Hamilton", "Ritchie", "Thompson", "Allen", "Knuth", "Perlman", "Stroustrup", "Lamarr", "Backus", "Johnson", "Wirth", "Wilson", "Lee", "Borg", "Lamport", "Goldwasser", "Rossum", "Clarke" };
	static const char* const cities [ ] = { "London", "Oslo", "Stockholm", "Helsinki", "Berlin", "Paris", "Lisbon", "Toronto", "Boston", "Tokyo", "Seoul", "Zurich" };
	static const char* const adjectives [ ] = { "Quiet", "Granite", "Amber", "Polar", "Velvet", "Iron", "Copper", "Lunar", "Cedar", "Slate" };
	static const char* const nouns [ ] = { "Keyboard", "Lamp", "Notebook", "Kettle", "Backpack", "Chair" };
	static const char* const categories [ ] = { "Office", "Home", "Office", "Kitchen", "Travel", "Home" };
	static const char* const states [ ] = { "placed", "packed", "shipped", "delivered", "delivered", "delivered", "returned" };

	static uint64_t next ( uint64_t& state ) {

		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;

		return state;

	}

	static bool flush ( engine::database& db, base::text& sql, bool& any ) {

		if ( !any )
			return true;

		sql.add ( ';' );

		bool fine = engine::execute ( db, sql.value ( ), nullptr, 0, nullptr );

		sql.clear ( );
		any = false;

		return fine;

	}

	bool build_sample ( engine::database& db ) {

		const char* schema =
			"BEGIN;"
			"CREATE TABLE customers ( id INTEGER PRIMARY KEY, name TEXT NOT NULL, email TEXT UNIQUE, city TEXT, joined TEXT );"
			"CREATE TABLE products ( id INTEGER PRIMARY KEY, name TEXT NOT NULL, category TEXT, price REAL CHECK ( price >= 0 ) );"
			"CREATE TABLE orders ( id INTEGER PRIMARY KEY, customer INT NOT NULL REFERENCES customers ( id ), placed TEXT, status TEXT );"
			"CREATE TABLE order_items ( order_id INT REFERENCES orders ( id ) ON DELETE CASCADE, product INT REFERENCES products ( id ), quantity INT CHECK ( quantity > 0 ), PRIMARY KEY ( order_id, product ) );"
			"CREATE TABLE reviews ( id INTEGER PRIMARY KEY, product INT REFERENCES products ( id ), customer INT REFERENCES customers ( id ), stars INT CHECK ( stars BETWEEN 1 AND 5 ), body TEXT );"
			"CREATE INDEX orders_customer ON orders ( customer );"
			"CREATE INDEX products_category ON products ( category, price );"
			"CREATE INDEX reviews_product ON reviews ( product );";

		if ( !engine::execute ( db, schema, nullptr, 0, nullptr ) )
			return false;

		uint64_t state = 0x853C49E6748FEA9Bull;
		base::text sql;
		bool any = false;
		bool fine = true;
		static const uint32_t customers = 600;
		static const uint32_t products = 60;
		static const uint32_t orders = 4000;

		for ( uint32_t i = 1; i <= customers && fine; i++ ) {

			const char* first = first_names [ next ( state ) % 24 ];
			const char* last = last_names [ next ( state ) % 24 ];

			sql.add ( any ? ", " : "INSERT INTO customers ( name, email, city, joined ) VALUES " );
			sql.add ( "( '" ).add ( first ).add ( ' ' ).add ( last ).add ( "', '" );

			for ( const char* at = first; *at; at++ )
				sql.add ( base::lower ( *at ) );

			sql.add ( '.' );

			for ( const char* at = last; *at; at++ )
				sql.add ( base::lower ( *at ) );

			sql.number ( i ).add ( "@example.org', '" ).add ( cities [ next ( state ) % 12 ] ).add ( "', '202" ).number ( 2 + next ( state ) % 4 ).add ( '-' );
			sql.add ( '0' ).number ( 1 + next ( state ) % 9 ).add ( "-1" ).number ( next ( state ) % 9 ).add ( "' )" );
			any = true;

			if ( i % 200 == 0 )
				fine = flush ( db, sql, any );

		}

		fine = fine && flush ( db, sql, any );

		for ( uint32_t i = 0; i < products && fine; i++ ) {

			uint32_t noun = i % 6;
			uint64_t cents = 900 + next ( state ) % 24000;

			sql.add ( any ? ", " : "INSERT INTO products ( name, category, price ) VALUES " );
			sql.add ( "( '" ).add ( adjectives [ i / 6 % 10 ] ).add ( ' ' ).add ( nouns [ noun ] ).add ( "', '" ).add ( categories [ noun ] ).add ( "', " );
			sql.decimal ( cents, 100, 2 ).add ( " )" );
			any = true;

		}

		fine = fine && flush ( db, sql, any );

		for ( uint32_t i = 1; i <= orders && fine; i++ ) {

			sql.add ( any ? ", " : "INSERT INTO orders ( customer, placed, status ) VALUES " );
			sql.add ( '(' ).number ( 1 + next ( state ) % customers ).add ( ", '2025-" );

			uint64_t month = 1 + next ( state ) % 12;
			uint64_t day = 1 + next ( state ) % 28;

			if ( month < 10 )
				sql.add ( '0' );

			sql.number ( month ).add ( '-' );

			if ( day < 10 )
				sql.add ( '0' );

			sql.number ( day ).add ( "', '" ).add ( states [ next ( state ) % 7 ] ).add ( "' )" );
			any = true;

			if ( i % 250 == 0 )
				fine = flush ( db, sql, any );

		}

		fine = fine && flush ( db, sql, any );

		uint32_t items = 0;

		for ( uint32_t order = 1; order <= orders && fine; order++ ) {

			uint32_t lines = 1 + static_cast < uint32_t > ( next ( state ) % 4 );
			uint32_t start = static_cast < uint32_t > ( next ( state ) % products );

			for ( uint32_t k = 0; k < lines; k++ ) {

				sql.add ( any ? ", " : "INSERT INTO order_items ( order_id, product, quantity ) VALUES " );
				sql.add ( '(' ).number ( order ).add ( ", " ).number ( 1 + ( start + k * 7 ) % products ).add ( ", " ).number ( 1 + next ( state ) % 5 ).add ( ')' );
				any = true;
				items++;

			}

			if ( items > 300 ) {

				fine = flush ( db, sql, any );
				items = 0;

			}

		}

		fine = fine && flush ( db, sql, any );

		static const char* const words [ ] = { "Solid and quiet.", "Arrived late but works well.", "Exactly as described.", "Would buy again.", "The finish scratches easily.", "Great value for the price.", "Heavier than expected." };

		for ( uint32_t i = 1; i <= 900 && fine; i++ ) {

			sql.add ( any ? ", " : "INSERT INTO reviews ( product, customer, stars, body ) VALUES " );
			sql.add ( '(' ).number ( 1 + next ( state ) % products ).add ( ", " ).number ( 1 + next ( state ) % customers ).add ( ", " ).number ( 1 + next ( state ) % 5 ).add ( ", '" ).add ( words [ next ( state ) % 7 ] ).add ( "' )" );
			any = true;

			if ( i % 300 == 0 )
				fine = flush ( db, sql, any );

		}

		fine = fine && flush ( db, sql, any );

		if ( !fine ) {

			engine::execute ( db, "ROLLBACK", nullptr, 0, nullptr );

			return false;

		}

		return engine::execute ( db, "COMMIT", nullptr, 0, nullptr );

	}

}
