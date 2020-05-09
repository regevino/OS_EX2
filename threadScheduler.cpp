//
// Created by Avinoam on 5/9/2020.
//

#include "threadScheduler.h"
#include <iostream>

#define MAX_MILISECONDS 999999
#define MAIN_THREAD_ID 0
#define FAILURE -1
#define SUCCESS 0
#define MAIN_THREAD_PRIORITY 0
#define INITIAL_QUANTUMS 1
#define INITIAL_NUM_OF_THREADS 1

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;

#define JB_SP 6

#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
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

Thread::Thread(int id, int priority, EntryPoint_t entry, bool mainThread)
        : id(id), totalQuantum(mainThread), priority(priority), state(READY), stack(nullptr)
{
    sigsetjmp(environment, 1);
    if (!mainThread)
    {
        stack = std::unique_ptr<char[]>(new char[STACK_SIZE]);
        address_t sp = (address_t) stack.get() + STACK_SIZE - sizeof(address_t);
        auto pc = (address_t) entry;
        (environment->__jmpbuf)[JB_SP] = translate_address(sp);
        (environment->__jmpbuf)[JB_PC] = translate_address(pc);
        if (sigemptyset(&environment->__saved_mask))
        {
            std::cerr << "system error: sigemptyset failure.\n";
            exit(EXIT_FAILURE);
        }
    }
}

sigjmp_buf &Thread::getEnvironment()
{
    return environment;
}

int Thread::getId() const
{
    return id;
}

int Thread::getPriority() const
{
    return priority;
}

void Thread::setPriority(int _priority)
{
    this->priority = _priority;
}

Thread::states Thread::getState() const
{
    return state;
}

void Thread::setState(states _state)
{
    Thread::state = _state;
}

void Thread::incrementTotalQuantum()
{
    ++totalQuantum;
}

int Thread::getTotalQuantum() const
{
    return totalQuantum;
}

Dispatcher::Dispatcher() : totalQuantums(INITIAL_QUANTUMS)
{
}

void Dispatcher::switchToThread(std::shared_ptr<Thread> &&currentThread,
                                const std::shared_ptr<Thread> &targetThread)
{
    ++totalQuantums;
    targetThread->incrementTotalQuantum();
    int ret_val = sigsetjmp(currentThread->getEnvironment(), 1);
    currentThread.reset();
    if (ret_val == 1)
    {
        return;
    }
    siglongjmp(targetThread->getEnvironment(), 1);
}

int Dispatcher::getTotalQuantums() const
{
    return totalQuantums;
}

Scheduler::Scheduler(const std::map<int, int> &pQuantums) : numOfThreads(INITIAL_NUM_OF_THREADS)
{
    me = this;
    for (const auto &quant: pQuantums)
    {
        itimerval timer{};
        timer.it_interval.tv_sec = 0;
        timer.it_interval.tv_usec = 0;
        __suseconds_t usecs = quant.second;
        __time_t seconds = 0;
        while (usecs > MAX_MILISECONDS)
        {
            ++seconds;
            usecs -= MAX_MILISECONDS;
        }
        timer.it_value.tv_sec = seconds;
        timer.it_value.tv_usec = usecs;
        quantums[quant.first] = timer;
    }

    sa.sa_handler = &Scheduler::timerHandler;
    if (sigaction(SIGVTALRM, &sa, nullptr) < 0)
    {
        std::cerr << "system error: sigaction failure.\n";
        exit(EXIT_FAILURE);
    }
    try
    {
        auto mainThread = std::make_shared<Thread>(MAIN_THREAD_ID, MAIN_THREAD_PRIORITY, nullptr,
                                                   true);
        running = mainThread;
        threads[MAIN_THREAD_ID] = mainThread;
        setTimer(MAIN_THREAD_PRIORITY);
    }
    catch (std::bad_alloc &e)
    {
        std::cerr << "system error: Memory allocation failure.\n";
        exit(EXIT_FAILURE);
    }
}

