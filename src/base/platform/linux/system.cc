#include "base/platform/os.hh"
#include "base/platform/linux/calls.hh"
#include "base/platform/linux/zone.hh"
#include "base/text/text.hh"
#include "base/file/file.hh"

namespace base {

	namespace linux_calls {

		static char** saved_environment;

		void set_environment ( char** environment ) {

			saved_environment = environment;

		}

		char** environment ( ) {

			return saved_environment;

		}

	}

	namespace os {

		using namespace linux_calls;

		static long descriptor ( void* file ) {

			return static_cast < long > ( reinterpret_cast < intptr_t > ( file ) ) - 1;

		}

		void* allocate_pages ( size_t size ) {

			long made = call6 ( LINUX_MMAP, 0, static_cast < long > ( size ), LINUX_PROT_READ_WRITE, LINUX_MAP_PRIVATE_ANONYMOUS, -1, 0 );

			return failed ( made ) ? nullptr : reinterpret_cast < void* > ( made );

		}

		void release_pages ( void* at, size_t size ) {

			if ( at )
				call2 ( LINUX_MUNMAP, reinterpret_cast < long > ( at ), static_cast < long > ( size ) );

		}

		bool open_file ( void*& out, const char* path, bool write, bool create ) {

			long flags = ( write ? LINUX_READ_WRITE : LINUX_READ_ONLY ) | ( create ? LINUX_CREATE : 0 ) | LINUX_CLOSE_ON_EXEC;
			long made = call4 ( LINUX_OPENAT, LINUX_AT_CWD, reinterpret_cast < long > ( path ), flags, 0644 );

			out = nullptr;

			if ( failed ( made ) )
				return false;

			out = reinterpret_cast < void* > ( static_cast < intptr_t > ( made + 1 ) );

			return true;

		}

		bool read_at ( void* file, uint64_t offset, void* out, size_t size, size_t& got ) {

			long read = call4 ( LINUX_PREAD, descriptor ( file ), reinterpret_cast < long > ( out ), static_cast < long > ( size ), static_cast < long > ( offset ) );

			got = failed ( read ) ? 0 : static_cast < size_t > ( read );

			return !failed ( read );

		}

		bool write_at ( void* file, uint64_t offset, const void* data, size_t size, size_t& put ) {

			long wrote = call4 ( LINUX_PWRITE, descriptor ( file ), reinterpret_cast < long > ( data ), static_cast < long > ( size ), static_cast < long > ( offset ) );

			put = failed ( wrote ) ? 0 : static_cast < size_t > ( wrote );

			return !failed ( wrote );

		}

		bool sync_file ( void* file ) {

			return !failed ( call1 ( LINUX_FDATASYNC, descriptor ( file ) ) );

		}

		bool file_size ( void* file, uint64_t& out ) {

			long size = call3 ( LINUX_LSEEK, descriptor ( file ), 0, LINUX_SEEK_END );

			if ( failed ( size ) )
				return false;

			out = static_cast < uint64_t > ( size );

			return true;

		}

		bool truncate_file ( void* file, uint64_t size ) {

			return !failed ( call2 ( LINUX_FTRUNCATE, descriptor ( file ), static_cast < long > ( size ) ) );

		}

		void close_file ( void* file ) {

			call1 ( LINUX_CLOSE, descriptor ( file ) );

		}

		void* map_file ( void* file, uint64_t offset, size_t size, void*& mapping ) {

			uint64_t current = 0;

			mapping = nullptr;

			if ( !file_size ( file, current ) || ( current < offset + size && !truncate_file ( file, offset + size ) ) )
				return nullptr;

			long made = call6 ( LINUX_MMAP, 0, static_cast < long > ( size ), LINUX_PROT_READ_WRITE, LINUX_MAP_SHARED, descriptor ( file ), static_cast < long > ( offset ) );

			return failed ( made ) ? nullptr : reinterpret_cast < void* > ( made );

		}

