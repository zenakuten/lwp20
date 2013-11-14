/* lwp 2.0 */
/* ASM code for the lwp package.  This is where the nitty gritty shtuff is */

.globl __lwpPmIrq8TimerHook
.globl __lwpPmIrq0TimerHook
.globl __lwpasmStart
.globl __lwpasmEnd
.globl _lwpYield
.globl __lwpScheduler
.globl _lwpEnterCriticalSection
.globl _lwpLeaveCriticalSection
.globl __lwpInitFpu
.globl __lwpInitFlags
.globl __lwpDeadYield
.globl __lwpOldIrq0Handler
.globl __lwpOldIrq8Handler

.align 4
.text
__lwpasmStart:

lwpInterruptPending: .long 0

tmp1: .long 0 
tmp2: .long 0
tmp3: .long 0

__lwpOldIrq0Handler: .long 0, 0    /* __dpmi_paddr */
__lwpOldIrq8Handler: .long 0, 0    /* __dpmi_paddr */

__lwpPmIrq8TimerHook: 
	pushl %eax
	pushl %ds
	
	.byte 0x2E   /* CS: */
        movw ___djgpp_ds_alias, %ds 

	cmp $0, lwpInterruptPending
	jne L2

	movl $1, lwpInterruptPending

	cmp $1, __lwpEnable
	jne  L1
	movb $0x99,%al 
        call ___djgpp_hw_exception
	jmp L2

L1:  	movl $0, lwpInterruptPending 
L2: 	popl %ds
	popl %eax
	ljmp %cs:__lwpOldIrq8Handler


__lwpPmIrq0TimerHook: 
	pushl %eax
	pushl %ebx
	pushl %ds
	.byte 0x2E  /* CS: */
        movw ___djgpp_ds_alias, %ds 
	
	movl $0x8B, %eax
	outb %al, $0x70
	inb  $0x71, %al
	movl %eax, %ebx
	andl $0x40, %eax
	jnz skip
	orl  $0x40, %ebx
	movl $0x8B, %eax
	outb %al, $0x70
	movl %ebx, %eax
	outb %al, $0x71

skip:	popl %ds
	popl %ebx
	popl %eax
	ljmp %cs:__lwpOldIrq0Handler

.align 4
__lwpasmEnd:                 

.align 4
__lwpScheduler:

	movl $1, lwpInterruptPending

	movl  ___djgpp_exception_state_ptr, %edi
	cmpl $0, %edi		/* SIGILL was raise()'d, not by interrupt */
	je raised               
	cmpl $0x99, 56(%edi)    /* our exception or real exception? */
	jne raised

	movl 0(%edi), %eax
	pushl %eax
	movl 4(%edi), %ebx
	movl 8(%edi), %ecx
	movl 12(%edi), %edx
	movl 16(%edi), %esi
	movl 24(%edi), %ebp
	
	movl 28(%edi), %eax
	movl %eax, tmp1		/* esp */
	movl 32(%edi), %eax
	movl %eax, tmp2		/* eip */
	movl 36(%edi), %eax
	movl %eax, tmp3		/* eflags */
	movl 20(%edi), %eax	/* edi */

	movw 44(%edi), %es
	movw 46(%edi), %fs
	movw 48(%edi), %gs
	movw 42(%edi), %ds

	movl %eax, %edi
	popl %eax
	.byte 0x2e
	movl tmp1, %esp       /* stack switched to exception state stack */
	.byte 0x2e
	pushl tmp2            /* push exeception state ret address */
	.byte 0x2e
	pushl tmp3   /* eflags */
	popfl

	/* now it's setup to look just like a yield happened in the code  */
	/* This little bit is what does the priorities */
	pushl %eax
	pushl %esi
	pushl %ds
	pushfl
	.byte 0x2e
	movw ___djgpp_ds_alias, %ds
	movl __lwpCur, %esi
	decl 20(%esi)			/* pcount */
	jnz noYld			/* pcount == 0 ? */
	movl 16(%esi), %eax		/* yes, pcount = priority */
	movl %eax, 20(%esi)		
	popfl
	popl %ds
	popl %esi
	popl %eax
	jmpl _lwpYield
noYld:	popfl
	popl %ds
	popl %esi
	popl %eax
raised:	pushl %ds
	.byte 0x2e
	movw ___djgpp_ds_alias, %ds	
	movl $0, lwpInterruptPending 
	popl %ds
	ret


.align 4
_lwpYield:
	pushl %ds
	.byte 0x2e
	movw ___djgpp_ds_alias, %ds
	movl $1, lwpInterruptPending
	popl %ds
	pushal
	pushl %ds 
	pushl %es
	pushl %fs
	pushl %gs
	pushfl

	subl $108, %esp 
	fwait
	fnsave (%esp)   /* FPU pushal ;) */
	fwait

	.byte 0x2e
	movw ___djgpp_ds_alias, %ds
	movl __lwpCur, %esi
	movl %esp, 12(%esi)	

yl1:	movl 4(%esi), %esi  /* Find next ACTIVE thread */
	cmpl $0, 24(%esi)
	je yl1

	movl 12(%esi), %esp
	movl %esi, __lwpCur	

	fwait 
	frstor (%esp)   /* FPU popal ;) */
	fwait
	addl $108, %esp

	popfl
	popl %gs
	popl %fs
	popl %es
	popl %ds 
	popal

	pushl %ds
	.byte 0x2e
	movw ___djgpp_ds_alias, %ds
	movl $0, lwpInterruptPending
	popl %ds

	ret


__lwpDeadYield:
	.byte 0x2e
	movw ___djgpp_ds_alias, %ds
	movl $1, lwpInterruptPending
	movl $1, __lwpEnable
	decl __lwpCount
	movl __lwpCur, %esi
	movl %esi, %edi		/* save for later */
	movl %esi, %ebx		/* save for later */
	movl %esi, %edx		/* lwp * to free */
	movl 12(%esi), %eax	/* lwp->stack to free */


ldy1:	movl %ebx, %ecx	
	movl 4(%ebx), %ebx
	cmpl %ebx, %edi
	jne ldy1

	/* now %ecx = thread before _lwpCur */
	movl 4(%esi), %edi
	movl %edi, 4(%ecx)   /* ecx->next = esi->next */

ldy2:	movl 4(%esi), %esi
	cmpl $0, 24(%esi)    /* find next ACTIVE thread */
	je ldy2
	

	movl 12(%esi), %esp
	movl %esi, __lwpCur 

	pushl %eax
	call _free      
	addl $4, %esp

	pushl %edx
	call _free
	addl $4, %esp

	fwait 
	frstor (%esp)   /* FPU popal ;) */
	fwait
	addl $108, %esp


	popfl
	popl %gs
	popl %fs
	popl %es
	popl %ds
	popal
	pushl %ds
	.byte 0x2e
	movw ___djgpp_ds_alias, %ds
	movl $0, lwpInterruptPending
	popl %ds
	ret

__lwpInitFpu:
	pushl %eax
	movl $__lwpFpuState, %eax
	finit 
	fwait
	fnsave (%eax)
	fwait
	popl %eax
	ret

__lwpInitFlags:
	pushfl
	popl %eax
	ret

_lwpEnterCriticalSection:	
	movl $0, __lwpEnable
	ret

_lwpLeaveCriticalSection:
	movl $1, __lwpEnable
	ret


