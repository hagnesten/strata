#include "server/server.hh"
#include "server/tls/tls.hh"
#include "base/console/console.hh"
#include "base/text/base64.hh"
#include "base/file/folder.hh"
#include "base/file/file.hh"
#include "base/clock/clock.hh"
#include "base/platform/os.hh"
#include "engine/value/moment.hh"

namespace server {

	static const uint8_t oid_ecdsa_sha256 [ ] = { 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02 };
	static const uint8_t oid_ec_public [ ] = { 0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01 };
	static const uint8_t oid_prime256 [ ] = { 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07 };
	static const uint8_t oid_common_name [ ] = { 0x06, 0x03, 0x55, 0x04, 0x03 };
	static const uint8_t oid_alt_names [ ] = { 0x06, 0x03, 0x55, 0x1D, 0x11 };

	static void der_wrap ( base::text& out, uint8_t tag, const base::text& body ) {

		out.add ( static_cast < char > ( tag ) );

		size_t size = body.size;

		if ( size < 128 )
			out.add ( static_cast < char > ( size ) );
		else if ( size < 256 ) {

			out.add ( static_cast < char > ( 0x81 ) );
			out.add ( static_cast < char > ( size ) );

		} else {

			out.add ( static_cast < char > ( 0x82 ) );
			out.add ( static_cast < char > ( size >> 8 ) );
			out.add ( static_cast < char > ( size ) );

		}

		out.add ( body.value ( ), body.size );

	}

	static void der_raw ( base::text& out, const uint8_t* bytes, size_t size ) {

		out.add ( reinterpret_cast < const char* > ( bytes ), size );

	}

	static void der_integer ( base::text& out, const uint8_t* bytes, size_t size ) {

		base::text body;

		while ( size > 1 && bytes [ 0 ] == 0 && !( bytes [ 1 ] & 0x80 ) ) {

			bytes++;
			size--;

		}

		if ( bytes [ 0 ] & 0x80 )
			body.add ( '\0' );

		der_raw ( body, bytes, size );
		der_wrap ( out, 0x02, body );

	}

	static void der_name ( base::text& out, const char* common ) {

		base::text value, pair, set;

		value.add ( common );
		der_raw ( pair, oid_common_name, sizeof ( oid_common_name ) );
		der_wrap ( pair, 0x0C, value );

		base::text sequence;

		der_wrap ( sequence, 0x30, pair );
		der_wrap ( set, 0x31, sequence );
		der_wrap ( out, 0x30, set );

	}

	static void utc_time ( base::text& out, int64_t seconds ) {

		int64_t days = seconds >= 0 ? seconds / 86400 : ( seconds - 86399 ) / 86400;
		int64_t rest = seconds - days * 86400;
		int64_t year = 0, month = 0, day = 0;

		engine::moment_civil_from_days ( days, year, month, day );

		base::text body;
		int64_t parts [ 6 ] = { year % 100, month, day, rest / 3600, rest / 60 % 60, rest % 60 };

		for ( int64_t part : parts ) {

			body.add ( static_cast < char > ( '0' + part / 10 ) );
			body.add ( static_cast < char > ( '0' + part % 10 ) );

		}

		body.add ( 'Z' );
		der_wrap ( out, 0x17, body );

	}

	static void der_signature ( base::text& out, const uint8_t r [ 32 ], const uint8_t s [ 32 ] ) {

		base::text body;

		der_integer ( body, r, 32 );
		der_integer ( body, s, 32 );
		der_wrap ( out, 0x30, body );

	}

	static void algorithm ( base::text& out ) {

		base::text body;

		der_raw ( body, oid_ecdsa_sha256, sizeof ( oid_ecdsa_sha256 ) );
		der_wrap ( out, 0x30, body );

	}

	static void public_key_info ( base::text& out, const uint8_t point [ P256_POINT_BYTES ] ) {

		base::text kind, bits, body;

		der_raw ( kind, oid_ec_public, sizeof ( oid_ec_public ) );
		der_raw ( kind, oid_prime256, sizeof ( oid_prime256 ) );
		der_wrap ( body, 0x30, kind );
		bits.add ( '\0' );
		der_raw ( bits, point, P256_POINT_BYTES );
		der_wrap ( body, 0x03, bits );
		der_wrap ( out, 0x30, body );

	}

