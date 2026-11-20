#include "base/platform/os.hh"

#include <winsock2.h>
#include <ws2tcpip.h>

namespace base {

	namespace os {

		static bool sockets_ready;

		static bool start_sockets ( ) {

			if ( sockets_ready )
				return true;

			WSADATA data;

			if ( WSAStartup ( MAKEWORD ( 2, 2 ), &data ) != 0 )
				return false;

			sockets_ready = true;

			return true;

		}

		bool connect_tcp ( void*& out, const char* host, uint16_t port ) {

			out = nullptr;

			if ( !start_sockets ( ) )
				return false;

			char service [ 8 ];
			uint32_t digits = 0;
			char reversed [ 8 ];
			uint32_t value = port;

			do {

				reversed [ digits++ ] = static_cast < char > ( '0' + value % 10 );
				value /= 10;

			} while ( value );

			for ( uint32_t i = 0; i < digits; i++ )
				service [ i ] = reversed [ digits - 1 - i ];

			service [ digits ] = 0;

			ADDRINFOA hints = { };
			ADDRINFOA* found = nullptr;

			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;

			if ( getaddrinfo ( host, service, &hints, &found ) != 0 )
				return false;

			SOCKET made = INVALID_SOCKET;

			for ( ADDRINFOA* at = found; at; at = at->ai_next ) {

				made = socket ( at->ai_family, at->ai_socktype, at->ai_protocol );

				if ( made == INVALID_SOCKET )
					continue;

				if ( connect ( made, at->ai_addr, static_cast < int > ( at->ai_addrlen ) ) == 0 )
					break;

				closesocket ( made );
				made = INVALID_SOCKET;

			}

			freeaddrinfo ( found );

			if ( made == INVALID_SOCKET )
				return false;

			BOOL on = TRUE;

			setsockopt ( made, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast < const char* > ( &on ), sizeof ( on ) );
			out = reinterpret_cast < void* > ( made );

			return true;

		}

		bool send_all ( void* socket, const void* data, size_t size ) {

			SOCKET item = reinterpret_cast < SOCKET > ( socket );
			const char* at = static_cast < const char* > ( data );

			while ( size ) {

				int chunk = size > 0x40000000 ? 0x40000000 : static_cast < int > ( size );
				int sent = send ( item, at, chunk, 0 );

				if ( sent <= 0 )
					return false;

				at += sent;
				size -= static_cast < size_t > ( sent );

			}

			return true;

		}

		bool receive_some ( void* socket, void* out, size_t room, size_t& got ) {

			int chunk = room > 0x40000000 ? 0x40000000 : static_cast < int > ( room );
			int read = recv ( reinterpret_cast < SOCKET > ( socket ), static_cast < char* > ( out ), chunk, 0 );

			got = read > 0 ? static_cast < size_t > ( read ) : 0;

			return read > 0;

		}

		void close_socket ( void* socket ) {

			if ( socket )
				closesocket ( reinterpret_cast < SOCKET > ( socket ) );

		}

	}

}
