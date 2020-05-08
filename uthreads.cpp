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
#include <iostream>
#include <algorithm>

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
            : id(id), quantum(quantum), totalQuantum(mainThread), priority(priority), state(READY),
              stack(nullptr)
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

    sigjmp_buf &getEnvironment()
    {
        return environment;
    }

    int getId() const
    {
        return id;
    }

    int getPriority() const
    {
        return priority;
    }

    void setPriority(int _priority)
    {
        this->priority = _priority;
    }

    states getState() const
    {
        return state;
    }

    void setState(states _state)
    {
        Thread::state = _state;
    }

    void incrementTotalQuantum()
    {
        ++totalQuantum;
    }

    std::unique_ptr<char[]> &getStack()
    {
        return stack;
    }

    int getTotalQuantum() const
    {
        return totalQuantum;
    }

private:
    int id;
    int quantum;
    int totalQuantum;
    int priority;
    states state;
    sigjmp_buf environment;
    std::unique_ptr<char[]> stack;
};


class Dispatcher
{
public:
    Dispatcher() : totalQuantums(1)
    {
    }

    void switchToThread(const std::shared_ptr<Thread> &currentThread,
                        const std::shared_ptr<Thread> &targetThread)
    {
        ++totalQuantums;
        targetThread->incrementTotalQuantum();
        int ret_val = sigsetjmp(currentThread->getEnvironment(), 1);
        if (ret_val == 1)
        {
            return;
        }
        siglongjmp(targetThread->getEnvironment(), 1);
    }

private:
    int totalQuantums;
public:
    int getTotalQuantums() const
    {
        return totalQuantums;
    }
};


class Scheduler
{
    static Scheduler *me;
public:
    explicit Scheduler(std::map<int, int> quantums) : numOfThreads(1), quantums(std::move(quantums))
    {
        me = this;
        timer.it_interval.tv_sec = 0;
        timer.it_interval.tv_usec = 0;
        timer.it_value.tv_sec = 0;
        sa.sa_handler = &Scheduler::timerHandler;
        if (sigaction(SIGVTALRM, &sa, NULL) < 0)
        {
            std::cerr << "system error: sigaction failure.\n";
            exit(1);
        }
        try
        {
            auto mainThread = std::make_shared<Thread>(0, quantums[0], 0, nullptr, true);
            running = mainThread;
            threads[0] = mainThread;
            setTimer(0);
        }
        catch (std::bad_alloc &e)
        {
            std::cerr << "system error: Memory allocation failure.\n";
            exit(1);
        }


    }

    void setTimer(int priority)
    {
        timer.it_value.tv_usec = quantums[priority];
        if (setitimer(ITIMER_VIRTUAL, &timer, NULL))
        {
            std::cerr << "system error: setitimer failure.\n";
            exit(1);
        }
//        std::cerr << "priority: " << priority << " quantums priority: " << quantums[priority]
//                  << '\n';
    }

    int addThread(EntryPoint_t entryPoint, int priority)
    {
        if (numOfThreads == MAX_THREAD_NUM || !quantums.count(priority))
        {
            std::cerr << "thread library error: Cannot create new thread with priority " << priority
                      << ".\n";
            return -1;
        }
        try
        {
            int j = 1;
            for (int i = 0; i < MAX_THREAD_NUM; ++i)
            {
                if (threads[i] == nullptr)
                {
                    j = i;
                    break;
                }
            }
            ready.erase(std::remove_if(ready.begin(), ready.end(), [j](const std::shared_ptr<Thread>& ptr)
            {
                return ptr->getId() == j;
            }), ready.end());
            threads[j] = std::make_shared<Thread>(j, quantums[priority], priority, entryPoint);
            ++numOfThreads;
            ready.push_back(std::shared_ptr<Thread>(threads[j]));
            return j;
        } catch (std::bad_alloc &e)
        {
            std::cerr << "system error: Memory allocation failure.\n";
            exit(1);
        }

    }

