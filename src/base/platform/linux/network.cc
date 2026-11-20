#include "base/platform/os.hh"
#include "base/platform/linux/calls.hh"

namespace base {

	namespace os {

		static long descriptor_of ( void* socket ) {

			return reinterpret_cast < long > ( socket ) - 1;

		}

		static void* socket_of ( long descriptor ) {

			return reinterpret_cast < void* > ( descriptor + 1 );

		}

		static uint16_t big16 ( uint16_t value ) {

			return static_cast < uint16_t > ( value << 8 | value >> 8 );

		}

		static bool read_address ( const char* host, uint32_t& out ) {

			if ( !host || !*host || host [ 0 ] == '*' ) {

				out = 0;

				return true;

			}

			const char* at = host;

			if ( at [ 0 ] == 'l' && at [ 1 ] == 'o' && at [ 2 ] == 'c' )
				at = "127.0.0.1";

			uint32_t parts [ 4 ] = { };

			for ( uint32_t i = 0; i < 4; i++ ) {

				uint32_t value = 0;
				uint32_t digits = 0;

				while ( *at >= '0' && *at <= '9' && digits < 3 ) {

					value = value * 10 + static_cast < uint32_t > ( *at++ - '0' );
					digits++;

				}

				if ( !digits || value > 255 )
					return false;

				parts [ i ] = value;

				if ( i < 3 && *at++ != '.' )
					return false;

			}

			if ( *at )
				return false;

			out = parts [ 0 ] | parts [ 1 ] << 8 | parts [ 2 ] << 16 | parts [ 3 ] << 24;

			return true;

		}

		static void quick_send ( long descriptor ) {

			int32_t on = 1;

			linux_calls::call6 ( LINUX_SETSOCKOPT, descriptor, LINUX_IPPROTO_TCP, LINUX_TCP_NODELAY, reinterpret_cast < long > ( &on ), sizeof ( on ), 0 );

		}

		bool connect_tcp ( void*& out, const char* host, uint16_t port ) {

			out = nullptr;

			linux_calls::socket_address address = { };

			if ( !read_address ( host, address.address ) )
				return false;

			address.family = LINUX_AF_INET;
			address.port = big16 ( port );

			long made = linux_calls::call3 ( LINUX_SOCKET, LINUX_AF_INET, LINUX_SOCK_STREAM | LINUX_CLOSE_ON_EXEC, 0 );

			if ( made < 0 )
				return false;

			if ( linux_calls::call3 ( LINUX_CONNECT, made, reinterpret_cast < long > ( &address ), sizeof ( address ) ) < 0 ) {

				linux_calls::call1 ( LINUX_CLOSE, made );

				return false;

			}

			quick_send ( made );
			out = socket_of ( made );

			return true;

		}

		bool send_all ( void* socket, const void* data, size_t size ) {

			const uint8_t* at = static_cast < const uint8_t* > ( data );

			while ( size ) {

				long put = linux_calls::call6 ( LINUX_SENDTO, descriptor_of ( socket ), reinterpret_cast < long > ( at ), static_cast < long > ( size ), LINUX_MSG_NOSIGNAL, 0, 0 );

				if ( put == -LINUX_AGAIN ) {

					linux_calls::poll_entry wait = { static_cast < int32_t > ( descriptor_of ( socket ) ), LINUX_POLL_OUT, 0 };

					linux_calls::call3 ( LINUX_POLL, reinterpret_cast < long > ( &wait ), 1, -1 );

					continue;

				}

				if ( put <= 0 )
					return false;

				at += put;
				size -= static_cast < size_t > ( put );

			}

			return true;

		}

		bool receive_some ( void* socket, void* out, size_t room, size_t& got ) {

			long read = linux_calls::call6 ( LINUX_RECVFROM, descriptor_of ( socket ), reinterpret_cast < long > ( out ), static_cast < long > ( room ), 0, 0, 0 );

			got = read > 0 ? static_cast < size_t > ( read ) : 0;

			return read > 0;

		}

