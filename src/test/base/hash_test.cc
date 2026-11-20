#include "test/harness/harness.hh"
#include "base/hash/scram.hh"
#include "base/text/base64.hh"
#include "base/runtime/memory.hh"

namespace test {

	static bool same_hex ( const uint8_t* bytes, size_t size, const char* wanted ) {

		base::text written;

		for ( size_t i = 0; i < size; i++ )
			written.hex ( bytes [ i ], 2 );

		return base::same ( written.value ( ), wanted );

	}

	static bool sha_of ( const char* value, const char* wanted ) {

		uint8_t out [ SHA256_BYTES ];

		base::sha256 ( value, base::length ( value ), out );

		return same_hex ( out, SHA256_BYTES, wanted );

	}

	static void sha256_tests ( ) {

		expect ( sha_of ( "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" ), "SHA-256 of nothing" );
		expect ( sha_of ( "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" ), "SHA-256 of abc" );
		expect ( sha_of ( "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" ), "SHA-256 across two blocks" );

		base::sha256_state state;
		uint8_t chunk [ 1001 ];
		uint8_t out [ SHA256_BYTES ];

		base::fill ( chunk, 'a', sizeof ( chunk ) );
		base::sha256_start ( state );

		for ( uint32_t i = 0; i < 1000; i++ )
			base::sha256_add ( state, chunk, i % 2 ? 999 : 1001 );

		base::sha256_finish ( state, out );
		expect ( same_hex ( out, SHA256_BYTES, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0" ), "SHA-256 of a million letters fed in uneven pieces" );

		const char* data = "what do ya want for nothing?";

		base::hmac_sha256 ( "Jefe", 4, data, base::length ( data ), out );
		expect ( same_hex ( out, SHA256_BYTES, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843" ), "HMAC-SHA-256 with a short key" );

		uint8_t long_key [ 131 ];

		base::fill ( long_key, 0xAA, sizeof ( long_key ) );
		data = "Test Using Larger Than Block-Size Key - Hash Key First";
		base::hmac_sha256 ( long_key, sizeof ( long_key ), data, base::length ( data ), out );
		expect ( same_hex ( out, SHA256_BYTES, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54" ), "HMAC-SHA-256 with a key longer than a block" );

		uint8_t derived [ 64 ];

		base::pbkdf2_sha256 ( "passwd", 6, "salt", 4, 1, derived, sizeof ( derived ) );
		expect ( same_hex ( derived, sizeof ( derived ), "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783" ), "PBKDF2-HMAC-SHA-256 with one round and two blocks" );

		base::pbkdf2_sha256 ( "Password", 8, "NaCl", 4, 80000, derived, sizeof ( derived ) );
		expect ( same_hex ( derived, sizeof ( derived ), "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d" ), "PBKDF2-HMAC-SHA-256 with 80,000 rounds" );

	}

	static bool base64_of ( const char* value, const char* wanted ) {

		base::text written;
		uint8_t back [ 64 ];
		size_t got = 0;

		base::add_base64 ( written, value, base::length ( value ) );

		return base::same ( written.value ( ), wanted ) && base::read_base64 ( written.data, written.size, back, sizeof ( back ), got ) && got == base::length ( value ) && base::same_bytes ( back, value, got );

	}

	static void base64_tests ( ) {

		expect ( base64_of ( "", "" ), "base64 of nothing" );
		expect ( base64_of ( "f", "Zg==" ), "base64 with two pads" );
		expect ( base64_of ( "fo", "Zm8=" ), "base64 with one pad" );
		expect ( base64_of ( "foobar", "Zm9vYmFy" ), "base64 without pads" );

		uint8_t back [ 8 ];
		size_t got = 0;

		expect ( !base::read_base64 ( "Zm9v!mFy", 8, back, sizeof ( back ), got ), "base64 refuses a stray letter" );
		expect ( !base::read_base64 ( "Zm9vYmFy", 8, back, 4, got ), "base64 refuses to overrun its room" );

	}

	static void scram_tests ( ) {

		const char* client_first = "n=user,r=rOprNGfwEbeRWgbNEkqO";
		const char* server_first = "r=rOprNGfwEbeRWgbNEkqO%hvYDpWUa2RaTCAfuxFIlj)hNlF$k0,s=W22ZaJ0SNY7soEsUEjb6gQ==,i=4096";
		const char* client_final = "c=biws,r=rOprNGfwEbeRWgbNEkqO%hvYDpWUa2RaTCAfuxFIlj)hNlF$k0";
		base::scram_answer answer;
		base::text proof;
		base::text server;

		bool fine = base::scram_prove ( "pencil", 6, client_first, base::length ( client_first ), server_first, base::length ( server_first ), client_final, base::length ( client_final ), answer );

		base::add_base64 ( proof, answer.proof, SHA256_BYTES );
		base::add_base64 ( server, answer.server, SHA256_BYTES );
		expect ( fine && base::same ( proof.value ( ), "dHzbZapWIk4jUhN+Ute9ytag9zjfMHgsqmmiz7AndVQ=" ), "SCRAM-SHA-256 client proof matches RFC 7677" );
		expect ( fine && base::same ( server.value ( ), "6rriTRBi23WpRR/wtup+mMhUZUn/dB5nLTJRsjl95G4=" ), "SCRAM-SHA-256 server signature matches RFC 7677" );

		const char* value = nullptr;
		size_t size = 0;

		expect ( base::scram_field ( server_first, base::length ( server_first ), 'i', value, size ) && size == 4 && base::same_bytes ( value, "4096", 4 ), "SCRAM fields are found by name" );
		expect ( !base::scram_prove ( "pencil", 6, client_first, base::length ( client_first ), "r=abc,i=4096", 12, client_final, base::length ( client_final ), answer ), "SCRAM refuses a challenge without salt" );
		expect ( !base::scram_prove ( "pencil", 6, client_first, base::length ( client_first ), "r=abc,s=W22ZaJ0SNY7soEsUEjb6gQ==,i=0", 36, client_final, base::length ( client_final ), answer ), "SCRAM refuses zero rounds" );

	}

	void hash_tests ( ) {

		section ( "hashes and sign-in" );
		sha256_tests ( );
		base64_tests ( );
		scram_tests ( );

	}

}
