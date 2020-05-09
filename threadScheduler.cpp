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
    	// Creating a new thread, set environment and allcoate a stack.
        stack = std::unique_ptr<char[]>(new char[STACK_SIZE]);
        address_t sp = (address_t) stack.get() + STACK_SIZE - sizeof(address_t);
        auto pc = (address_t) entry;
        (environment->__jmpbuf)[JB_SP] = translate_address(sp);
        (environment->__jmpbuf)[JB_PC] = translate_address(pc);
        if (sigemptyset(&environment->__saved_mask))
        {
            std::cerr << SYS_ERROR_SIGEMPTYSET;
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
	// Increment the quantum count:
    ++totalQuantums;
    targetThread->incrementTotalQuantum();

    // Save current state
    int ret_val = sigsetjmp(currentThread->getEnvironment(), 1);
    if (ret_val == 1)
    {
		return;
	}
    // Before jumping, release the pointer for the old thread.
	currentThread.reset();

    // Preform context switch.
	siglongjmp(targetThread->getEnvironment(), 1);
}

int Dispatcher::getTotalQuantums() const
{
    return totalQuantums;
}

Scheduler::Scheduler(const std::map<int, int> &pQuantums) : numOfThreads(INITIAL_NUM_OF_THREADS)
{
	if (me != nullptr)
	{
		// Scheduler has already been instantiated once, so throw exception.
		throw SchedulerException();
	}

	// Keep a pointer to this instance.
    me = this;


	// Set timers for all possible quantums:
	for (const auto &quant: pQuantums)
    {
        itimerval timer{};
        timer.it_interval.tv_sec = 0;
        timer.it_interval.tv_usec = 0;
        __suseconds_t usecs = quant.second;
        __time_t seconds = 0;
        while (usecs > MAX_MILISECONDS)
        {
        	// Number of milliseconds is longer than a second.
            ++seconds;
            usecs -= MAX_MILISECONDS;
        }
        timer.it_value.tv_sec = seconds;
        timer.it_value.tv_usec = usecs;
        quantums[quant.first] = timer;
    }

	// Set the sigaction handler for the timer:
    sa.sa_handler = &Scheduler::timerHandler;
    if (sigaction(SIGVTALRM, &sa, nullptr) < 0)
    {
        std::cerr << SYS_ERROR_SIGACTION;
        exit(EXIT_FAILURE);
    }

    // Create the main thread as thread with ID 0:
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
        std::cerr << SYS_ERROR_MEMORY_ALLOC;
        exit(EXIT_FAILURE);
    }
}

void Scheduler::setTimer(int priority)
{
	// Set the timer for a quantum corresponding to priority.
    if (setitimer(ITIMER_VIRTUAL, &quantums[priority], nullptr))
    {
        std::cerr << SYS_ERROR_SETITIMER;
        exit(EXIT_FAILURE);
    }
}

int Scheduler::addThread(Thread::EntryPoint_t entryPoint, int priority)
{
	// If there are already MAX_THREAD_NUM threads, return a failure.
    if (numOfThreads == MAX_THREAD_NUM || !quantums.count(priority))
    {
        std::cerr << ADD_THREAD_ERR_MSG << priority << '\n';
        return FAILURE;
    }

    try
    {
    	// Find the lowest free ID:
        int lowest_id = 1;
        for (int i = 0; i < MAX_THREAD_NUM; ++i)
        {
            if (threads[i] == nullptr)
            {
				lowest_id = i;
                break;
            }
        }
        // Make sure there aren't any pointers to threads with this ID in the ready queue:
        ready.erase(
                std::remove_if(ready.begin(), ready.end(), [lowest_id](const std::shared_ptr<Thread> &ptr)
                {
                    return ptr->getId() == lowest_id;
                }), ready.end());

        // Create the thread and add it to the queue, then return its ID:
        threads[lowest_id] = std::make_shared<Thread>(lowest_id, priority, entryPoint);
        ++numOfThreads;
        ready.push_back(threads[lowest_id]);
        return lowest_id;
    } catch (std::bad_alloc &e)
    {
        std::cerr << SYS_ERROR_MEMORY_ALLOC;
        exit(EXIT_FAILURE);
    }
}

