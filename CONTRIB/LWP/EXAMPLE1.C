#include "lwp.h"
#include <stdio.h>
#include <dpmi.h>
#include <dos.h>
#include <conio.h>
#define MAX_PROC 4 
void proc(void *p)
{
	volatile int i = 0;
	while(1)
		{
		if(lwpLockMutex(printf))
			printf("PROC %d\n", *((int *) p) );	
		lwpReleaseMutex(printf);
		for(i=0;i<1000;i++);	/* delay so we CAN pre-empt.	*/

					/* This delay isn't need if your*/ 
					/* function actually does       */
					/* something, and it wouldn't   */
					/* be needed here if printf was	*/
					/* re-entrant.			*/
     		}
}


int main()
{
	volatile int i;
	int ids[MAX_PROC];

	int a,b,c,d;
	a=1;
	b=2;
	c=3;
	d=4;

	clrscr();
	printf("\nLWP: Example1.\n");
	printf("This program spawns 4 threads that each print a message.\n");
	printf("Press any key to start....");
	fflush(stdout);
	getch();

	if(lwpInit(RTC1024, 1)) 
	{
	lwpCreateMutex(printf);
	ids[0] = lwpSpawn(proc, &a, 4096,1, TRUE);
	ids[1] = lwpSpawn(proc, &b, 4096,1, TRUE);
	ids[2] = lwpSpawn(proc, &c, 4096,1, TRUE);
	ids[3] = lwpSpawn(proc, &d, 4096,1, TRUE);
	while(!kbhit()) 
		{
		if(lwpLockMutex(printf))
			printf("MAIN\n");
		lwpReleaseMutex(printf);
		for(i=0;i<1000;i++);
		}
	getch();
	lwpKill(ids[0]);
	lwpKill(ids[1]);
	lwpKill(ids[2]);
	lwpKill(ids[3]);
	lwpDeleteMutex(printf);
	}
	return(0);
}
