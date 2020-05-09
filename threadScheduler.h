//
// Created by Avinoam on 5/9/2020.
//

#ifndef THREADS_THREADSCHEDULER_H
#define THREADS_THREADSCHEDULER_H

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



// Error messages:

#define SYS_ERROR_SIGPROCMASK "system error: sigprocmask failed.\n"
#define SYS_ERROR_SIGEMPTYSET "system error: sigemptyset failure.\n"
#define SYS_ERROR_SIGACTION "system error: sigaction failure.\n"
#define SYS_ERROR_MEMORY_ALLOC "system error: Memory allocation failure.\n"
#define SYS_ERROR_SETITIMER "system error: setitimer failure.\n"
#define TLERROR_INIT_NEGATIVE_QUANTUM "thread library error: Cannot initialize library with negative quantum.\n"
#define TLERROR_SPAWN_NEGATIVE_PRIORITY "thread library error: Cannot spawn thread with negative priority.\n"
#define TLERROR_INIT_NO_QUANTUMS "thread library error: Cannot initialize library with no quantum values.\n"
#define BLOCK_ERR_MSG "thread library error: Cannot block thread with id "
#define RESUME_ERR_MSG "thread library error: Cannot resume thread with id "
#define NON_EXISTENT_THREAD_MSG ": No such thread.\n"
#define QUANTUM_ERR_MSG "thread library error: Cannot get quantum of thread with id "
#define TERMINATION_ERR_MSG "thread library error: Cannot terminate thread with id "
#define CHANGE_PRIORITY_ERR_MSG "thread library error: Cannot change priority of thread with id "
#define ADD_THREAD_ERR_MSG "thread library error: Cannot create new thread with priority "



/*
 * Class representing a user thread.
 */
class Thread
{
public:
	/*
	 * enum representing states the thread can be in (READY includes RUNNING).
	 */
	enum states
	{
		READY,
		BLOCKED,
		TERMINATED
	};

	/*
	 * Pointer to a void function that is an entry point to a thread.
	 */
	typedef void (*EntryPoint_t)();


	/**
	 * contructor for a thread.
	 * @param id ID of this thread.
	 * @param priority Priority this thread should start with.
	 * @param entry Entry point of this thread.
	 * @param mainThread
	 */
	Thread(int id, int priority, EntryPoint_t entry, bool mainThread = false);

	/**
	 * Getter for this thread's environment.
	 */
	sigjmp_buf &getEnvironment();

	/**
	 * Getter for this thread's ID.
	 */
	int getId() const;

	/**
	 * Getter for this thread's priority value.
	 */
	int getPriority() const;

	/**
	 * Setter for this thread's priority.
	 * @param _priority New priority value to give this thread.
	 */
	void setPriority(int _priority);

	/**
	 * Getter for this thread's current state.
	 * @return
	 */
	states getState() const;

	/**
	 * Setter for this threa'd current state.
	 * @param _state The new state for this thread.
	 */
	void setState(states _state);

	/**
	 * Increment the total quantum count for this thread.
	 */
	void incrementTotalQuantum();

	/**
	 * Getter for the total quantum count of this thread.
	 */
	int getTotalQuantum() const;

private:
	int id;
	int totalQuantum;
	int priority;
	states state;
	sigjmp_buf environment;
	std::unique_ptr<char[]> stack;
};

/*
 * A dispatcher object responsible for preforming context-switches between threads.
 */
class Dispatcher
{
public:
	/**
	 * Default constructor for a dispatcher.
	 */
	Dispatcher();

	/**
	 * Preform a context switch from the current thread to the target thread.
	 * @param currentThread Current (running) thread.
	 * @param targetThread Target thread.
	 */
	void switchToThread(std::shared_ptr<Thread> &&currentThread,
						const std::shared_ptr<Thread> &targetThread);
	/**
	 * Getter for the total quantum count (number of context switches preformed by this dispatcher).
	 */
	int getTotalQuantums() const;

private:
	int totalQuantums;
};

/*
 * Singleton Scheduler object responsible for implementing the uthread library functionality.
 * Only one instance of this object can be used at once.
 */
class Scheduler
{
	/*
	 * Pointer to the singleton instance scheduler.
	 */
	static Scheduler *me;

public:

	class SchedulerException: std::exception
	{};

	/**
	 * Constructor for scheduler. Call only once. Further calls will raise SchedulerException.
	 * @param pQuantums mapping between priorities and amount of milliseconds the quantum should
	 * run for.
	 */
	explicit Scheduler(const std::map<int, int> &pQuantums);

	/**
	 * Create a new thread.
	 * @param entryPoint Entry point for this thread.
	 * @param priority The Prioirty this thread should start with.
	 * @return 0 on success, -1 if failed.
	 */
	int addThread(Thread::EntryPoint_t entryPoint, int priority);

	/**
	 * Change the priority of a thread.
	 * @param tid ID of the thread.
	 * @param priority New priority for this thread.
	 * @return 0 on success, -1 if failed.
	 */
	int changePriority(int tid, int priority);

	/**
	 * Terminate the thread with tid.
	 * @param tid ID of the thread to terminate. If ID is 0, program will terminate. If ID belongs
	 * to the currently running thread, function will not return.
	 * @return 0 on success, -1 if failed.
	 */
	int terminate(int tid);

	/**
	 * Block the thread with ID tid.
	 * @param tid ID of the thread to block. Main thread cannot be blocked.
	 * @return 0 on success, -1 if failed.
	 */
	int block(int tid);

	/**
	 * Resume the thread with ID tid.
	 * @param tid ID of the thread to resume.
	 * @return 0 on success, -1 if failed.
	 */
	int resume(int tid);

	/**
	 * Get the ID of the currently running thread.
	 * @return
	 */
	int getRunningId();

	/**
	 * Get the total amount of quantums that have run so far.
	 */
	int getTotalQuantums();

	/**
	 * Get the amount of quantums that the thread with ID tid has run for.
	 * @param tid ID of the thread.
	 * @return Amount of quantums the thread has run for so far.
	 */
	int getThreadsQuantums(int tid);

private:
	std::shared_ptr<Thread> threads[MAX_THREAD_NUM];
	size_t numOfThreads;
	std::map<int, itimerval> quantums;
	std::shared_ptr<Thread> running;
	std::deque<std::shared_ptr<Thread>> ready;
	Dispatcher dispatcher;
	struct sigaction sa = {{nullptr}};

	/**
	 * Release all resources and exit the program.
	 */
	void clearAndExit();

	/**
	 * Handler function for SIGVTALRM. Called by sigaction only.
	 */
	static void timerHandler(int);

	/**
	 * Set the timer for SIGVTALRM for a quantum corresponding to priority.
	 * @param priority priority of the quantum the timer should be set for.
	 */
	void setTimer(int priority);
};


#endif //THREADS_THREADSCHEDULER_H
