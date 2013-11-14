/* LWP 2.0  */
#ifndef _LWP_
#define _LWP_
#ifndef FALSE
#define FALSE 0
#define TRUE !FALSE
#endif
#define DEBUG

/* for programming IRQ8 */

#define RTC2		0xF	
#define RTC4		0xE	
#define RTC8		0xD
#define RTC16		0xC
#define RTC32		0xB
#define RTC64		0xA
#define RTC128		0x9
#define RTC256		0x8
#define RTC512		0x7
#define RTC1024		0x6
#define RTC2048		0x5
#define RTC4096		0x4
#define RTC8192		0x3


/* User functions */

#ifdef __cplusplus
extern "C" {
#endif

int lwpInit(int speed, int priority);
int lwpSpawn(void (*proc)(void *), void *param, int stackLength, int priority, int active);
int lwpKill(volatile int lwpid);
void lwpYield();

int lwpAdjustThreadPriority(int lwpid, int priority);
int lwpGetThreadPriority(int lwpid);
int lwpThreadSuspend(int lwpid);
int lwpThreadResume(int lwpid);

int lwpGetpid(void);
int lwpThreadCount(void);


int lwpCreateSemaphore(void *lockaddr, int count);
int lwpDeleteSemaphore(void *lockaddr);

int lwpGetSemaphoreCount(void *lockaddr);
int lwpAdjustSemaphoreCount(void *lockaddr, int count);

int lwpLockSemaphore(void *lockaddr);
int lwpReleaseSempahore(void *lockaddr);

int lwpCreateMutex(void *lockaddr);
int lwpDeleteMutex(void *lockaddr);

int lwpLockMutex(void *lockaddr);
int lwpReleaseMutex(void *lockaddr);


void lwpEnterCriticalSection(void);
void lwpLeaveCriticalSection(void);


#ifdef __cplusplus
}
#endif


#endif   /* _LWP_ */