    static void timerHandler(int sig)
    {
        if (me->ready.empty())
        {
            return;
        }
        auto runningId = me->running->getId();
//        std::cerr << "\n------------ SIGNAL SIGVALRM --------------" << std::endl;
        if (runningId != me->ready.front()->getId())
        {
            me->ready.push_back(std::shared_ptr<Thread>(me->running));
        }

        std::shared_ptr<Thread> prev = me->running;
        me->running = std::shared_ptr<Thread>(me->ready.front());
        me->ready.pop_front();
        while (me->running->getState() == Thread::TERMINATED ||
               me->running->getState() == Thread::BLOCKED)
        {
            if (me->ready.empty())
            {
                return;
            }
            me->running = std::shared_ptr<Thread>(me->ready.front());
            me->ready.pop_front();
        }
        me->setTimer(me->running->getPriority());
        if (prev->getId() != me->running->getId())
        {
            me->dispatcher.switchToThread(prev, me->running);
        }

    }

    int changePriority(int tid, int priority)
    {
        if (threads[tid] == nullptr || !quantums.count(priority))
        {
            std::cerr << "thread library error: Cannot change priority of thread with id " << tid
                      << " to " << priority << ".\n";
            return -1;
        }
        threads[tid]->setPriority(priority);
        return 0;
    }

    int terminate(int tid)
    {
        if (threads[tid] == nullptr)
        {
            std::cerr << "thread library error: Cannot thread thread with id " << tid
                      << ": No such thread.\n";
            return -1;
        }
        threads[tid]->setState(Thread::TERMINATED);

        --numOfThreads;

        if (tid == 0)
        {
            clearAndExit();
        }
        if (running->getId() == tid)
        {
            ready.push_back(std::shared_ptr<Thread>(threads[tid]));
            auto previous = running;
            running = std::shared_ptr<Thread>(ready.front());
            while (running->getState() == Thread::TERMINATED ||
                   running->getState() == Thread::BLOCKED)
            {
                ready.pop_front();
                if (ready.empty())
                {
                    running = threads[0];
                }
                else
                {
                    running = std::shared_ptr<Thread>(ready.front());
                }
            }
            threads[tid].reset();
            dispatcher.switchToThread(previous, running);
        }
        threads[tid].reset();
        return 0;
    }

    void clearAndExit()
    {
        running.reset();
        ready.clear();
        for (int i = 0; i < MAX_THREAD_NUM; ++i)
        {
            threads[i].reset();
        }
        exit(0);
    }

    int block(int tid)
    {
        if (tid == 0 || threads[tid] == nullptr)
        {
            std::cerr << "thread library error: Cannot block thread with id " << tid << ".\n";
            return -1;
        }
        threads[tid]->setState(Thread::BLOCKED);
        if (tid != running->getId())
        {
            for (auto it = ready.begin(); it != ready.end(); it++)
            {
                if (it->get()->getId() == tid)
                {
                    ready.erase(it);
                    break;
                }
            }
        }
        else
        {
            running = std::shared_ptr<Thread>(ready.front());
            while (running->getState() == Thread::TERMINATED ||
                   running->getState() == Thread::BLOCKED)
            {
                ready.pop_front();
                if (ready.empty())
                {
                    running = threads[0];
                }
                else
                {
                    running = std::shared_ptr<Thread>(ready.front());
                }
            }
            dispatcher.switchToThread(threads[tid], running);
        }
        return 0;

    }

    int resume(int tid)
    {
        if (threads[tid] == nullptr)
        {
            std::cerr << "thread library error: Cannot resume thread with id " << tid
                      << ": No such thread.\n";
            return -1;
        }
        if (threads[tid]->getState() == Thread::BLOCKED)
        {
            ready.push_back(std::shared_ptr<Thread>(threads[tid]));
            threads[tid]->setState(Thread::READY);
//            for (const auto &it:ready)
//            {
//                std::cerr << "TID: " << it->getId() << ", NUMOFTHREADS: " << numOfThreads
//                          << ", STATE: " << it->getState() <<
//                          '\n';
//            }
        }
        return 0;
    }

