/* Copyright (C) 1995 DJ Delorie, see COPYING.DJ for details */

/* Modified December 22, 1997  for LWP 2.0  Josh Turpen */
/* Win95 has some wierd re-entrancy problems with int 0x31, ax = 0x0300 */
/* and ___djgpp_hw_exception */

#define USE_EBX
#define USE_EDI

#include <libc/asmdefs.h>

#define DPMI(x)		movw $(x),%ax; int $0x31; jnc L_noerror; movw %ax,___dpmi_error; movl $-1,%eax; jmp L_leave; L_noerror:
#define DPMIce(x)	movw $(x),%ax; int $0x31; movw %ax,___dpmi_error; jc L_error

	.comm	___dpmi_error,2

	.text
	.comm	___dpmi_int_ss, 2
	.comm	___dpmi_int_sp, 2
	.comm	___dpmi_int_flags, 2
	.lcomm	dpmi_busy, 1

	.text

	FUNC(___dpmi_int)
	ENTER

	pushl	%es
	pushl   %ss
	movw	___djgpp_ds_alias, %ax
	movw	%ax, %es
	movw    %ax, %ss

	movl	ARG1, %ebx
	movl	$0, %ecx
	movl	ARG2, %edi
	incb	dpmi_busy
	
	cmpb	$1, dpmi_busy
	jne	L_is_busy
	
	movw	___dpmi_int_flags, %ax
	movw	%ax, 0x20(%edi)	/* r.x.flags */
	movw	___dpmi_int_sp, %ax
	movw	%ax, 0x2e(%edi)	/* r.x.sp */
	movw	___dpmi_int_ss, %ax
	movw	%ax, 0x30(%edi)	/* r.x.ss */
	jmp	L_not_busy
	
L_is_busy:
	movw	$0, 0x20(%edi)	/* r.x.flags */
	movw	$0, 0x2e(%edi)	/* r.x.sp */
	movw	$0, 0x30(%edi)	/* r.x.ss */
	
L_not_busy:
	DPMI(0x0300)
	xorl	%eax,%eax

	LEAVEP(decb dpmi_busy; popl %ss; popl %es)
