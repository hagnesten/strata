extern "C" __attribute__ ( ( naked ) ) void __chkstk ( ) {

	__asm__ volatile (
		"push %rcx\n"
		"push %rax\n"
		"cmp $0x1000, %rax\n"
		"lea 24(%rsp), %rcx\n"
		"jb 2f\n"
		"1:\n"
		"sub $0x1000, %rcx\n"
		"test %rcx, (%rcx)\n"
		"sub $0x1000, %rax\n"
		"cmp $0x1000, %rax\n"
		"ja 1b\n"
		"2:\n"
		"sub %rax, %rcx\n"
		"test %rcx, (%rcx)\n"
		"pop %rax\n"
		"pop %rcx\n"
		"ret\n" );

}
