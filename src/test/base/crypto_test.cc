#include "test/harness/harness.hh"
#include "base/crypto/x25519.hh"
#include "base/crypto/p256.hh"
#include "base/crypto/aes_gcm.hh"
#include "base/crypto/hkdf.hh"
#include "base/text/text.hh"

namespace test {

	static uint32_t from_hex ( const char* text, uint8_t* out ) {

		uint32_t count = 0;

		for ( ; text [ 0 ] && text [ 1 ]; text += 2 ) {

			auto digit = [ ] ( char c ) { return c <= '9' ? c - '0' : ( c | 32 ) - 'a' + 10; };

			out [ count++ ] = static_cast < uint8_t > ( digit ( text [ 0 ] ) << 4 | digit ( text [ 1 ] ) );

		}

		return count;

	}

	static bool hex_is ( const uint8_t* bytes, size_t size, const char* wanted ) {

		base::text written;

		for ( size_t i = 0; i < size; i++ )
			written.hex ( bytes [ i ], 2 );

		return base::same ( written.value ( ), wanted );

	}

	static void x25519_tests ( ) {

		uint8_t alice [ 32 ], bob [ 32 ], alice_public [ 32 ], bob_public [ 32 ], shared_a [ 32 ], shared_b [ 32 ];

		from_hex ( "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", alice );
		from_hex ( "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", bob );
		base::x25519_public ( alice, alice_public );
		base::x25519_public ( bob, bob_public );
		expect ( hex_is ( alice_public, 32, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a" ), "X25519 public key of Alice" );
		expect ( hex_is ( bob_public, 32, "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f" ), "X25519 public key of Bob" );
		expect ( base::x25519 ( alice, bob_public, shared_a ) && base::x25519 ( bob, alice_public, shared_b ), "X25519 agreement succeeds" );
		expect ( hex_is ( shared_a, 32, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742" ) && hex_is ( shared_b, 32, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742" ), "X25519 shared secret" );

		uint8_t zero_point [ 32 ] = { };

		expect ( !base::x25519 ( alice, zero_point, shared_a ), "X25519 refuses a zero shared secret" );

	}

	static void p256_tests ( ) {

		uint8_t secret [ 32 ], point [ 65 ], digest [ 32 ], r [ 32 ], s [ 32 ];

		from_hex ( "c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721", secret );
		expect ( base::p256_public ( secret, point ), "P-256 public key is made" );
		expect ( hex_is ( point + 1, 32, "60fed4ba255a9d31c961eb74c6356d68c049b8923b61fa6ce669622e60f29fb6" ) && hex_is ( point + 33, 32, "7903fe1008b8bc99a41ae9e95628bc64f2f1b20c2d7e9f5177a3c294d4462299" ), "P-256 public key matches RFC 6979" );

		base::sha256 ( "sample", 6, digest );
		expect ( base::p256_sign ( secret, digest, r, s ), "P-256 signs" );
		expect ( hex_is ( r, 32, "efd48b2aacb6a8fd1140dd9cd45e81d69d2c877b56aaf991c34d0ea84eaf3716" ) && hex_is ( s, 32, "f7cb1c942d657c41d436c7a1b6e29f65f3e900dbb9aff4064dc4ab2f843acda8" ), "P-256 deterministic signature matches RFC 6979" );
		expect ( base::p256_verify ( point, digest, r, s ), "P-256 signature verifies" );

		digest [ 0 ] ^= 1;
		expect ( !base::p256_verify ( point, digest, r, s ), "P-256 rejects a changed digest" );

		uint8_t fresh [ 32 ], fresh_point [ 65 ];

		expect ( base::p256_new_secret ( fresh ) && base::p256_public ( fresh, fresh_point ) && base::p256_sign ( fresh, digest, r, s ) && base::p256_verify ( fresh_point, digest, r, s ), "P-256 fresh key round trip" );

	}

	static void gcm_tests ( ) {

		if ( !base::aes_gcm_supported ( ) ) {

			expect ( true, "AES-GCM skipped, the processor has no AES instructions" );

			return;

		}

		uint8_t key [ 16 ] = { }, nonce [ 12 ] = { }, plain [ 64 ] = { }, sealed [ 80 ], opened [ 64 ], aad [ 20 ];
		base::aes_gcm_key schedule;

		base::aes_gcm_setup ( schedule, key );
		base::aes_gcm_seal ( schedule, nonce, nullptr, 0, plain, 16, sealed );
		expect ( hex_is ( sealed, 32, "0388dace60b6a392f328c2b971b2fe78ab6e47d42cec13bdf53a67b21257bddf" ), "AES-GCM test case 2" );

		from_hex ( "feffe9928665731c6d6a8f9467308308", key );
		from_hex ( "cafebabefacedbaddecaf888", nonce );
		uint32_t size = from_hex ( "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39", plain );
		uint32_t aad_size = from_hex ( "feedfacedeadbeeffeedfacedeadbeefabaddad2", aad );

		base::aes_gcm_setup ( schedule, key );
		base::aes_gcm_seal ( schedule, nonce, aad, aad_size, plain, size, sealed );
		expect ( hex_is ( sealed, size + 16, "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e0915bc94fbc3221a5db94fae95ae7121a47" ), "AES-GCM test case 4" );
		expect ( base::aes_gcm_open ( schedule, nonce, aad, aad_size, sealed, size + 16, opened ) && hex_is ( opened, size, "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39" ), "AES-GCM opens test case 4" );

		sealed [ 5 ] ^= 0x40;
		expect ( !base::aes_gcm_open ( schedule, nonce, aad, aad_size, sealed, size + 16, opened ), "AES-GCM rejects a changed byte" );

	}

	static void hkdf_tests ( ) {

		uint8_t input [ 22 ], salt [ 13 ], info [ 10 ], prk [ 32 ], okm [ 42 ];

		for ( int i = 0; i < 22; i++ )
			input [ i ] = 0x0b;

		for ( int i = 0; i < 13; i++ )
			salt [ i ] = static_cast < uint8_t > ( i );

		for ( int i = 0; i < 10; i++ )
			info [ i ] = static_cast < uint8_t > ( 0xf0 + i );

		base::hkdf_extract ( salt, 13, input, 22, prk );
		base::hkdf_expand ( prk, info, 10, okm, 42 );
		expect ( hex_is ( prk, 32, "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5" ), "HKDF extract" );
		expect ( hex_is ( okm, 42, "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865" ), "HKDF expand" );

	}

	void crypto_tests ( ) {

		section ( "cryptography" );
		x25519_tests ( );
		p256_tests ( );
		gcm_tests ( );
		hkdf_tests ( );

	}

}