		void close_socket ( void* socket ) {

			if ( socket )
				linux_calls::call1 ( LINUX_CLOSE, descriptor_of ( socket ) );

		}

		bool listen_tcp ( void*& out, const char* host, uint16_t port ) {

			out = nullptr;

			linux_calls::socket_address address = { };

			if ( !read_address ( host, address.address ) )
				return false;

			address.family = LINUX_AF_INET;
			address.port = big16 ( port );

			long made = linux_calls::call3 ( LINUX_SOCKET, LINUX_AF_INET, LINUX_SOCK_STREAM | LINUX_SOCK_NONBLOCK | LINUX_CLOSE_ON_EXEC, 0 );

			if ( made < 0 )
				return false;

			int32_t on = 1;

			linux_calls::call6 ( LINUX_SETSOCKOPT, made, LINUX_SOL_SOCKET, LINUX_SO_REUSEADDR, reinterpret_cast < long > ( &on ), sizeof ( on ), 0 );

			if ( linux_calls::call3 ( LINUX_BIND, made, reinterpret_cast < long > ( &address ), sizeof ( address ) ) < 0 || linux_calls::call2 ( LINUX_LISTEN, made, 512 ) < 0 ) {

				linux_calls::call1 ( LINUX_CLOSE, made );

				return false;

			}

			out = socket_of ( made );

			return true;

		}

		bool accept_tcp ( void* listener, void*& out, bool& accepted ) {

			out = nullptr;
			accepted = false;

			long made = linux_calls::call4 ( LINUX_ACCEPT4, descriptor_of ( listener ), 0, 0, LINUX_SOCK_NONBLOCK | LINUX_CLOSE_ON_EXEC );

			if ( made < 0 )
				return made == -LINUX_AGAIN;

			quick_send ( made );
			out = socket_of ( made );
			accepted = true;

			return true;

		}

		bool send_some ( void* socket, const void* data, size_t size, size_t& sent ) {

			long put = linux_calls::call6 ( LINUX_SENDTO, descriptor_of ( socket ), reinterpret_cast < long > ( data ), static_cast < long > ( size ), LINUX_MSG_NOSIGNAL, 0, 0 );

			sent = put > 0 ? static_cast < size_t > ( put ) : 0;

			return put >= 0 || put == -LINUX_AGAIN;

		}

		bool receive_now ( void* socket, void* out, size_t room, size_t& got, bool& closed ) {

			long read = linux_calls::call6 ( LINUX_RECVFROM, descriptor_of ( socket ), reinterpret_cast < long > ( out ), static_cast < long > ( room ), 0, 0, 0 );

			got = read > 0 ? static_cast < size_t > ( read ) : 0;
			closed = read == 0;

			return read >= 0 || read == -LINUX_AGAIN;

		}

		bool poll_sockets ( socket_poll* items, uint32_t count, int32_t timeout ) {

			linux_calls::poll_entry polled [ LINUX_POLL_ROOM ];

			if ( count > LINUX_POLL_ROOM )
				count = LINUX_POLL_ROOM;

			for ( uint32_t i = 0; i < count; i++ ) {

				polled [ i ].descriptor = static_cast < int32_t > ( descriptor_of ( items [ i ].socket ) );
				polled [ i ].events = static_cast < int16_t > ( ( items [ i ].want_read ? LINUX_POLL_IN : 0 ) | ( items [ i ].want_write ? LINUX_POLL_OUT : 0 ) );
				polled [ i ].returned = 0;

			}

			if ( linux_calls::call3 ( LINUX_POLL, reinterpret_cast < long > ( polled ), count, timeout ) < 0 )
				return false;

			for ( uint32_t i = 0; i < count; i++ ) {

				items [ i ].readable = ( polled [ i ].returned & ( LINUX_POLL_IN | LINUX_POLL_HANGUP ) ) != 0;
				items [ i ].writable = ( polled [ i ].returned & LINUX_POLL_OUT ) != 0;
				items [ i ].failed = ( polled [ i ].returned & ( LINUX_POLL_ERROR | LINUX_POLL_INVALID ) ) != 0;

			}

			return true;

		}

	}

}
