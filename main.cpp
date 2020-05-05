#include <iostream>
#include <zconf.h>
#include <assert.h>
#include "uthreads.cpp"

void f();
void g();


Dispatcher dispatcher;

void f(void)
{
    int i = 0;
    while(1){
        ++i;
        if (i % 100000 == 0)
        {
            assert(uthread_get_tid() == 1);

            std::cerr << "in f (" << i << ")";
        }
        if (i == 1500000)
        {
            uthread_block(1);
        }

    }
}

void g(void)
{
    int i = 0;
    while(1){
        ++i;
        if (i % 100000 == 0)
        {
            assert(uthread_get_tid() == 2);
            std::cerr << "in g (" << i << ")";
        }
    }
}
void h(void)
{
    int i = 0;
    while(1){
        ++i;
        if (i % 100000 == 0)
        {
            assert(uthread_get_tid() == 3);

            std::cerr << "in h (" << i << ")";
        }
    }
}
void funcymcfuncface(void)
{
    int i = 0;
    while(1){
        ++i;
        if (i % 100000 == 0)
        {
            assert(uthread_get_tid() == 4);

            std::cerr << "in funcymcfuncfunc (" << i << ")";
        }
        if (i == 3000000)
        {
            assert(uthread_resume(1) == 0);
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
    int a[2] = {10000, 10};
    uthread_init(a, 2);
    uthread_spawn(f, 0);
    uthread_spawn(g, 0);
    uthread_spawn(h, 0);
    uthread_spawn(funcymcfuncface, 0);


    while(1)
    {
        std::cerr << "In Main";
    }
	return 0;
}
