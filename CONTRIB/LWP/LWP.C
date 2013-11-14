/* lwp 2.0 */
#include "lwp.h"
#include <crt0.h>
#include <dpmi.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/exceptn.h>
#include <string.h>
#include <sys/segments.h>
#include <string.h>
#include <bios.h>
#include <stdlib.h>     
#include <stdio.h>

typedef struct lwpStruct {
	    int lwpid;			/* Process ID 			*/	
	 struct lwpStruct *next;	/* Pointer to next struct 	*/
	    int stklen;			/* Size of stack for this lwp	*/
  unsigned long *stack;			/* Pointer to the stack		*/
	    int priority;		/* Priority of LWP		*/
	    int pcount;			/* Used by scheduler		*/
	    int active;			/* Thread suspend 		*/
} lwp;

typedef struct semaphoreStruct {
	int owner;			/* Process ID that created it	*/
	int lwpidLock;			/* Pid that did the lock	*/
	int lock;			/* Lock value			*/
	int countMax;			/* Number of threads allowed	*/
	int count;			/* Number of threads using	*/
	void *lockaddr;			/* Address of locked func/var 	*/
	struct semaphoreStruct *next;	/* Pointer to next struct	*/
} lwpSemaphore;


#define LWP_EXCPTN 0x99     		/* arbitrary exception number 	*/

#define IRQ8 0x70         		/* Interrupt # for IRQ8		*/
#define IRQ0 0x8          		/* Interrupt # for IRQ0		*/


#define LWP_MAIN -1			/* Main thread's PID		*/


static int _lwpOn = 0;			/* Used to keep track for exit	*/
static int _lwpSsSel;			/* to fix __dpmi_int under 95   */
					/* It won't be needed when it's */
					/* fixed in the libc source 	*/
					  
extern __dpmi_paddr 
	_lwpOldIrq0Handler, 		/* Used to keep track for exit	*/
	_lwpOldIrq8Handler;

char _lwpFpuState[108];			/* Default FPU state for thread */
int _lwpFlags;				/* Default eflags for lwp	*/


void (*_lwpOldHandler)(int);		/* Old SIGILL handler 		*/
void (*_lwpOldFpuHandler)(int);		/* Old SIGFPE handler		*/

lwp *_lwpCur;				/* The running thread's struct 	*/
lwpSemaphore *_lwpSemaphoreList;	/* Head of semaphore List	*/

volatile int _lwpCount;			/* Number of running threads 	*/
static int _lwpPidCount;		/* Serial number for PID's 	*/
volatile int _lwpEnable;		/* enables task switches if 0   */
extern long  _lwpasmStart;		/* Used to lock asm code	*/
extern long  _lwpasmEnd;		/* Used to lock asm code	*/


/* Some function prototypes */

extern void _lwpPmIrq8TimerHook(void);	/* Interrupt handler	*/
extern void _lwpPmIrq0TimerHook(void);	/* Interrupt handler	*/

static int _lwpLockData( void *lockaddr, unsigned long locksize );
static int _lwpLockCode( void *lockaddr, unsigned long locksize );

extern void _lwpScheduler(int signum);		/* SIGILL Handler	*/
extern void _lwpInitFpu(void);			/* Init's _fpu_state	*/
static void _lwpFpuHandler(int signum);		/* SIGFPE handler	*/
static void lwpDeinit(void);			/* At_exit destructor	*/
static int _lwpLockMemory();			/* Locks all code/data	*/
static void setRTC(int value);			/* Sets speed of RTC	*/
static void startIRQ8(void);			/* Starts RTC going	*/
static void stopIRQ8(void);			/* Stops RTC going	*/
extern int _lwpInitFlags();			/* Gets default flags 	*/
extern void _lwpDeadYield();			/* jik function returns	*/


/* Implementation */


