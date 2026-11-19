extern "C" {

	__attribute__ ( ( visibility ( "hidden" ) ) ) void* __dso_handle = &__dso_handle;

	int __cxa_atexit ( void ( *finish ) ( void* ), void* argument, void* handle ) {

		( void ) finish;
		( void ) argument;
		( void ) handle;

		return 0;

	}

}