	static void alt_names ( base::text& out ) {

		base::text names, list, holder, extension, extensions, wrapped;

		names.add ( "localhost" );
		der_wrap ( list, 0x82, names );

		base::text sequence;

		der_wrap ( sequence, 0x30, list );
		der_raw ( extension, oid_alt_names, sizeof ( oid_alt_names ) );
		der_wrap ( extension, 0x04, sequence );
		der_wrap ( holder, 0x30, extension );
		der_wrap ( extensions, 0x30, holder );
		der_wrap ( out, 0xA3, extensions );

	}

	static void add_pem ( base::text& out, const char* label, const base::text& der ) {

		base::text encoded;

		base::add_base64 ( encoded, der.value ( ), der.size );
		out.add ( "-----BEGIN " ).add ( label ).add ( "-----\n" );

		for ( size_t at = 0; at < encoded.size; at += 64 )
			out.add ( encoded.value ( ) + at, encoded.size - at < 64 ? encoded.size - at : 64 ).add ( '\n' );

		out.add ( "-----END " ).add ( label ).add ( "-----\n" );

	}

	bool tls_identity_make ( tls_identity& out, const char* certificate, const char* key, base::text& problem ) {

		uint8_t serial [ 16 ];
		uint8_t digest [ SHA256_BYTES ];
		uint8_t r [ 32 ], s [ 32 ];

		if ( !base::p256_new_secret ( out.secret ) || !base::p256_public ( out.secret, out.public_key ) || !base::os::random_bytes ( serial, sizeof ( serial ) ) ) {

			problem.add ( "a TLS key could not be made" );

			return false;

		}

		serial [ 0 ] = static_cast < uint8_t > ( ( serial [ 0 ] & 0x7F ) | 0x40 );

		int64_t now = base::unix_milliseconds ( ) / 1000;
		base::text tbs, body, version, validity, times;

		version.add ( "\x02\x01\x02", 3 );
		der_wrap ( body, 0xA0, version );
		der_integer ( body, serial, sizeof ( serial ) );
		algorithm ( body );
		der_name ( body, "strata" );
		utc_time ( times, now - 86400 );
		utc_time ( times, now + 3650ll * 86400 );
		der_wrap ( body, 0x30, times );
		der_name ( body, "strata" );
		public_key_info ( body, out.public_key );
		alt_names ( body );
		der_wrap ( tbs, 0x30, body );

		base::sha256 ( tbs.value ( ), tbs.size, digest );

		if ( !base::p256_sign ( out.secret, digest, r, s ) ) {

			problem.add ( "the TLS certificate could not be signed" );

			return false;

		}

		base::text signature, bits, whole, cert_body;

		der_signature ( signature, r, s );
		bits.add ( '\0' );
		bits.add ( signature.value ( ), signature.size );
		cert_body.add ( tbs.value ( ), tbs.size );
		algorithm ( cert_body );
		der_wrap ( cert_body, 0x03, bits );
		der_wrap ( whole, 0x30, cert_body );

		base::text key_der, key_body, curve, point_bits, point_holder, octets;

		key_body.add ( "\x02\x01\x01", 3 );
		octets.add ( reinterpret_cast < const char* > ( out.secret ), P256_SCALAR_BYTES );
		der_wrap ( key_body, 0x04, octets );
		der_raw ( curve, oid_prime256, sizeof ( oid_prime256 ) );
		der_wrap ( key_body, 0xA0, curve );
		point_bits.add ( '\0' );
		der_raw ( point_bits, out.public_key, P256_POINT_BYTES );
		der_wrap ( point_holder, 0x03, point_bits );
		der_wrap ( key_body, 0xA1, point_holder );
		der_wrap ( key_der, 0x30, key_body );

		base::text cert_pem, key_pem;

		add_pem ( cert_pem, "CERTIFICATE", whole );
		add_pem ( key_pem, "EC PRIVATE KEY", key_der );

		if ( !base::save_private_file ( key, key_pem.value ( ), key_pem.size ) || !base::save_file ( certificate, cert_pem.value ( ), cert_pem.size ) ) {

			problem.add ( "the TLS certificate could not be saved next to the database" );

			return false;

		}

		out.chain.clear ( );
		out.chain.add ( static_cast < char > ( whole.size >> 16 ) ).add ( static_cast < char > ( whole.size >> 8 ) ).add ( static_cast < char > ( whole.size ) );
		out.chain.add ( whole.value ( ), whole.size );
		out.chain.add ( "\0\0", 2 );
		out.chain_count = 1;
		out.ready = true;

		return true;

	}