void Scheduler::setTimer(int priority)
{
    if (setitimer(ITIMER_VIRTUAL, &quantums[priority], nullptr))
    {
        std::cerr << "system error: setitimer failure.\n";
        exit(EXIT_FAILURE);
    }
}

int Scheduler::addThread(EntryPoint_t entryPoint, int priority)
{
    if (numOfThreads == MAX_THREAD_NUM || !quantums.count(priority))
    {
        std::cerr << "thread library error: Cannot create new thread with priority " << priority
                  << ".\n";
        return FAILURE;
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
        ready.erase(
                std::remove_if(ready.begin(), ready.end(), [j](const std::shared_ptr<Thread> &ptr)
                {
                    return ptr->getId() == j;
                }), ready.end());
        threads[j] = std::make_shared<Thread>(j, priority, entryPoint);
        ++numOfThreads;
        ready.push_back(threads[j]);
        return j;
    } catch (std::bad_alloc &e)
    {
        std::cerr << "system error: Memory allocation failure.\n";
        exit(EXIT_FAILURE);
    }
}

void Scheduler::timerHandler(int)
{
    if (me->ready.empty())
    {
        return;
    }
    auto runningId = me->running->getId();
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
        me->dispatcher.switchToThread(std::move(prev), me->running);
    }
}

int Scheduler::changePriority(int tid, int priority)
{
    if (threads[tid] == nullptr || !quantums.count(priority))
    {
        std::cerr << "thread library error: Cannot change priority of thread with id " << tid
                  << " to " << priority << ".\n";
        return FAILURE;
    }
    threads[tid]->setPriority(priority);
    return SUCCESS;
}

int Scheduler::terminate(int tid)
{
    if (threads[tid] == nullptr)
    {
        std::cerr << "thread library error: Cannot thread thread with id " << tid
                  << ": No such thread.\n";
        return FAILURE;
    }
    threads[tid]->setState(Thread::TERMINATED);

    --numOfThreads;

    if (tid == MAIN_THREAD_ID)
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
                running = threads[MAIN_THREAD_ID];
            }
            else
            {
                running = std::shared_ptr<Thread>(ready.front());
            }
        }
        threads[tid].reset();
        dispatcher.switchToThread(std::move(previous), running);
    }
    threads[tid].reset();
    return SUCCESS;
}

void Scheduler::clearAndExit()
{
    running.reset();
    ready.clear();
    for (auto &thread : threads)
    {
        thread.reset();
    }
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGVTALRM, &sa, nullptr) < 0)
    {
        std::cerr << "system error: sigaction failure.\n";
        exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}

int Scheduler::block(int tid)
{
    if (tid == MAIN_THREAD_ID || threads[tid] == nullptr)
    {
        std::cerr << "thread library error: Cannot block thread with id " << tid << ".\n";
        return FAILURE;
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
                running = threads[MAIN_THREAD_ID];
            }
            else
            {
                running = std::shared_ptr<Thread>(ready.front());
            }
        }
        dispatcher.switchToThread(std::shared_ptr<Thread>(threads[tid]), running);
    }
    return SUCCESS;
}

int Scheduler::resume(int tid)
{
    if (threads[tid] == nullptr)
    {
        std::cerr << "thread library error: Cannot resume thread with id " << tid
                  << ": No such thread.\n";
        return FAILURE;
    }
    if (threads[tid]->getState() == Thread::BLOCKED)
    {
        ready.push_back(std::shared_ptr<Thread>(threads[tid]));
        threads[tid]->setState(Thread::READY);
    }
    return SUCCESS;
}

int Scheduler::getRunningId()
{
    return running->getId();
}

int Scheduler::getTotalQuantums()
{
    return dispatcher.getTotalQuantums();
}

int Scheduler::getThreadsQuantums(int tid)
{
    if (threads[tid] == nullptr)
    {
        std::cerr << "thread library error: Cannot get quantum of thread with id " << tid
                  << ": No such thread.\n";
        return FAILURE;
    }
    return threads[tid]->getTotalQuantum();
}

Scheduler *Scheduler::me;