int lwpInit(int speed, int priority)
{ 
	__dpmi_paddr irq0, irq8;
	
	if(priority == 0)
		{
		#ifdef DEBUG
		printf("(lwpInit) Error! Priority is zero\n");
		#endif
		return(FALSE);
		}

	if(atexit(lwpDeinit))
		{
		#ifdef DEBUG
		printf("(lwpInit) ERROR: Couldn't install atexit routine.\n");
		#endif
		return(FALSE);
		}

   	if (!_lwpLockMemory())
		{
		#ifdef DEBUG
		printf("(lwpInit) ERROR: Couldn't lock code/data.\n");
		#endif
		return(FALSE);
      		}

	if((_lwpCur = (lwp *) malloc(sizeof(lwp))) == NULL)
		{
		#ifdef DEBUG
		printf("(lwpInit) ERROR:  Couldn't malloc _lwp_Cur\n");
		#endif
		return(FALSE);
      		}
	

	_lwpInitFpu();
	_lwpFlags = _lwpInitFlags();
	_lwpCur->stack = NULL;	
	_lwpCur->lwpid = LWP_MAIN;
	_lwpCur->next = _lwpCur;
	_lwpCur->priority = priority;
	_lwpCur->pcount = 1;
	_lwpCur->active = TRUE;
	_lwpEnable = FALSE;
	_lwpCount = 0;
	_lwpPidCount = 0;

	_lwpSemaphoreList = NULL;	

	/* SIGILL is raised when an unhandled exception is raised */
	_lwpOldHandler = signal(SIGILL,_lwpScheduler); 
	_lwpOldFpuHandler = signal(SIGFPE, _lwpFpuHandler);

	irq8.offset32 = (int)_lwpPmIrq8TimerHook;
	irq8.selector = _my_cs();
	__dpmi_get_protected_mode_interrupt_vector(IRQ8, &_lwpOldIrq8Handler);
	__dpmi_set_protected_mode_interrupt_vector(IRQ8, &irq8);

	startIRQ8(); 
	setRTC(speed); 

	irq0.offset32 = (int)_lwpPmIrq0TimerHook;
	irq0.selector = (int)_my_cs();
	__dpmi_get_protected_mode_interrupt_vector(IRQ0, &_lwpOldIrq0Handler);
	__dpmi_set_protected_mode_interrupt_vector(IRQ0, &irq0);

	_lwpOn = TRUE;
	_lwpEnable = TRUE;
	return(TRUE);
}

int lwpGetpid(void)
{  
	return(_lwpCur->lwpid);
}

int lwpThreadCount(void)
{
	return(_lwpCount);
}

int lwpGetThreadPriority(int lwpid)
{
	lwp *tmpLwpPtr;
	int i = 0;
	tmpLwpPtr = _lwpCur;

	while((i<_lwpCount) && (tmpLwpPtr->lwpid != lwpid))
		{
		i++;
		tmpLwpPtr = tmpLwpPtr->next;
		}
	if(tmpLwpPtr->lwpid != lwpid)
		{
		#ifdef DEBUG
		printf("(lwpAdjustThreadPriority) Error!  PID not found\n");
		#endif
		return(FALSE);
		}
	return(tmpLwpPtr->priority);
}

int lwpAdjustThreadPriority(int lwpid, int priority)
{
	lwp *tmpLwpPtr;
	int i = 0;

	tmpLwpPtr = _lwpCur;
	while((i<_lwpCount) && (tmpLwpPtr->lwpid != lwpid))
		{
		i++;
		tmpLwpPtr = tmpLwpPtr->next;
		}
	if(tmpLwpPtr->lwpid != lwpid)
		{
		#ifdef DEBUG
		printf("(lwpAdjustThreadPriority) Error!  PID not found\n");
		#endif
		return(FALSE);
		}
	tmpLwpPtr->priority = priority;
	return(TRUE);

}

/* spawns a light weight process.  Returns the lwpid of the proc or 0 on fail*/