	bool der_read ( const uint8_t* at, const uint8_t* end, der_item& out ) {

		if ( end - at < 2 )
			return false;

		out.tag = at [ 0 ];

		size_t size = at [ 1 ];

		at += 2;

		if ( size & 0x80 ) {

			size_t count = size & 0x7F;

			if ( count == 0 || count > 3 || static_cast < size_t > ( end - at ) < count )
				return false;

			size = 0;

			for ( size_t i = 0; i < count; i++ )
				size = size << 8 | *at++;

		}

		if ( static_cast < size_t > ( end - at ) < size )
			return false;

		out.body = at;
		out.size = size;
		out.next = at + size;

		return true;

	}

	static bool pem_blocks ( const base::text& file, const char* label, base::text* blocks, uint32_t room, uint32_t& count ) {

		base::text begin, finish;
		const char* text = file.value ( );

		begin.add ( "-----BEGIN " ).add ( label ).add ( "-----" );
		finish.add ( "-----END " ).add ( label ).add ( "-----" );
		count = 0;

		for ( size_t at = 0; at + begin.size <= file.size; at++ ) {

			if ( !base::same_folded_size ( text + at, begin.value ( ), begin.size ) )
				continue;

			size_t start = at + begin.size;
			size_t stop = start;

			while ( stop + finish.size <= file.size && !base::same_folded_size ( text + stop, finish.value ( ), finish.size ) )
				stop++;

			if ( stop + finish.size > file.size || count == room )
				return false;

			base::text compact;

			for ( size_t i = start; i < stop; i++ )
				if ( text [ i ] != '\r' && text [ i ] != '\n' && text [ i ] != ' ' && text [ i ] != '\t' )
					compact.add ( text [ i ] );

			size_t got = 0;

			if ( !blocks [ count ].reserve ( compact.size ) || !base::read_base64 ( compact.value ( ), compact.size, reinterpret_cast < uint8_t* > ( blocks [ count ].data ), compact.size, got ) )
				return false;

			blocks [ count ].size = got;
			count++;
			at = stop + finish.size;

		}

		return count > 0;

	}

	static bool sec1_secret ( const uint8_t* at, const uint8_t* end, uint8_t out [ 32 ] ) {

		der_item whole, version, octets;

		if ( !der_read ( at, end, whole ) || whole.tag != 0x30 || !der_read ( whole.body, whole.body + whole.size, version ) || version.tag != 0x02 || !der_read ( version.next, whole.body + whole.size, octets ) || octets.tag != 0x04 || octets.size > 32 || !octets.size )
			return false;

		for ( size_t i = 0; i < 32; i++ )
			out [ i ] = 0;

		for ( size_t i = 0; i < octets.size; i++ )
			out [ 32 - octets.size + i ] = octets.body [ i ];

		return true;

	}

	static bool pkcs8_secret ( const uint8_t* at, const uint8_t* end, uint8_t out [ 32 ] ) {

		der_item whole, version, kind, octets;

		if ( !der_read ( at, end, whole ) || whole.tag != 0x30 || !der_read ( whole.body, whole.body + whole.size, version ) || !der_read ( version.next, whole.body + whole.size, kind ) || kind.tag != 0x30 || !der_read ( kind.next, whole.body + whole.size, octets ) || octets.tag != 0x04 )
			return false;

		bool curve = false;

		for ( size_t i = 0; i + sizeof ( oid_prime256 ) <= kind.size; i++ ) {

			bool same = true;

			for ( size_t k = 0; k < sizeof ( oid_prime256 ) && same; k++ )
				same = kind.body [ i + k ] == oid_prime256 [ k ];

			curve = curve || same;

		}

		return curve && sec1_secret ( octets.body, octets.body + octets.size, out );

	}

