#pragma once

#include "base/hash/sha256.hh"

#define SCRAM_SALT_ROOM 128

namespace base {

	struct scram_answer {

		uint8_t proof [ SHA256_BYTES ];
		uint8_t server [ SHA256_BYTES ];

	};

	bool scram_field ( const char* message, size_t size, char name, const char*& value, size_t& value_size );

	bool scram_prove ( const char* password, size_t password_size, const char* client_first, size_t client_first_size, const char* server_first, size_t server_first_size, const char* client_final, size_t client_final_size, scram_answer& out );

}