int lwpSpawn(void (*proc)(void *), void *param, int stackLength, int priority, int active)
{
	lwp *tmpLwpPtr;
	int tmpCriticalSection;
	tmpCriticalSection = _lwpEnable;
	_lwpEnable = FALSE;
	if((proc == NULL) || (stackLength < 256))
		{
		#ifdef DEBUG
		printf("(lwpSpawn) Error!  Attempt to spawn with a stack < 256 bytes\n");
		#endif
        	_lwpEnable = tmpCriticalSection;
      		return(FALSE);
     		}
	if((tmpLwpPtr = (lwp *) malloc(sizeof(lwp))) == NULL)
		{
		#ifdef DEBUG
		printf("(lwpSpawn) Error!  Couldn't allocate memory in lwpSpawn\n");
		#endif
		_lwpEnable = tmpCriticalSection;
		return(FALSE);
		}

	if((stackLength % 2) != 0) 
		{
		stackLength++; /* make the stack even */
		}	

	if((stackLength %4) != 0) 
		{
		stackLength+=2; /* make the stack divisible by 4 */
		}
	
	if((tmpLwpPtr->stack = (unsigned long *) malloc(stackLength)) == NULL)
		{
		#ifdef DEBUG
		printf("(lwpSpawn) Error!  Couldn't allocate memory in lwpSpawn\n");
		#endif
		_lwpEnable = tmpCriticalSection;
		return(FALSE);
		}

	if(priority == 0) /* infinite priority! */
		{
		#ifdef DEBUG
		printf("(lwpSpawn) Error! Attempted to spawn with a priority of zero.\n"); 
		#endif
		_lwpEnable = tmpCriticalSection;
		return(FALSE);
		}

	 /* 8 regs +flags */

	tmpLwpPtr->stack[(stackLength/4) - 1] = (long) param;
	tmpLwpPtr->stack[(stackLength/4) - 2] = (long) _lwpDeadYield;
	tmpLwpPtr->stack[(stackLength/4) - 3] = (long) proc;
	tmpLwpPtr->stack[(stackLength/4) - 4] = 0; /* eax */
	tmpLwpPtr->stack[(stackLength/4) - 5] = 0; /* ebx */
	tmpLwpPtr->stack[(stackLength/4) - 6] = 0; /* ecx */ 
	tmpLwpPtr->stack[(stackLength/4) - 7] = 0; /* edx */
	tmpLwpPtr->stack[(stackLength/4) - 8] = 0; /* ebp */
	tmpLwpPtr->stack[(stackLength/4) - 9] = 0; /* esp, discarded by popal */
	tmpLwpPtr->stack[(stackLength/4) -10 ] = 0; /* esi */
	tmpLwpPtr->stack[(stackLength/4) -11] = 0; /* edi */
	tmpLwpPtr->stack[(stackLength/4) -12] = (long) _my_ds();  /* ds */
	tmpLwpPtr->stack[(stackLength/4) -13] = (long) _my_ds();  /* es */
	tmpLwpPtr->stack[(stackLength/4) -14] = (long) _my_ds();  /* fs */
	tmpLwpPtr->stack[(stackLength/4) -15] = (long) _my_ds();  /* gs */
	tmpLwpPtr->stack[(stackLength/4) -16] = (long) _lwpFlags; /* flags */

	/* copy initial FPU state to stack */
	memcpy(tmpLwpPtr->stack + (stackLength/4) - 43, _lwpFpuState, 108);
	tmpLwpPtr->stack = tmpLwpPtr->stack + (stackLength/4) - 43;
	tmpLwpPtr->priority = priority;
	tmpLwpPtr->pcount = priority;
	tmpLwpPtr->active = active;
	tmpLwpPtr->lwpid = ++_lwpPidCount; 
	tmpLwpPtr->next = _lwpCur->next;
	_lwpCur->next = tmpLwpPtr; 
	_lwpCount++;
	_lwpEnable = tmpCriticalSection;
	return(tmpLwpPtr->lwpid);
}


/* Kills a lwp (takes it out of the linked list of lwp's) , or 0 if it fails */