	static bool contains ( const base::text& haystack, const uint8_t* needle, size_t size ) {

		const uint8_t* bytes = reinterpret_cast < const uint8_t* > ( haystack.value ( ) );

		for ( size_t at = 0; at + size <= haystack.size; at++ ) {

			bool same = true;

			for ( size_t k = 0; k < size && same; k++ )
				same = bytes [ at + k ] == needle [ k ];

			if ( same )
				return true;

		}

		return false;

	}

	bool tls_identity_load ( tls_identity& out, const char* certificate, const char* key, base::text& problem ) {

		base::text cert_file, key_file;
		base::text certs [ 8 ];
		base::text keys [ 1 ];
		uint32_t cert_count = 0, key_count = 0;

		if ( !base::read_whole ( certificate, cert_file ) || !base::read_whole ( key, key_file ) ) {

			problem.add ( "the TLS certificate or key file could not be read" );

			return false;

		}

		if ( !pem_blocks ( cert_file, "CERTIFICATE", certs, 8, cert_count ) ) {

			problem.add ( "the TLS certificate file holds no PEM certificate" );

			return false;

		}

		bool found = false;

		if ( pem_blocks ( key_file, "EC PRIVATE KEY", keys, 1, key_count ) )
			found = sec1_secret ( reinterpret_cast < const uint8_t* > ( keys [ 0 ].value ( ) ), reinterpret_cast < const uint8_t* > ( keys [ 0 ].value ( ) ) + keys [ 0 ].size, out.secret );
		else if ( pem_blocks ( key_file, "PRIVATE KEY", keys, 1, key_count ) )
			found = pkcs8_secret ( reinterpret_cast < const uint8_t* > ( keys [ 0 ].value ( ) ), reinterpret_cast < const uint8_t* > ( keys [ 0 ].value ( ) ) + keys [ 0 ].size, out.secret );

		key_file.clear ( );

		if ( !found || !base::p256_public ( out.secret, out.public_key ) ) {

			problem.add ( "the TLS key must be an ECDSA P-256 key in PEM form" );

			return false;

		}

		if ( !contains ( certs [ 0 ], out.public_key, P256_POINT_BYTES ) ) {

			problem.add ( "the TLS key does not belong to the first certificate in the file" );

			return false;

		}

		out.chain.clear ( );

		for ( uint32_t i = 0; i < cert_count; i++ ) {

			size_t size = certs [ i ].size;

			out.chain.add ( static_cast < char > ( size >> 16 ) ).add ( static_cast < char > ( size >> 8 ) ).add ( static_cast < char > ( size ) );
			out.chain.add ( certs [ i ].value ( ), size );
			out.chain.add ( "\0\0", 2 );

		}

		out.chain_count = cert_count;
		out.ready = true;

		return true;

	}

	bool tls_prepare ( server_state& state ) {

		const server_settings& settings = state.settings;

		if ( !settings.tls && !settings.tls_required && !settings.tls_certificate )
			return true;

		if ( !base::aes_gcm_supported ( ) ) {

			base::write_error ( "TLS needs a processor with AES and carry-less multiply instructions\n" );

			return false;

		}

		tls_identity* identity = base::make < tls_identity > ( 1 );
		base::text problem, certificate, key;

		if ( !identity )
			return false;

		if ( settings.tls_certificate ) {

			certificate.add ( settings.tls_certificate );
			key.add ( settings.tls_key ? settings.tls_key : settings.tls_certificate );

		} else {

			certificate.add ( settings.path ).add ( ".cert.pem" );
			key.add ( settings.path ).add ( ".key.pem" );

		}

		bool given = settings.tls_certificate != nullptr;
		bool stored = base::path_exists ( certificate.value ( ) ) && base::path_exists ( key.value ( ) );
		bool fine = given || stored ? tls_identity_load ( *identity, certificate.value ( ), key.value ( ), problem ) : tls_identity_make ( *identity, certificate.value ( ), key.value ( ), problem );

		if ( !fine ) {

			base::text line;

			line.add ( problem.value ( ) ).add ( '\n' );
			base::write_error ( line.value ( ) );
			base::release ( identity );

			return false;

		}

		state.identity = identity;

		base::text line;

		line.add ( "TLS 1.3 is on with " ).add ( given || stored ? "the certificate " : "a new self-signed certificate " ).add ( certificate.value ( ) );
		line.add ( settings.tls_required ? ", plain connections are refused" : "" );
		server_log ( state, line.value ( ) );

		return true;

	}

}