		void unmap_file ( void* view, size_t size, void* mapping ) {

			( void ) mapping;

			if ( view )
				call2 ( LINUX_MUNMAP, reinterpret_cast < long > ( view ), static_cast < long > ( size ) );

		}

		bool lock_range ( void* file, uint64_t offset, uint64_t size, bool exclusive, bool wait ) {

			lock_value request = { };

			request.type = exclusive ? LINUX_LOCK_WRITE : LINUX_LOCK_READ;
			request.start = static_cast < int64_t > ( offset );
			request.length = static_cast < int64_t > ( size );

			return !failed ( call3 ( LINUX_FCNTL, descriptor ( file ), wait ? LINUX_LOCK_WAIT : LINUX_LOCK_SET, reinterpret_cast < long > ( &request ) ) );

		}

		void unlock_range ( void* file, uint64_t offset, uint64_t size ) {

			lock_value request = { };

			request.type = LINUX_LOCK_NONE;
			request.start = static_cast < int64_t > ( offset );
			request.length = static_cast < int64_t > ( size );
			call3 ( LINUX_FCNTL, descriptor ( file ), LINUX_LOCK_SET, reinterpret_cast < long > ( &request ) );

		}

		void sleep_milliseconds ( uint32_t milliseconds ) {

			timespec_value wait = { milliseconds / 1000, static_cast < int64_t > ( milliseconds % 1000 ) * 1000000 };

			call2 ( LINUX_NANOSLEEP, reinterpret_cast < long > ( &wait ), 0 );

		}

		bool erase_path ( const char* path ) {

			return !failed ( call1 ( LINUX_UNLINK, reinterpret_cast < long > ( path ) ) );

		}

		bool replace_path ( const char* from, const char* to ) {

			return !failed ( call2 ( LINUX_RENAME, reinterpret_cast < long > ( from ), reinterpret_cast < long > ( to ) ) );

		}

		bool path_exists ( const char* path ) {

			return !failed ( call2 ( LINUX_ACCESS, reinterpret_cast < long > ( path ), 0 ) );

		}

		bool make_folder ( const char* path ) {

			long made = call2 ( LINUX_MKDIR, reinterpret_cast < long > ( path ), 0755 );

			return !failed ( made ) || made == -LINUX_EXISTS;

		}

		static bool read_small ( const char* path, text& out ) {

			void* file = nullptr;
			char chunk [ 4096 ];
			uint64_t offset = 0;

			out.clear ( );

			if ( !open_file ( file, path, false, false ) )
				return false;

			for ( ;; ) {

				size_t got = 0;

				if ( !read_at ( file, offset, chunk, sizeof ( chunk ), got ) || !got )
					break;

				out.add ( chunk, got );
				offset += got;

			}

			close_file ( file );

			return true;

		}

		bool environment ( const char* name, text& out ) {

			size_t size = length ( name );

			out.clear ( );

			if ( char** listed = linux_calls::environment ( ) ) {

				for ( char** at = listed; *at; at++ ) {

					if ( same_bytes ( *at, name, size ) && ( *at ) [ size ] == '=' ) {

						out.add ( *at + size + 1 );

						return true;

					}

				}

				return false;

			}

			text whole;

			if ( !read_small ( "/proc/self/environ", whole ) )
				return false;

			for ( size_t at = 0; at < whole.size; ) {

				const char* entry = whole.data + at;
				size_t entry_size = length ( entry );

				if ( entry_size > size && same_bytes ( entry, name, size ) && entry [ size ] == '=' ) {

					out.add ( entry + size + 1 );

					return true;

				}

				at += entry_size + 1;

			}

			return false;

		}

		bool temp_folder ( text& out ) {

			if ( !environment ( "TMPDIR", out ) || !out.size )
				out.add ( "/tmp" );

			if ( out.data [ out.size - 1 ] != '/' )
				out.add ( '/' );

			return true;

		}

