//
// Created by Dan Regev on 4/22/2020.
//

#include "uthreads.h"
#include <map>
#include <memory>
#include <queue>
#include <utility>
#include <setjmp.h>
#include <signal.h>
#include <sys/time.h>

typedef void (*EntryPoint_t)(void);

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6

#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
static address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
        "rol    $0x9,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}



#endif

class Thread
{
public:
    enum states
    {
        READY,
        RUNNING,
        BLOCKED,
        TERMINATED
    };

    Thread(int id, int quantum, int priority, EntryPoint_t entry, bool mainThread = false)
            : id(id), quantum(quantum), priority(priority), state(READY), entry(entry), stack(nullptr)
    {
        sigsetjmp(environment, 1);
        if (!mainThread)
        {
            stack = std::unique_ptr<char[]>(new char[STACK_SIZE]);
            address_t sp = (address_t) stack.get() + STACK_SIZE - sizeof(address_t);
            auto pc = (address_t) entry;
            (environment->__jmpbuf)[JB_SP] = translate_address(sp);
            (environment->__jmpbuf)[JB_PC] = translate_address(pc);
            sigemptyset(&environment->__saved_mask);
        }
    }

    sigjmp_buf &getEnvironment(){return environment;}
    int getId() const{return id;}
    int getPriority() const{return priority;}
    void setPriority(int priority){ this->priority = priority;}
    states getState() const{return state;}
    void setState(states state){Thread::state = state;}
private:
    int id;
    int quantum;
    int totalQuantum;
    int priority;
    states state;
    sigjmp_buf environment;
    std::unique_ptr<char[]> stack;
public:
    std::unique_ptr<char[]> &getStack()
    {

        return stack;
    }

private:
    EntryPoint_t entry;
};


class Dispatcher
{
public:
    void switchToThread(const std::shared_ptr<Thread>& currentThread,
                        const std::shared_ptr<Thread>& targetThread)
    {
        if (currentThread->getId() == 2)
        {
            std::cerr << "HELLLP its thread 2!!!!" << std::endl;
        }
        int ret_val = sigsetjmp(currentThread->getEnvironment(), 1);
        if (ret_val == 1)
        {
            std::cerr << "Returning to " << currentThread->getId() << '\n';
            return;
        }
        std::cerr << "Jumping to " << targetThread->getId() << '\n';
        siglongjmp(targetThread->getEnvironment(), 1);
    }

private:

};


class Scheduler
{
    static Scheduler *me;
public:
    explicit Scheduler(std::map<int, int> quantums)
            : quantums(std::move(quantums))
    {
        me = this;
        timer.it_interval.tv_sec = 0;
        timer.it_interval.tv_usec = 0;
        timer.it_value.tv_sec = 0;
        sa.sa_handler = &Scheduler::timerHandler;
        if (sigaction(SIGVTALRM, &sa, NULL) < 0)
        {
            printf("sigaction error.");//TODO
        }
        auto mainThread = std::make_shared<Thread>(0, quantums[0], 0, nullptr, true);
        running = mainThread;
        threads[0] = mainThread;
        setTimer(0);

    }

    void setTimer(int priority)
    {
        timer.it_value.tv_usec = quantums[priority];
        if (setitimer(ITIMER_VIRTUAL, &timer, NULL))
        {
            printf("setitimer error.");//TODO
        }
        std::cerr << "priority: " << priority << " quantums priority: " << quantums[priority]
                  << '\n';
    }

    int addThread(EntryPoint_t entryPoint, int priority)
    {
        int size = threads.size();
        if (size == MAX_THREAD_NUM)
        {
            return -1;
        }
        threads[size] = std::make_shared<Thread>(size, quantums[priority], priority, entryPoint);
        ready.push_back(std::shared_ptr<Thread>(threads[size]));
        return 0;
    }

    static void timerHandler(int sig)
    {
        auto id = me->running->getId();
        std::cerr << "Handling sig" << std::endl;
        me->ready.push_back(me->running);
        std::shared_ptr<Thread> prev = me->running;
        me->running = std::shared_ptr<Thread>(me->ready.front());
        me->ready.pop_front();
        while (me->running->getState() == Thread::TERMINATED)
        {
            me->threads.erase(me->running->getId());
            me->running = me->ready.front();
            me->ready.pop_front();
        }
        me->setTimer(me->running->getPriority());
        std::cerr << "prev id: " << prev->getId() << " running id: " << me->running->getId() <<
                  std::endl;
        std::cerr << "Id that started this call is: " << id << std::endl;
        me->dispatcher.switchToThread(prev, me->running);
        for (const auto& thread:me->threads)
        {
            std::cerr << "Thread key is " << thread.first << " And thread id is " << thread
                    .second->getId();
            if (thread.second->getId() != 0)
            {
                void* p = thread.second->getStack().get();
                long pp = (long)(p);
                std::cerr << " And stack Location is: " <<
                          pp  << " And call stack is: " << (long)&pp << std::endl;
            }
        }
    }

    int changePriority(int tid, int priority)
    {
        if (tid == 0)
        {
            return -1;
        }
        threads[tid]->setPriority(priority);
        return 0;
    }

    int terminate(int tid)
    {
        threads[tid]->setState(Thread::TERMINATED);
        if (tid==0)
        {
            clearAndExit();
        }
        if (running->getId() == tid)
        {
            std::cerr << "HELP ME!\n";
            running = ready.front();
            ready.pop_front();
            setTimer(running->getPriority());
            dispatcher.switchToThread(threads[tid], running);
        }
        return 0;
    }
    void clearAndExit()
    {
        running.reset();
        ready.clear();
        threads.clear();
        exit(0);
    }

private:
    std::map<int, std::shared_ptr<Thread>> threads;
    std::map<int, int> quantums;
    std::shared_ptr<Thread> running;
    std::deque<std::shared_ptr<Thread>> ready;
    Dispatcher dispatcher;
    struct sigaction sa = {0};
    struct itimerval timer;
};
Scheduler*Scheduler::me;

///////////////////////////////////////////////////////////////////////////////////////////////////

std::shared_ptr<Scheduler> scheduler;

int uthread_init(int *quantum_usecs, int size)
{
    std::map<int, int> quantums;
    for (int i = 0; i < size; ++i)
    {
        if (quantum_usecs[i] < 0)
        {
            return -1;
        }
        quantums[i] = quantum_usecs[i];
    }
    scheduler = std::make_shared<Scheduler>(quantums);

    return 0;
}


sigset_t maskSignals;
int res = sigemptyset(&maskSignals);
int x = sigaddset(&maskSignals, SIGALRM);

int uthread_spawn(void (*f)(void), int priority)
{
    sigprocmask(SIG_BLOCK, &maskSignals, NULL);
    int result = scheduler->addThread(f, priority);
    sigprocmask(SIG_UNBLOCK, &maskSignals, NULL);
    return result;
}

int uthread_change_priority(int tid, int priority)
{
    return scheduler->changePriority(tid, priority);
}

int uthread_terminate(int tid)
{
    sigprocmask(SIG_BLOCK, &maskSignals, NULL);
    int result = scheduler->terminate(tid);
    sigprocmask(SIG_UNBLOCK, &maskSignals, NULL);
    return result;
}