int lwpKill(int lwpid)
{
	int i = 0, tmpCriticalSection;
	lwp *tmpLwpPtr1,*tmpLwpPtr2;
	tmpCriticalSection = _lwpEnable;
	_lwpEnable = FALSE;
	tmpLwpPtr1 = _lwpCur->next;
	tmpLwpPtr2 = _lwpCur;

	if(lwpid == LWP_MAIN)
		{
		#ifdef DEBUG
		printf("(lwpKill) Error! Attempted to kill main()!\n");
		#endif
		_lwpEnable = tmpCriticalSection;
		return(FALSE);
		}
	if(lwpid == 0)
		{
		/* No such thing as pid 0.  Pid's start at 1 */
		#ifdef DEBUG
		printf("(lwpKill) Error!  Attempted to kill pid 0.\n");
		#endif
		_lwpEnable = tmpCriticalSection;
		return(FALSE);
		}

	/* special case for the current thread */
	if(lwpid == _lwpCur->lwpid)
		{
		_lwpDeadYield();
		printf("(lwpKill) PANIC!\n"); /* should never get here after the yield */
		exit(-1);
		}

	if((tmpLwpPtr1 == NULL) || (tmpLwpPtr2 == NULL))
		{
		#ifdef DEBUG
		printf("(lwpKill) Error!  LWP list head is NULL!\n");
		#endif
		_lwpEnable = tmpCriticalSection;
		return(FALSE);
		}
  	while((i< _lwpCount+1) && (tmpLwpPtr1->lwpid != lwpid))
		{
		i++;
		tmpLwpPtr1 = tmpLwpPtr1->next;
		tmpLwpPtr2 = tmpLwpPtr2->next;
		}
	/* went through all of them and wasn't found */
	if(tmpLwpPtr1->lwpid != lwpid) 
		{
		#ifdef DEBUG
		printf("(lwpKill) Error!  PID not found\n");
		#endif
		_lwpEnable = tmpCriticalSection;
		return(FALSE);
		}

	/* now tmp IS the right one to kill */
	tmpLwpPtr2->next = tmpLwpPtr1->next;
	free(tmpLwpPtr1->stack);
	free(tmpLwpPtr1);
	_lwpCount--;
	_lwpEnable = tmpCriticalSection;
	return(TRUE);
}

int lwpThreadSuspend(int lwpid)
{
	int i = 0;
	lwp *tmpLwpPtr;
	tmpLwpPtr = _lwpCur;
	if(lwpid == LWP_MAIN)
		{
		#ifdef DEBUG
		printf("Tried to suspend the main thread!\n");	
		#endif
		return(FALSE);
		}
	while((i<_lwpCount+1) && (tmpLwpPtr->lwpid != lwpid))
		{
		i++;
		tmpLwpPtr = tmpLwpPtr->next;
		}
	if(tmpLwpPtr->lwpid != lwpid)
		{
		#ifdef DEBUG
		printf("(lwpThreadSuspend) Error!  PID not found\n");
		#endif
		return(FALSE);
		}
	tmpLwpPtr->active = FALSE;
	return(TRUE);
}

int lwpThreadResume(int lwpid)
{
	int i = 0;
	lwp *tmpLwpPtr;
	tmpLwpPtr = _lwpCur;
	while((i<_lwpCount+1) && (tmpLwpPtr->lwpid != lwpid))
		{
		i++;
		tmpLwpPtr = tmpLwpPtr->next;
		}
	if(tmpLwpPtr->lwpid != lwpid)
		{
		#ifdef DEBUG
		printf("(lwpThreadResume) Error!  PID not found\n");
		#endif
		return(FALSE);
		}
	tmpLwpPtr->active = TRUE;
	return(TRUE);
}


