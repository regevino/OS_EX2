#include <iostream>
#include <zconf.h>
#include "uthreads.cpp"

void f();
void g();


Dispatcher dispatcher;

void f(void)
{
    int i = 0;
    while(1){
        ++i;
        if (i % 1000 == 0)
        {
            printf("in f (%d)\n",i);
        }
        if (i > 1500)
        {
            uthread_terminate(2);
        }

    }
}

void g(void)
{
    int i = 0;
    while(1){
        ++i;
        if (i % 100 == 0)
        {
            printf("in g (%d)\n",i);
        }
    }
}

void timer_handler(int)
{
    std::cerr << "SIGNAL" << std::endl;
}

int main()
{
//	dispatcher.switchToThread(pThread1, pThread2);
    int a = 1000;
    uthread_init(&a, 1);
    uthread_spawn(f, 0);
    uthread_spawn(g, 0);

    while(1)
    {

    }
	return 0;
}
