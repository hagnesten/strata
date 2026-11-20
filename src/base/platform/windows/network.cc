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

		bool listen_tcp ( void*& out, const char* host, uint16_t port ) {

			out = nullptr;

			if ( !start_sockets ( ) )
				return false;

			sockaddr_in address = { };

			address.sin_family = AF_INET;
			address.sin_port = htons ( port );

			if ( !host || !*host || host [ 0 ] == '*' )
				address.sin_addr.s_addr = htonl ( INADDR_ANY );
			else if ( inet_pton ( AF_INET, host [ 0 ] == 'l' ? "127.0.0.1" : host, &address.sin_addr ) != 1 )
				return false;

			SOCKET made = socket ( AF_INET, SOCK_STREAM, IPPROTO_TCP );

			if ( made == INVALID_SOCKET )
				return false;

			BOOL exclusive = TRUE;
			u_long nonblocking = 1;

			setsockopt ( made, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast < const char* > ( &exclusive ), sizeof ( exclusive ) );

			if ( bind ( made, reinterpret_cast < sockaddr* > ( &address ), sizeof ( address ) ) != 0 || listen ( made, SOMAXCONN ) != 0 || ioctlsocket ( made, FIONBIO, &nonblocking ) != 0 ) {

				closesocket ( made );

				return false;

			}

			out = reinterpret_cast < void* > ( made );

			return true;

		}

		bool accept_tcp ( void* listener, void*& out, bool& accepted ) {

			out = nullptr;
			accepted = false;

			SOCKET made = accept ( reinterpret_cast < SOCKET > ( listener ), nullptr, nullptr );

			if ( made == INVALID_SOCKET )
				return WSAGetLastError ( ) == WSAEWOULDBLOCK;

			BOOL on = TRUE;
			u_long nonblocking = 1;

			setsockopt ( made, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast < const char* > ( &on ), sizeof ( on ) );
			ioctlsocket ( made, FIONBIO, &nonblocking );
			out = reinterpret_cast < void* > ( made );
			accepted = true;

			return true;

		}

		bool send_some ( void* socket, const void* data, size_t size, size_t& sent ) {

			int chunk = size > 0x40000000 ? 0x40000000 : static_cast < int > ( size );
			int put = send ( reinterpret_cast < SOCKET > ( socket ), static_cast < const char* > ( data ), chunk, 0 );

			sent = 0;

			if ( put >= 0 ) {

				sent = static_cast < size_t > ( put );

				return true;

			}

			return WSAGetLastError ( ) == WSAEWOULDBLOCK;

		}

		bool receive_now ( void* socket, void* out, size_t room, size_t& got, bool& closed ) {

			int chunk = room > 0x40000000 ? 0x40000000 : static_cast < int > ( room );
			int read = recv ( reinterpret_cast < SOCKET > ( socket ), static_cast < char* > ( out ), chunk, 0 );

			got = 0;
			closed = false;

			if ( read > 0 ) {

				got = static_cast < size_t > ( read );

				return true;

			}

			if ( read == 0 ) {

				closed = true;

				return true;

			}

			return WSAGetLastError ( ) == WSAEWOULDBLOCK;

		}

		bool poll_sockets ( socket_poll* items, uint32_t count, int32_t timeout ) {

			WSAPOLLFD polled [ 512 ];

			if ( count > 512 )
				count = 512;

			for ( uint32_t i = 0; i < count; i++ ) {

				polled [ i ].fd = reinterpret_cast < SOCKET > ( items [ i ].socket );
				polled [ i ].events = static_cast < SHORT > ( ( items [ i ].want_read ? POLLRDNORM : 0 ) | ( items [ i ].want_write ? POLLWRNORM : 0 ) );
				polled [ i ].revents = 0;

			}

			if ( WSAPoll ( polled, count, timeout ) < 0 )
				return false;

			for ( uint32_t i = 0; i < count; i++ ) {

				items [ i ].readable = ( polled [ i ].revents & ( POLLRDNORM | POLLHUP ) ) != 0;
				items [ i ].writable = ( polled [ i ].revents & POLLWRNORM ) != 0;
				items [ i ].failed = ( polled [ i ].revents & ( POLLERR | POLLNVAL ) ) != 0;

			}

			return true;

		}

	}

}
