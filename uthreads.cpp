//
// Created by Dan Regev on 4/22/2020.
//

#include "threadScheduler.h"



static std::shared_ptr<Scheduler> scheduler;
static sigset_t maskSignals;


int uthread_init(int *quantum_usecs, int size)
{
    if (size <= 0)
    {
        std::cerr << "thread library error: Cannot initialize library with no quantum values.\n";
        return -1;
    }
	sigemptyset(&maskSignals);
	sigaddset(&maskSignals, SIGVTALRM);
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
    return 0;
}

int uthread_spawn(void (*f)(), int priority)
{
    if (priority < 0)
    {
        std::cerr << "thread library error: Cannot spawn thread with negative priority.\n";
        return -1;
    }
    sigprocmask(SIG_BLOCK, &maskSignals, nullptr);
    int result = scheduler->addThread(f, priority);
    sigprocmask(SIG_UNBLOCK, &maskSignals, nullptr);
    return result;
}

int uthread_change_priority(int tid, int priority)
{
	sigprocmask(SIG_BLOCK, &maskSignals, nullptr);
	int result = scheduler->changePriority(tid, priority);
	sigprocmask(SIG_UNBLOCK, &maskSignals, nullptr);
	return result;
}

int uthread_terminate(int tid)
{
    sigprocmask(SIG_BLOCK, &maskSignals, nullptr);
    int result = scheduler->terminate(tid);
    sigprocmask(SIG_UNBLOCK, &maskSignals, nullptr);
    return result;
}

int uthread_block(int tid)
{
    sigprocmask(SIG_BLOCK, &maskSignals, nullptr);
    int result = scheduler->block(tid);
    sigprocmask(SIG_UNBLOCK, &maskSignals, nullptr);
    return result;
}

int uthread_resume(int tid)
{
    sigprocmask(SIG_BLOCK, &maskSignals, nullptr);
    int result = scheduler->resume(tid);
    sigprocmask(SIG_UNBLOCK, &maskSignals, nullptr);
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