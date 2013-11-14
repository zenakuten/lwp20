 #include "lwp.h"
#include <dpmi.h>
#include <dos.h>
#include <stdlib.h>
#include <math.h>
#include <sys/farptr.h>
#include <go32.h>
#include <time.h>
#include <ctype.h>
#include <stdio.h>
#include <conio.h>

#define BLOB_COUNT 240 
#define CENTERY 0.1
#define CENTERX 0.1
volatile int plotCount = 0;
void putpixel(int x, int y, char col);
void blobProc(void *);
void waitVBL(void);
void getNewCoords(double *x, double *y,double *xspeed,double *yspeed,double centerx,double centery);
double gravConst = 0.0525;
int main()
{
	int i;
	char answer = 0;
	int ids[BLOB_COUNT];
	__dpmi_regs regs;
	clrscr();
	printf("LWP:  Example 4.  Pre-emptive/Coop\n");
	printf("This program spawns %d threads that each draw\n", BLOB_COUNT);
	printf("a dot on the screen that orbit around the center\n");
	printf("of the screen.  \n\n");
	printf("U = Increase gravitational constant\n");
	printf("D = Decrease gravitational constant\n");
	printf("N = Spawn a new thread. (Don't over due it!)\n");
	printf("Q = Quit.\n");
	printf("Press any key to continue...");
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
		ids[i] = lwpSpawn(blobProc, NULL, 4096, 1, TRUE);
		}
	while((toupper(answer) != 'Q'))
		{
		while(!kbhit())
			{
			if(plotCount >= BLOB_COUNT)
				{
				plotCount = 0;
				putpixel(0,0,90);
				waitVBL();
				}	
			lwpYield();
			}
		lwpEnterCriticalSection();
		answer = toupper(getch());
		lwpLeaveCriticalSection();
		if (answer == 'U')
			{
			gravConst+=0.001;
			}
		if (answer == 'D')
			{
			gravConst-=0.001;
			}
		if (answer == 'N')
			{
			lwpSpawn(blobProc, NULL, 4096, 1, TRUE);
			}
		}
	for(i=0;i<BLOB_COUNT;i++)
		{
		lwpKill(ids[i]);
		}
	regs.x.ax = 0x3;
	lwpEnterCriticalSection();
	__dpmi_int(0x10, &regs);
	lwpLeaveCriticalSection();
	return(0);
}

void blobProc(void *p)
{
	double x, y, xspeed,yspeed;
	char col;
	lwpEnterCriticalSection();
        x = (double) 70 * ((float) random()/ (float) RAND_MAX) -35.0;
	y = (double) 70 * ((float) random()/ (float) RAND_MAX) - 35.0;
	col = (char) 200 * ((float) random() / (float) RAND_MAX) + 50;	
	xspeed = (double) 2 * ((float) random()/ (float) RAND_MAX) - 1.0;
	yspeed = (double) 2 * ((float) random()/ (float) RAND_MAX) - 1.0;
	_farsetsel(_dos_ds);
	lwpLeaveCriticalSection();
	while(1)
		{
		putpixel((int) x,(int) y,col);	
		putpixel((int) x+1,(int) y,col); 
		putpixel((int) x,(int) y+1,col); 
		putpixel((int) x+1,(int) y+1,col); 
		plotCount++;
		lwpYield();
		putpixel((int) x,(int) y,0); 
		putpixel((int) x+1,(int) y,0); 
		putpixel((int) x,(int) y+1,0); 
		putpixel((int) x+1,(int) y+1,0); 
		/* get new X,Y position from vector function */
		getNewCoords(&x,&y,&xspeed,&yspeed,CENTERX, CENTERY);
		}
}	
 
void putpixel(int x, int y, char col)
{
 	if(!((x < -159) || (x > 159) || (y < -99) || (y > 99)))	
		{
		x+=159;
		y+=99;
		lwpEnterCriticalSection();
		_farnspokeb(0xA0000+x+(y<<6)+(y<<8), col);
		lwpLeaveCriticalSection();
		}
}

void waitVBL(void)
{
	/* Not really a critical section, but there's no point
	 * in waiting for the virtical blank if you are switching
	 * to tasks that draw on the screen.
	 */

	lwpEnterCriticalSection();
	while(!(inportb(0x3DA) & 8));
	while(inportb(0x3DA) & 8);
	lwpLeaveCriticalSection();
}

void getNewCoords(double *x, double *y,double *xspeed,double *yspeed,double centerx,double centery)
{
        double tmp1, tmp2, tmp3, tmp4;
	double divisor1, divisor2;
        tmp1 = (centerx)-(*xspeed)-(*x);
        tmp2 = (centery)-(*yspeed)-(*y);     

	lwpEnterCriticalSection();
	divisor1 = sqrt((tmp1*tmp1)+(tmp2*tmp2));
	lwpLeaveCriticalSection();

	divisor2 = ((centerx-(*xspeed))*(centerx-(*xspeed)))+((centery-(*yspeed))*(centery-(*yspeed)));
        if((divisor1 != 0) && (divisor2 != 0))
                {
		tmp3 = tmp1/divisor1;
		tmp4 = tmp2/divisor1;
		(*xspeed) = ((*xspeed) + tmp3*gravConst/divisor2);
		(*yspeed) = ((*yspeed) + tmp4*gravConst/divisor2);
		(*x) += (*xspeed);
		(*y) += (*yspeed);
                }
        else
                {
		(*x) += (*xspeed);
		(*y) += (*yspeed);
                }
}