int lwpCreateSemaphore(void *lockaddr, int count)
{
	lwpSemaphore *tmpLwpSemaphore;
	int tmpCriticalSection;


	tmpCriticalSection = _lwpEnable;
	_lwpEnable = FALSE;

	if(_lwpSemaphoreList == NULL)
		{
		if((tmpLwpSemaphore = (lwpSemaphore *) malloc(sizeof(struct semaphoreStruct))) == NULL)
			{
			#ifdef DEBUG
			printf("(lwpCreateSemaphore) Error!  Couldn't allocate memory\n");
			#endif	
			return(FALSE);
			}
		tmpLwpSemaphore->lockaddr = lockaddr;
		tmpLwpSemaphore->countMax = count;
		tmpLwpSemaphore->next = NULL;
		tmpLwpSemaphore->lock = FALSE;
		tmpLwpSemaphore->lwpidLock = 0;
		tmpLwpSemaphore->owner = lwpGetpid();
		tmpLwpSemaphore->count = 0;

		_lwpSemaphoreList = tmpLwpSemaphore;

		_lwpEnable = tmpCriticalSection;

		return(TRUE);
		}

	/* Look through the list and see if it's already been created */
	tmpLwpSemaphore = _lwpSemaphoreList;
	while(tmpLwpSemaphore->lockaddr != lockaddr)
		{
		tmpLwpSemaphore = tmpLwpSemaphore->next;
		if(tmpLwpSemaphore == NULL)
			{
			/* If we've gotten this far, it hasn't been created */
			if((tmpLwpSemaphore = (lwpSemaphore *) malloc(sizeof(struct semaphoreStruct))) == NULL)
				{
				#ifdef DEBUG
				printf("(lwpCreateSemaphore) Error!  Couldn't allocate memory\n");
				#endif
				return(FALSE);
				}

			tmpLwpSemaphore->lockaddr = lockaddr;
			tmpLwpSemaphore->countMax = count;
			tmpLwpSemaphore->count = 0;
			tmpLwpSemaphore->lock = FALSE;
			tmpLwpSemaphore->lwpidLock = 0;
			tmpLwpSemaphore->owner = lwpGetpid();
			tmpLwpSemaphore->next = _lwpSemaphoreList->next;
			
			_lwpSemaphoreList->next = tmpLwpSemaphore;
			_lwpEnable = tmpCriticalSection;
			return(TRUE);
			}
		}

	#ifdef DEBUG
	printf("(lwpCreateSemaphore) Error!  Semaphore already created\n");
	#endif
	_lwpEnable = tmpCriticalSection;
	return(FALSE);
}

int lwpDeleteSemaphore(void *lockaddr)
{
	lwp *tmpLwpPtr;
	lwpSemaphore *tmpLwpSemaphore, *tmpLwpSemaphorePrev;
	int tmpCriticalSection;
	int i;
	
	if(_lwpSemaphoreList == NULL)
		{
		#ifdef DEBUG
		printf("(lwpDeleteSemaphore) Error!  Semaphore List is NULL\n");
		#endif
		return(FALSE);
		}
	
	tmpCriticalSection = _lwpEnable;
	_lwpEnable = FALSE;

	tmpLwpSemaphore = _lwpSemaphoreList;	
	tmpLwpSemaphorePrev = _lwpSemaphoreList;
	while(tmpLwpSemaphore->lockaddr != lockaddr)
		{
		tmpLwpSemaphorePrev = tmpLwpSemaphore;
		tmpLwpSemaphore = tmpLwpSemaphore->next;
		if(tmpLwpSemaphore == NULL)
			{
			#ifdef DEBUG
			printf("(lwpDeleteSemaphore) Error! Semaphore does not exist\n");	
			#endif
			_lwpEnable = tmpCriticalSection;
			return(FALSE);
			}
		}
	

	/* If the semaphore owner doesn't exist, we can delete it */
	tmpLwpPtr = _lwpCur;
	i = 0;
	while((i < _lwpCount+1) && (tmpLwpPtr->lwpid != tmpLwpSemaphore->owner))
		{
		tmpLwpPtr = tmpLwpPtr->next;
		i++;
		}
	if(i == _lwpCount+1)
		{
		/* Owner doesn't exist, so safe to delete */
		tmpLwpSemaphorePrev->next = tmpLwpSemaphore->next;
		free(tmpLwpSemaphore);
		_lwpEnable = tmpCriticalSection;	
		return(TRUE);
		}
	
	if(tmpLwpSemaphore->owner != lwpGetpid())
		{
		#ifdef DEBUG
		printf("(lwpDeleteSemaphore) Error! Thread tried to delete a semaphore it did not create\n");
		#endif
		_lwpEnable = tmpCriticalSection;
		return(FALSE);
		}

	tmpLwpSemaphorePrev->next = tmpLwpSemaphore->next;
	free(tmpLwpSemaphore);
	_lwpEnable = tmpCriticalSection;	
	return(TRUE);
}

