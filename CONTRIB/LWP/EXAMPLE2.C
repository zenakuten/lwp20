#include "lwp.h"
#include <dpmi.h>
#include <dos.h>
#include <stdlib.h>
#include <math.h>
#include <sys/farptr.h>
#include <time.h>
#include <go32.h>
#include <stdio.h>
#include <conio.h>

#define BLOB_COUNT 200  /* 200 threads */
volatile int plotCount = 0;
void putpixel(int x, int y, char col);
void blobProc(void *);
void waitVBL(void);

int main()
{
	int i;
	int ids[BLOB_COUNT];
	__dpmi_regs regs;

	clrscr();
	printf("\nLWP: Example 2.  Pre-emptive \n");
	printf("This program spawns %d threads that each draw\n", BLOB_COUNT);
	printf("a dot on the screen that moves in a random direction\n");
	printf("\nPress any key to continue...");
	fflush(stdout);
	getch();
	regs.x.ax = 0x13;
	srandom(time(0));

	__dpmi_int(0x10, &regs); 

	
	lwpInit(RTC128, 1);

	lwpEnterCriticalSection();
	_farsetsel(_dos_ds); 
	lwpLeaveCriticalSection();

	for(i=0;i<BLOB_COUNT;i++)
		{
		ids[i] = lwpSpawn(blobProc, NULL, 4096,1, TRUE);
		}
	while(!kbhit())
		{
		if(plotCount == BLOB_COUNT)
			{
			plotCount = 0;
			waitVBL();
			}	
		plotCount++;
		}
	for(i=0;i<BLOB_COUNT;i++)
		{
		lwpKill(ids[i]);
		}

	lwpEnterCriticalSection();
	getch();
	regs.x.ax = 0x3;
	__dpmi_int(0x10, &regs);
	lwpLeaveCriticalSection();


	return(0);
}

void blobProc(void *p)
{
	int x, y, xspeed,yspeed;
	char col;
	lwpEnterCriticalSection();
        x = random() & 319;
	y = random() & 199;
	col = random() & 0xF0;	
	xspeed = random() & 1;
	yspeed = random() & 1;
	_farsetsel(_dos_ds);
	lwpLeaveCriticalSection();
	while(1)
		{
		putpixel(x,y,0);
		plotCount++;
		x+=xspeed;
		if(x>=319)
			{
			xspeed=-xspeed;
			x = 319;
			}
		if(x<=0)
			{
			xspeed=-xspeed;
			x = 0;
			}	
		y+=yspeed;
		if(y>=199)
			{
			yspeed=-yspeed;
			y=199;
			}
		if(y<=0)
			{
			yspeed=-yspeed;
			y=0;
			}
		putpixel(x,y,col);	
		}
}	
 
void putpixel(int x, int y, char col)
{
	lwpEnterCriticalSection();
	_farnspokeb(0xA0000+x+(y<<6)+(y<<8), col);
	lwpLeaveCriticalSection();
}

void waitVBL(void)
{
	/* not really a critical section, but there's no point
	 * to wait for virtical blanking while switching to tasks
	 * that draw on the screen 
	 */
	lwpEnterCriticalSection();
	while(!(inportb(0x3DA) & 8));
	while(inportb(0x3DA) & 8);
	lwpLeaveCriticalSection();
}
