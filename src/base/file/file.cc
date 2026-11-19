#include "base/file/file.hh"
#include "base/platform/os.hh"

namespace base {

	file_faults faults;

	void plan_fault ( uint64_t after, bool torn ) {

		faults = { };
		faults.armed = true;
		faults.countdown = after;
		faults.torn = torn;

	}

	void clear_faults ( ) {

		uint64_t operations = faults.operations;

		faults = { };
		faults.operations = operations;

	}

	static bool fault_now ( ) {

		faults.operations++;

		if ( !faults.armed )
			return false;

		if ( faults.tripped )
			return true;

		if ( faults.countdown && --faults.countdown == 0 ) {

			faults.tripped = true;

			return true;

		}

		return false;

	}

	bool open_file ( file_handle& out, const char* path, bool create ) {

		out.handle = nullptr;

		return os::open_file ( out.handle, path, true, create );

	}

	bool open_reading ( file_handle& out, const char* path ) {

		out.handle = nullptr;

		return os::open_file ( out.handle, path, false, false );

	}

	bool read_at ( const file_handle& file, uint64_t offset, void* out, size_t size ) {

		uint8_t* at = static_cast < uint8_t* > ( out );

		while ( size ) {

			size_t got = 0;

			if ( !os::read_at ( file.handle, offset, at, min < size_t > ( size, 1u << 30 ), got ) || !got )
				return false;

			at += got;
			offset += got;
			size -= got;

		}

		return true;

	}

	static bool write_fully ( const file_handle& file, uint64_t offset, const uint8_t* at, size_t size ) {

		while ( size ) {

			size_t put = 0;

			if ( !os::write_at ( file.handle, offset, at, min < size_t > ( size, 1u << 30 ), put ) || !put )
				return false;

			at += put;
			offset += put;
			size -= put;

		}

		return true;

	}

	bool write_at ( const file_handle& file, uint64_t offset, const void* data, size_t size ) {

		const uint8_t* at = static_cast < const uint8_t* > ( data );
		bool was_tripped = faults.tripped;

		if ( fault_now ( ) ) {

			if ( !was_tripped && faults.torn && size > 1 )
				write_fully ( file, offset, at, size / 2 );

			return false;

		}

		return write_fully ( file, offset, at, size );

	}

	bool sync_file ( const file_handle& file ) {

		if ( fault_now ( ) )
			return false;

		return os::sync_file ( file.handle );

	}

	uint64_t file_size ( const file_handle& file ) {

		uint64_t size = 0;

		return os::file_size ( file.handle, size ) ? size : 0;

	}

	bool truncate_file ( const file_handle& file, uint64_t size ) {

		if ( fault_now ( ) )
			return false;

		return os::truncate_file ( file.handle, size );

	}

	void close_file ( file_handle& file ) {

		if ( file.handle )
			os::close_file ( file.handle );

		file.handle = nullptr;

	}

	bool erase_path ( const char* path ) {

		return os::erase_path ( path );

	}

	bool replace_path ( const char* from, const char* to ) {

		return os::replace_path ( from, to );

	}

	bool path_exists ( const char* path ) {

		return os::path_exists ( path );

	}

	bool temp_path ( char* out, size_t room, const char* name ) {

		text folder;

		if ( !os::temp_folder ( folder ) || folder.size + length ( name ) + 1 > room )
			return false;

		folder.add ( name );
		copy_text ( out, room, folder.value ( ) );

		return true;

	}

}