int lwpAdjustSemaphoreCount(void *lockaddr, int count)
{
	lwpSemaphore *tmpLwpSemaphore;
	int tmpCriticalSection;

	tmpCriticalSection = _lwpEnable;
	_lwpEnable = FALSE;

	tmpLwpSemaphore = _lwpSemaphoreList;

	if(tmpLwpSemaphore == NULL)
		{
		#ifdef DEBUG
		printf("(lwpAdjustSemaphoreCount) Error!  Semaphore list is NULL\n");	
		#endif
		_lwpEnable = tmpCriticalSection;
		return(FALSE);
		}

	while(tmpLwpSemaphore->lockaddr != lockaddr)
		{
		tmpLwpSemaphore = tmpLwpSemaphore->next;
		if(tmpLwpSemaphore == NULL)
			{
			#ifdef DEBUG
			printf("(lwpAdjustSemaphoreCount) Error! Semaphore does not exist\n");
			#endif
			_lwpEnable = tmpCriticalSection;
			return(FALSE);
			}
		}

	tmpLwpSemaphore->count = count;	

	_lwpEnable = tmpCriticalSection;

	return(TRUE);

}

int lwpGetSemaphoreCount(void *lockaddr)
{
	lwpSemaphore *tmpLwpSemaphore;
	int tmpCriticalSection;

	tmpCriticalSection = _lwpEnable;
	_lwpEnable = FALSE;

	tmpLwpSemaphore = _lwpSemaphoreList;

	if(tmpLwpSemaphore == NULL)
		{
		#ifdef DEBUG
		printf("(lwpGetSemaphoreCount) Error!  Semaphore list is NULL\n");	
		#endif
		_lwpEnable = tmpCriticalSection;
		return(FALSE);
		}

	while(tmpLwpSemaphore->lockaddr != lockaddr)
		{
		tmpLwpSemaphore = tmpLwpSemaphore->next;
		if(tmpLwpSemaphore == NULL)
			{
			#ifdef DEBUG
			printf("(lwpGetSemaphoreCount) Error! Semaphore does not exist\n");
			#endif
			_lwpEnable = tmpCriticalSection;
			return(FALSE);
			}
		}


	_lwpEnable = tmpCriticalSection;
	return(tmpLwpSemaphore->count);

}
int lwpLockSemaphore(void *lockaddr)
{
	lwpSemaphore *tmpLwpSemaphore;
	lwp *tmpLwpPtr;
	int tmpCriticalSection;
	int i;

	tmpCriticalSection = _lwpEnable;
	_lwpEnable = FALSE;
	
	tmpLwpSemaphore = _lwpSemaphoreList;
	
	if(tmpLwpSemaphore == NULL)
		{
		#ifdef DEBUG
		printf("(lwpLockSemaphore) Error!  Semaphore List is NULL\n");
		#endif
		_lwpEnable = tmpCriticalSection;
		return(FALSE);
		}
	while(tmpLwpSemaphore->lockaddr != lockaddr)
		{
		tmpLwpSemaphore = tmpLwpSemaphore->next;
		if(tmpLwpSemaphore == NULL)
			{
			#ifdef DEBUG
			printf("lwpLockSemaphore) Error!  Semaphore does not exist\n");
			#endif
			_lwpEnable = tmpCriticalSection;
			return(FALSE);
			}
		}

	if(tmpLwpSemaphore->count < tmpLwpSemaphore->countMax)
		{
		tmpLwpSemaphore->lock = TRUE;
		tmpLwpSemaphore->count++;
		tmpLwpSemaphore->lwpidLock = lwpGetpid();
		_lwpEnable = tmpCriticalSection;
		return(TRUE);
		}
	

	/* The tricky part.  Since the semaphore is already locked, we
	 * have to wait for it to unlock.  But we also have to check to
	 * see if the thread that created it still exists, to avoid
	 * deadlocks.  
	 * 
	 * Known bug:  When yielding to the other threads to allow the
	 * semaphore to get unlocked, the other threads might call
	 * lwpDeleteSemaphore on this semaphore!  Not good!
	 * 
	 */	
	while(tmpLwpSemaphore->lock == TRUE)
		{
		/* Make sure thread still exists that did the lock */
		i = 0;
		tmpLwpPtr = _lwpCur;
		while((i < _lwpCount+1) && (tmpLwpPtr->lwpid != tmpLwpSemaphore->lwpidLock))
			{
			tmpLwpPtr = tmpLwpPtr->next;
			i++;
			}

		/* Thread didn't exist, so we can lock it */
		if(i == _lwpCount+1)
			{

			tmpLwpSemaphore->count++;
			tmpLwpSemaphore->lwpidLock = lwpGetpid();
			tmpLwpSemaphore->lock = TRUE;
			_lwpEnable = tmpCriticalSection;
			return(TRUE);
			}	

		_lwpEnable = tmpCriticalSection;
		lwpYield();
		tmpCriticalSection = _lwpEnable;
		_lwpEnable = FALSE;
		}

	if(tmpLwpSemaphore->count >= tmpLwpSemaphore->countMax)
		{
		#ifdef DEBUG
		printf("(lwpLockSemaphore) Error!  Lock limit exceeded\n");
		#endif
		_lwpEnable = tmpCriticalSection;
		return(FALSE);
		}


	tmpLwpSemaphore->lock = TRUE;
	tmpLwpSemaphore->count++;
	tmpLwpSemaphore->lwpidLock = lwpGetpid();

	_lwpEnable = tmpCriticalSection;
	return(TRUE);
}
int lwpReleaseSemaphore(void *lockaddr)
{
	lwpSemaphore *tmpLwpSemaphore;
	int tmpCriticalSection;
	
	tmpCriticalSection = _lwpEnable;
	_lwpEnable = FALSE;
	
	tmpLwpSemaphore = _lwpSemaphoreList;
	if(tmpLwpSemaphore == NULL)
		{
		#ifdef DEBUG
		printf("(lwpReleaseSemaphore) Error!  Semaphore list is NULL\n");
		#endif 
		_lwpEnable = tmpCriticalSection;
		return(FALSE);
		}
	while(tmpLwpSemaphore->lockaddr != lockaddr)
		{
		tmpLwpSemaphore = tmpLwpSemaphore->next;
		if(tmpLwpSemaphore == NULL)
			{
			#ifdef DEBUG
			printf("(lwpReleaseSemaphore) Error!  Semaphore does not exist\n");
			#endif
			_lwpEnable = tmpCriticalSection;
			return(FALSE);
			}
		}
	
	tmpLwpSemaphore->lock = FALSE;
	tmpLwpSemaphore->lwpidLock = 0;
	tmpLwpSemaphore->count--;
	if(tmpLwpSemaphore->count < 0)
		{
		tmpLwpSemaphore->count = 0;
		}
	_lwpEnable = tmpCriticalSection;
	return(TRUE);
}