    int getRunningId()
    {
        return running->getId();
    }

    int getTotalQuantums()
    {
        return dispatcher.getTotalQuantums();
    }

    int getThreadsQuantums(int tid)
    {
        if (threads[tid] == nullptr)
        {
            std::cerr << "thread library error: Cannot get quantum of thread with id " << tid
                      << ": No such thread.\n";
            return -1;
        }
        return threads[tid]->getTotalQuantum();
    }

private:
//    std::map<int, std::shared_ptr<Thread>> threads;
    std::shared_ptr<Thread> threads[MAX_THREAD_NUM];
    size_t numOfThreads;
    std::map<int, int> quantums;
    std::shared_ptr<Thread> running;
    std::deque<std::shared_ptr<Thread>> ready;
    Dispatcher dispatcher;
    struct sigaction sa = {0};
    struct itimerval timer;
};

Scheduler *Scheduler::me;

///////////////////////////////////////////////////////////////////////////////////////////////////

std::shared_ptr<Scheduler> scheduler;

int uthread_init(int *quantum_usecs, int size)
{
//    std::cerr << "ENTERED INIT\n";
    if (size <= 0)
    {
        std::cerr << "thread library error: Cannot initialize library with no quantum values.\n";
        return -1;
    }
    std::map<int, int> quantums;
    for (int i = 0; i < size; ++i)
    {
        if (quantum_usecs[i] < 0)
        {
            std::cerr << "thread library error: Cannot initialize library with negative quantum.\n";
            return -1;
        }
        quantums[i] = quantum_usecs[i];
    }
    scheduler = std::make_shared<Scheduler>(quantums);
//    std::cerr << "EXITING INIT\n";
    return 0;
}


sigset_t maskSignals;
int res = sigemptyset(&maskSignals);
int x = sigaddset(&maskSignals, SIGALRM);

int uthread_spawn(void (*f)(void), int priority)
{
//    std::cerr << "ENTERED SPAWN\n";
    if (priority < 0)
    {
        std::cerr << "thread library error: Cannot spawn thread with negative priority.\n";
        return -1;
    }
    sigprocmask(SIG_BLOCK, &maskSignals, NULL);
    int result = scheduler->addThread(f, priority);
    sigprocmask(SIG_UNBLOCK, &maskSignals, NULL);
//    std::cerr << "EXITING SPAWN\n";
    return result;
}

int uthread_change_priority(int tid, int priority)
{
//    std::cerr << "ENTERED CHANGE PRIORITY\n";
    return scheduler->changePriority(tid, priority);
}

int uthread_terminate(int tid)
{
//    std::cerr << "ENTERED TERMINATE\n";
    sigprocmask(SIG_BLOCK, &maskSignals, NULL);
    int result = scheduler->terminate(tid);
    sigprocmask(SIG_UNBLOCK, &maskSignals, NULL);
//    std::cerr << "EXITING TERMINATE\n";
    return result;
}

int uthread_block(int tid)
{
    sigprocmask(SIG_BLOCK, &maskSignals, NULL);
    int result = scheduler->block(tid);
    sigprocmask(SIG_UNBLOCK, &maskSignals, NULL);
    return result;
}

int uthread_resume(int tid)
{
//    std::cerr << "ENTERED RESUME\n";
    sigprocmask(SIG_BLOCK, &maskSignals, NULL);
    int result = scheduler->resume(tid);
    sigprocmask(SIG_UNBLOCK, &maskSignals, NULL);
//    std::cerr << "EXITING RESUME\n";
    return result;
}

int uthread_get_tid()
{
    return scheduler->getRunningId();
}

int uthread_get_total_quantums()
{
    return scheduler->getTotalQuantums();
}

int uthread_get_quantums(int tid)
{
    return scheduler->getThreadsQuantums(tid);
}