		bool list_folder ( const char* path, folder_visit visit, void* context ) {

			long folder = call4 ( LINUX_OPENAT, LINUX_AT_CWD, reinterpret_cast < long > ( path ), LINUX_READ_ONLY | LINUX_DIRECTORY | LINUX_CLOSE_ON_EXEC, 0 );

			if ( failed ( folder ) )
				return false;

			alignas ( 8 ) char buffer [ 8192 ];
			bool fine = true;

			for ( ;; ) {

				long got = call3 ( LINUX_GETDENTS, folder, reinterpret_cast < long > ( buffer ), sizeof ( buffer ) );

				if ( failed ( got ) ) {

					fine = false;

					break;

				}

				if ( !got )
					break;

				for ( long at = 0; at < got && fine; ) {

					const directory_entry* entry = reinterpret_cast < const directory_entry* > ( buffer + at );

					at += entry->length;

					if ( entry->name [ 0 ] == '.' )
						continue;

					stat_value status = { };
					bool known = !failed ( call4 ( LINUX_FSTATAT, folder, reinterpret_cast < long > ( entry->name ), reinterpret_cast < long > ( &status ), 0 ) );
					bool directory = known ? ( status.mode & LINUX_TYPE_MASK ) == LINUX_TYPE_DIRECTORY : entry->type == LINUX_DIRENT_DIRECTORY;

					fine = visit ( context, entry->name, directory, known && !directory ? static_cast < uint64_t > ( status.size ) : 0 );

				}

				if ( !fine )
					break;

			}

			call1 ( LINUX_CLOSE, folder );

			return fine;

		}

		bool list_roots ( folder_visit visit, void* context ) {

			return visit ( context, "/", true, 0 );

		}

		char separator ( ) {

			return '/';

		}

		void write_stream ( int32_t which, const char* data, size_t size ) {

			while ( size ) {

				long wrote = call3 ( LINUX_WRITE, which == OS_ERROR ? 2 : 1, reinterpret_cast < long > ( data ), static_cast < long > ( size ) );

				if ( failed ( wrote ) || !wrote )
					return;

				data += wrote;
				size -= static_cast < size_t > ( wrote );

			}

		}

		bool read_input ( char* out, size_t room, size_t& got ) {

			long read = call3 ( LINUX_READ, 0, reinterpret_cast < long > ( out ), static_cast < long > ( room ) );

			got = failed ( read ) ? 0 : static_cast < size_t > ( read );

			return !failed ( read );

		}

		void use_utf8 ( ) {

		}

		static timespec_value clock_now ( long which ) {

			timespec_value now = { };

			call2 ( LINUX_CLOCK_GETTIME, which, reinterpret_cast < long > ( &now ) );

			return now;

		}

		int64_t unix_milliseconds ( ) {

			timespec_value now = clock_now ( LINUX_CLOCK_REALTIME );

			return now.seconds * 1000 + now.nanoseconds / 1000000;

		}

		int64_t local_offset_milliseconds ( ) {

			text zone;

			if ( environment ( "TZ", zone ) && ( !zone.size || base::same ( zone.value ( ), "UTC" ) || base::same ( zone.value ( ), "UTC0" ) || base::same ( zone.value ( ), ":UTC" ) ) )
				return 0;

			text data;
			const char* path = zone.size && zone.data [ 0 ] == ':' && zone.data [ 1 ] == '/' ? zone.value ( ) + 1 : "/etc/localtime";

			if ( !read_small ( path, data ) )
				return 0;

			return zone_offset_seconds ( reinterpret_cast < const uint8_t* > ( data.data ), data.size, unix_milliseconds ( ) / 1000 ) * 1000;

		}

		uint64_t ticks ( ) {

			timespec_value now = clock_now ( LINUX_CLOCK_MONOTONIC );

			return static_cast < uint64_t > ( now.seconds ) * 1000000000ull + static_cast < uint64_t > ( now.nanoseconds );

		}

		uint64_t tick_rate ( ) {

			return 1000000000ull;

		}

		uint32_t process_id ( ) {

			return static_cast < uint32_t > ( call1 ( LINUX_GETPID, 0 ) );

		}

		void exit_process ( int32_t code ) {

			for ( ;; )
				call1 ( LINUX_EXIT_GROUP, code );

		}

	}

}