void Scheduler::timerHandler(int)
{
    if (me->ready.empty())
    {
    	// There are no threads in the queue, so keep running until the timer goes again.
		me->setTimer(me->running->getPriority());
		return;
    }
    auto runningId = me->running->getId();

    if (runningId != me->ready.front()->getId())
    {
    	// Only if this thread is not also next in the queue, push it to the back.
        me->ready.push_back(std::shared_ptr<Thread>(me->running));
    }

    std::shared_ptr<Thread> prev = me->running;

    // Get the next thread:
    me->running = std::shared_ptr<Thread>(me->ready.front());
    me->ready.pop_front();
    while (me->running->getState() == Thread::TERMINATED ||
           me->running->getState() == Thread::BLOCKED)
    {
    	// Skip all the thread that are terminated or blocked:
        if (me->ready.empty())
        {
			me->setTimer(me->running->getPriority());
			return;
        }
        me->running = std::shared_ptr<Thread>(me->ready.front());
        me->ready.pop_front();
    }

    // Set the timer for the next thread and preform the context switch:
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
        std::cerr << CHANGE_PRIORITY_ERR_MSG << tid << " to " << priority << ".\n";
        return FAILURE;
    }
    threads[tid]->setPriority(priority);
    return SUCCESS;
}

int Scheduler::terminate(int tid)
{
    if (threads[tid] == nullptr)
    {
        std::cerr << TERMINATION_ERR_MSG << tid << NON_EXISTENT_THREAD_MSG;
        return FAILURE;
    }
    // Set the thread as terminated:
    threads[tid]->setState(Thread::TERMINATED);
    --numOfThreads;

    if (tid == MAIN_THREAD_ID)
    {
    	// Main thread was terminated, so exit the program.
        clearAndExit();
    }
    if (running->getId() == tid)
    {
    	// Put this thread in the queue so that its resources will be freed by a future thread
    	// when it comes out of the queue:
        ready.push_back(std::shared_ptr<Thread>(threads[tid]));
		// Running thread was terminated, so get the next thread:
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
        // Release the pointer to this thread and switch:
        threads[tid].reset();
        dispatcher.switchToThread(std::move(previous), running);
    }
    // Release the pointer to this thread:
    threads[tid].reset();
    return SUCCESS;
}

void Scheduler::clearAndExit()
{
	// Release all the pointers so resources are all freed:
    running.reset();
    ready.clear();
    for (auto &thread : threads)
    {
        thread.reset();
    }
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGVTALRM, &sa, nullptr) < 0)
    {
        std::cerr << SYS_ERROR_SIGACTION;
        exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}

int Scheduler::block(int tid)
{
    if (tid == MAIN_THREAD_ID || threads[tid] == nullptr)
    {
    	// It's an error to block the main thread.
        std::cerr << BLOCK_ERR_MSG << tid << '\n';
        return FAILURE;
    }
    // Set the state as blocked:
    threads[tid]->setState(Thread::BLOCKED);
    if (tid != running->getId())
    {
    	// Remove all pointers to this thread from the ready queue:
		ready.erase(
				std::remove_if(ready.begin(), ready.end(), [tid](const std::shared_ptr<Thread> &ptr)
				{
					return ptr->getId() == tid;
				}), ready.end());
    }
    else
    {
    	// Get the next thread:
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
        // Preform the context switch:
        dispatcher.switchToThread(std::shared_ptr<Thread>(threads[tid]), running);
    }
    return SUCCESS;
}

int Scheduler::resume(int tid)
{
    if (threads[tid] == nullptr)
    {
        std::cerr << RESUME_ERR_MSG << tid << NON_EXISTENT_THREAD_MSG;
        return FAILURE;
    }
    if (threads[tid]->getState() == Thread::BLOCKED)
    {
    	// If the thread was indeed blocked, add it back to the queue:
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
        std::cerr << QUANTUM_ERR_MSG << tid << NON_EXISTENT_THREAD_MSG;
        return FAILURE;
    }
    return threads[tid]->getTotalQuantum();
}

// Set the static pointer to null:
Scheduler *Scheduler::me = nullptr;