int lwpCreateMutex(void *lockaddr)
{
	return(lwpCreateSemaphore(lockaddr, 1));
}

int lwpDeleteMutex(void *lockaddr)
{
	return(lwpDeleteSemaphore(lockaddr));
}

int lwpLockMutex(void *lockaddr)
{
	return(lwpLockSemaphore(lockaddr));
}

int lwpReleaseMutex(void *lockaddr)
{
	return(lwpReleaseSemaphore(lockaddr));
}



static void lwpDeinit(void)
{
	_lwpEnable = FALSE;
	if(!_lwpOn)
		{
		#ifdef DEBUG
		printf("(lwpDeinit) Error!  lwpInit not called\n");
		#endif
		return;
		}
	
	__dpmi_set_protected_mode_interrupt_vector(IRQ0, &_lwpOldIrq0Handler);
	stopIRQ8();
	__dpmi_set_protected_mode_interrupt_vector(IRQ8, &_lwpOldIrq8Handler);

	signal(SIGILL, _lwpOldHandler);
	signal(SIGFPE, _lwpOldFpuHandler);
	free(_lwpCur);
	__dpmi_free_dos_memory(_lwpSsSel);
	return;
}


/* Set RTC dividen */
static void setRTC(int value)
{
	
	unsigned char tmp;	
	disable();	
	outportb(0x70, 0x8A);
	tmp = inportb(0x71);
	outportb(0x70, 0x8A);
	outportb(0x71, (tmp & 0xF0) | (value & 0x0F)); 
	enable();

}

