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
        std::cerr << TLERROR_INIT_NO_QUANTUMS;
        return -1;
    }
	sigemptyset(&maskSignals);
	sigaddset(&maskSignals, SIGVTALRM);
    std::map<int, int> quantums;
    for (int i = 0; i < size; ++i)
    {
        if (quantum_usecs[i] < 0)
        {
            std::cerr << TLERROR_INIT_NEGATIVE_QUANTUM;
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
        std::cerr << TLERROR_SPAWN_NEGATIVE_PRIORITY;
        return -1;
    }
    if(sigprocmask(SIG_BLOCK, &maskSignals, nullptr))
	{
		std::cerr << SYS_ERROR_SIGPROCMASK;
		exit(EXIT_FAILURE);
	}
    int result = scheduler->addThread(f, priority);
	if(sigprocmask(SIG_UNBLOCK, &maskSignals, nullptr))
	{
		std::cerr << SYS_ERROR_SIGPROCMASK;
		exit(EXIT_FAILURE);
	}
	return result;
}

int uthread_change_priority(int tid, int priority)
{
	if(sigprocmask(SIG_BLOCK, &maskSignals, nullptr))
	{
		std::cerr << SYS_ERROR_SIGPROCMASK;
		exit(1);
	}	int result = scheduler->changePriority(tid, priority);
	if(sigprocmask(SIG_UNBLOCK, &maskSignals, nullptr))
	{
		std::cerr << SYS_ERROR_SIGPROCMASK;
		exit(EXIT_FAILURE);
	}	
	return result;
}

int uthread_terminate(int tid)
{
	if(sigprocmask(SIG_BLOCK, &maskSignals, nullptr))
	{
		std::cerr << SYS_ERROR_SIGPROCMASK;
		exit(EXIT_FAILURE);
	}    int result = scheduler->terminate(tid);
	if(sigprocmask(SIG_UNBLOCK, &maskSignals, nullptr))
	{
		std::cerr << SYS_ERROR_SIGPROCMASK;
		exit(EXIT_FAILURE);
	}    
	return result;
}

int uthread_block(int tid)
{
	if(sigprocmask(SIG_BLOCK, &maskSignals, nullptr))
	{
		std::cerr << SYS_ERROR_SIGPROCMASK;
		exit(EXIT_FAILURE);
	}    int result = scheduler->block(tid);
	if(sigprocmask(SIG_UNBLOCK, &maskSignals, nullptr))
	{
		std::cerr << SYS_ERROR_SIGPROCMASK;
		exit(EXIT_FAILURE);
	}    
	return result;
}

int uthread_resume(int tid)
{
	if(sigprocmask(SIG_BLOCK, &maskSignals, nullptr))
	{
		std::cerr << SYS_ERROR_SIGPROCMASK;
		exit(EXIT_FAILURE);
	}    int result = scheduler->resume(tid);
	if(sigprocmask(SIG_UNBLOCK, &maskSignals, nullptr))
	{
		std::cerr << SYS_ERROR_SIGPROCMASK;
		exit(EXIT_FAILURE);
	}
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