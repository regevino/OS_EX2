#include <iostream>
#include <zconf.h>
#include "uthreads.cpp"

void f(void)
{
    int i = 0;
    while(1){
        ++i;
        printf("in f (%d)\n",i);
        if (i > 100)
        {

        }
        usleep(1000000);
    }
}

void g(void)
{
    int i = 0;
    while(1){
        ++i;
        printf("in g (%d)\n",i);
        usleep(1000000);
    }
}


int main()
{
    static Thread thread1(1, 1, f);
    static Thread thread2(1, 1, g);
    Dispatcher dispatcher;
    dispatcher.switchToThread(std::shared_ptr<Thread>(&thread1), std::shared_ptr<Thread>(&thread2));

    return 0;
}
