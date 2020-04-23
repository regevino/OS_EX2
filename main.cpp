#include <iostream>
#include <zconf.h>
#include "uthreads.cpp"

void f();
void g();

static Thread thread1(1, 1, f);
static Thread thread2(2, 1, g);
std::shared_ptr<Thread> pThread1(&thread1);
std::shared_ptr<Thread> pThread2(&thread2);

Dispatcher dispatcher;

void f(void)
{
    int i = 0;
    while(1){
        ++i;
        printf("in f (%d)\n",i);
        if (i % 10 == 0)
        {
			dispatcher.switchToThread(pThread1, pThread2);
		}
        usleep(10000);
    }
}

void g(void)
{
    int i = 0;
    while(1){
        ++i;
        printf("in g (%d)\n",i);
		if (i % 10 == 0)
		{
			dispatcher.switchToThread(pThread2, pThread1);
		}
        usleep(10000);
    }
}


int main()
{
	f();
//	dispatcher.switchToThread(pThread1, pThread2);
	return 0;
}