/* Enable RTC periodic interrupt */
static void startIRQ8(void)
{
	unsigned char tmp;
	disable();
	outportb(0x70, 0x8B);	
	tmp = inportb(0x71) | 0x40;
	outportb(0x70, 0x8B);	
	outportb(0x71, tmp);
	outportb(0x70, 0x8C);
 	inportb(0x71);
	enable();
}

/* Disable RTC periodic interrupt */
static void stopIRQ8(void)
{

	unsigned char tmp;
	disable();
	outportb(0x70, 0x8B);	
	tmp = inportb(0x71) & 0xBF;
	outportb(0x70, 0x8B);	
	outportb(0x71, tmp);
	outportb(0x70, 0x8C);	
	inportb(0x71);
	enable();

}

static int _lwpLockMemory()
{
	if(!_lwpLockCode(&_lwpasmStart, (long) &_lwpasmEnd - (long) _lwpasmStart))
		{
		return(FALSE);
		}
	
	if(!_lwpLockData((void *) &_lwpEnable, 4))
		{
		return(FALSE);
		}

	return(TRUE); 
}


static void _lwpFpuHandler(int signum)
{
	int tmp = _lwpEnable;
	_lwpEnable = FALSE;
	_lwpOldFpuHandler(signum);
	_lwpEnable = tmp;
}



/* copied from go32 lib funcs */
static int _lwpLockData( void *lockaddr, unsigned long locksize )
{
	unsigned long baseaddr;
	__dpmi_meminfo memregion;


	if( __dpmi_get_segment_base_address( _my_ds(), &baseaddr) == -1 ) 
		{
		return(FALSE);
		}

	memset( &memregion, 0, sizeof(memregion) );
	memregion.address = baseaddr + (unsigned long) lockaddr;
	memregion.size = locksize;

	if( __dpmi_lock_linear_region( &memregion ) == -1 ) 
		{
		return(FALSE);
		}
	return(TRUE);
}


static int _lwpLockCode( void *lockaddr, unsigned long locksize )
{
	unsigned long baseaddr;
	__dpmi_meminfo memregion;
	if( __dpmi_get_segment_base_address( _my_cs(), &baseaddr) == -1 ) 
		{
		return(FALSE);
		}
	memset( &memregion, 0, sizeof(memregion) );
	memregion.address = baseaddr + (unsigned long) lockaddr;
	memregion.size = locksize;

	if( __dpmi_lock_linear_region( &memregion ) == -1 ) 
		{
		return(FALSE);
		}
	return(TRUE);
}
