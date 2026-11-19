#include "bench/profile/sampler.hh"
#include "base/text/text.hh"
#include "base/file/folder.hh"

#include <windows.h>

namespace bench {

	static void walk ( sampler& item, CONTEXT& context ) {

		uint64_t* out = item.frames + static_cast < size_t > ( item.count ) * SAMPLER_DEPTH;
		uint32_t depth = 0;

		while ( depth < SAMPLER_DEPTH && context.Rip ) {

			out [ depth++ ] = context.Rip;

			DWORD64 image = 0;
			PRUNTIME_FUNCTION function = RtlLookupFunctionEntry ( context.Rip, &image, nullptr );

			if ( !function ) {

				context.Rip = *reinterpret_cast < DWORD64* > ( context.Rsp );
				context.Rsp += 8;

				continue;

			}

			void* handler = nullptr;
			DWORD64 frame = 0;

			RtlVirtualUnwind ( UNW_FLAG_NHANDLER, image, context.Rip, function, &context, &handler, &frame, nullptr );

		}

		item.depths [ item.count++ ] = static_cast < uint8_t > ( depth );

	}

	static DWORD WINAPI sample_loop ( void* context ) {

		sampler& item = *static_cast < sampler* > ( context );
		LARGE_INTEGER rate;
		LARGE_INTEGER now;

		QueryPerformanceFrequency ( &rate );
		QueryPerformanceCounter ( &now );

		int64_t step = rate.QuadPart * SAMPLER_MICROSECONDS / 1000000;
		int64_t next = now.QuadPart + step;

		while ( !item.stopping ) {

			QueryPerformanceCounter ( &now );

			if ( now.QuadPart < next ) {

				YieldProcessor ( );

				continue;

			}

			next = now.QuadPart + step;

			if ( item.paused )
				continue;

			if ( item.count >= SAMPLER_ROOM ) {

				item.lost++;

				continue;

			}

			CONTEXT state = { };

			state.ContextFlags = CONTEXT_FULL;

			if ( SuspendThread ( item.target ) == static_cast < DWORD > ( -1 ) )
				continue;

			if ( GetThreadContext ( item.target, &state ) )
				walk ( item, state );

			ResumeThread ( item.target );

		}

		return 0;

	}

	bool start_sampler ( sampler& item ) {

		item = { };
		item.frames = base::make < uint64_t > ( static_cast < size_t > ( SAMPLER_ROOM ) * SAMPLER_DEPTH );
		item.depths = base::make < uint8_t > ( SAMPLER_ROOM );

		HANDLE self = nullptr;

		if ( !item.frames || !item.depths || !DuplicateHandle ( GetCurrentProcess ( ), GetCurrentThread ( ), GetCurrentProcess ( ), &self, THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0 ) )
			return false;

		item.target = self;
		item.worker = CreateThread ( nullptr, 0, sample_loop, &item, 0, nullptr );

		return item.worker != nullptr;

	}

	static void add_place ( base::text& out, uint64_t address, uint64_t own_base, uint64_t own_size ) {

		if ( address >= own_base && address < own_base + own_size ) {

			out.add ( 's' ).hex ( address - own_base, 8 );

			return;

		}

		HMODULE module = nullptr;
		wchar_t wide [ MAX_PATH ] = { };
		char name [ MAX_PATH ] = { };

		if ( !GetModuleHandleExW ( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast < LPCWSTR > ( address ), &module ) ) {

			out.add ( '?' ).hex ( address, 16 );

			return;

		}

		GetModuleFileNameW ( module, wide, MAX_PATH );

		const wchar_t* last = wide;

		for ( const wchar_t* at = wide; *at; at++ )
			if ( *at == '\\' )
				last = at + 1;

		WideCharToMultiByte ( CP_UTF8, 0, last, -1, name, MAX_PATH, nullptr, nullptr );
		out.add ( name ).add ( '+' ).hex ( address - reinterpret_cast < uint64_t > ( module ), 8 );

	}

	bool stop_sampler ( sampler& item, const char* path ) {

		item.stopping = true;

		if ( item.worker ) {

			WaitForSingleObject ( item.worker, INFINITE );
			CloseHandle ( item.worker );

		}

		if ( item.target )
			CloseHandle ( item.target );

		const uint8_t* image = reinterpret_cast < const uint8_t* > ( GetModuleHandleW ( nullptr ) );
		const IMAGE_NT_HEADERS* headers = reinterpret_cast < const IMAGE_NT_HEADERS* > ( image + reinterpret_cast < const IMAGE_DOS_HEADER* > ( image )->e_lfanew );
		uint64_t own_base = reinterpret_cast < uint64_t > ( image );
		uint64_t own_size = headers->OptionalHeader.SizeOfImage;
		base::text out;

		for ( uint32_t i = 0; i < item.count; i++ ) {

			const uint64_t* frames = item.frames + static_cast < size_t > ( i ) * SAMPLER_DEPTH;

			for ( uint32_t d = 0; d < item.depths [ i ]; d++ ) {

				if ( d )
					out.add ( ' ' );

				add_place ( out, frames [ d ], own_base, own_size );

			}

			out.add ( '\n' );

		}

		bool fine = base::write_whole ( path, out.data, out.size );

		base::release ( item.frames );
		base::release ( item.depths );
		item.frames = nullptr;
		item.depths = nullptr;

		return fine;

	}

}
