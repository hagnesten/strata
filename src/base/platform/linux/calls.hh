#pragma once

#include <stdint.h>
#include <stddef.h>

#define LINUX_READ 0
#define LINUX_WRITE 1
#define LINUX_CLOSE 3
#define LINUX_LSEEK 8
#define LINUX_MMAP 9
#define LINUX_MUNMAP 11
#define LINUX_PREAD 17
#define LINUX_PWRITE 18
#define LINUX_ACCESS 21
#define LINUX_YIELD 24
#define LINUX_NANOSLEEP 35
#define LINUX_LOCK_SET 37
#define LINUX_LOCK_WAIT 38
#define LINUX_LOCK_READ 0
#define LINUX_LOCK_WRITE 1
#define LINUX_LOCK_NONE 2
#define LINUX_PROT_NONE 0
#define LINUX_MPROTECT 10
#define LINUX_MAP_SHARED 1
#define LINUX_GETPID 39
#define LINUX_FCNTL 72
#define LINUX_FSYNC 74
#define LINUX_FDATASYNC 75
#define LINUX_FTRUNCATE 77
#define LINUX_RENAME 82
#define LINUX_MKDIR 83
#define LINUX_UNLINK 87
#define LINUX_FUTEX 202
#define LINUX_GETDENTS 217
#define LINUX_CLOCK_GETTIME 228
#define LINUX_EXIT_GROUP 231
#define LINUX_OPENAT 257
#define LINUX_FSTATAT 262
#define LINUX_GETRANDOM 318
#define LINUX_POLL 7
#define LINUX_SOCKET 41
#define LINUX_CONNECT 42
#define LINUX_SENDTO 44
#define LINUX_RECVFROM 45
#define LINUX_BIND 49
#define LINUX_LISTEN 50
#define LINUX_SETSOCKOPT 54
#define LINUX_ACCEPT4 288
#define LINUX_AF_INET 2
#define LINUX_SOCK_STREAM 1
#define LINUX_SOCK_NONBLOCK 0x800
#define LINUX_SOL_SOCKET 1
#define LINUX_SO_REUSEADDR 2
#define LINUX_IPPROTO_TCP 6
#define LINUX_TCP_NODELAY 1
#define LINUX_MSG_NOSIGNAL 0x4000
#define LINUX_AGAIN 11
#define LINUX_POLL_IN 1
#define LINUX_POLL_OUT 4
#define LINUX_POLL_ERROR 8
#define LINUX_POLL_HANGUP 0x10
#define LINUX_POLL_INVALID 0x20
#define LINUX_POLL_ROOM 512

#define LINUX_AT_CWD -100
#define LINUX_READ_ONLY 0
#define LINUX_READ_WRITE 2
#define LINUX_CREATE 0x40
#define LINUX_DIRECTORY 0x10000
#define LINUX_CLOSE_ON_EXEC 0x80000
#define LINUX_PROT_READ_WRITE 3
#define LINUX_MAP_PRIVATE_ANONYMOUS 0x22
#define LINUX_SEEK_END 2
#define LINUX_EXISTS 17
#define LINUX_CLOCK_REALTIME 0
#define LINUX_CLOCK_MONOTONIC 1
#define LINUX_TYPE_MASK 0170000
#define LINUX_TYPE_DIRECTORY 0040000
#define LINUX_DIRENT_DIRECTORY 4
#define LINUX_DIRENT_FILE 8

namespace base {

	namespace linux_calls {

		struct timespec_value {

			int64_t seconds;
			int64_t nanoseconds;

		};

		struct stat_value {

			uint64_t device;
			uint64_t inode;
			uint64_t links;
			uint32_t mode;
			uint32_t user;
			uint32_t group;
			uint32_t padding;
			uint64_t special;
			int64_t size;
			int64_t block_size;
			int64_t blocks;
			uint64_t times [ 6 ];
			int64_t reserved [ 3 ];

		};

		struct lock_value {

			int16_t type;
			int16_t whence;
			int32_t padding;
			int64_t start;
			int64_t length;
			int32_t process;
			int32_t tail;

		};

		struct socket_address {

			uint16_t family;
			uint16_t port;
			uint32_t address;
			uint8_t zero [ 8 ];

		};

		struct poll_entry {

			int32_t descriptor;
			int16_t events;
			int16_t returned;

		};

		struct directory_entry {

			uint64_t inode;
			int64_t offset;
			uint16_t length;
			uint8_t type;
			char name [ 1 ];

		};

		inline long call1 ( long number, long a ) {

			long result;

			__asm__ volatile ( "syscall" : "=a" ( result ) : "a" ( number ), "D" ( a ) : "rcx", "r11", "memory" );

			return result;

		}

		inline long call2 ( long number, long a, long b ) {

			long result;

			__asm__ volatile ( "syscall" : "=a" ( result ) : "a" ( number ), "D" ( a ), "S" ( b ) : "rcx", "r11", "memory" );

			return result;

		}

		inline long call3 ( long number, long a, long b, long c ) {

			long result;

			__asm__ volatile ( "syscall" : "=a" ( result ) : "a" ( number ), "D" ( a ), "S" ( b ), "d" ( c ) : "rcx", "r11", "memory" );

			return result;

		}

		inline long call4 ( long number, long a, long b, long c, long d ) {

			long result;
			register long r10 __asm__ ( "r10" ) = d;

			__asm__ volatile ( "syscall" : "=a" ( result ) : "a" ( number ), "D" ( a ), "S" ( b ), "d" ( c ), "r" ( r10 ) : "rcx", "r11", "memory" );

			return result;

		}

		inline long call6 ( long number, long a, long b, long c, long d, long e, long f ) {

			long result;
			register long r10 __asm__ ( "r10" ) = d;
			register long r8 __asm__ ( "r8" ) = e;
			register long r9 __asm__ ( "r9" ) = f;

			__asm__ volatile ( "syscall" : "=a" ( result ) : "a" ( number ), "D" ( a ), "S" ( b ), "d" ( c ), "r" ( r10 ), "r" ( r8 ), "r" ( r9 ) : "rcx", "r11", "memory" );

			return result;

		}

		inline bool failed ( long result ) {

			return result < 0 && result > -4096;

		}

		void set_environment ( char** environment );

		char** environment ( );

	}

}
