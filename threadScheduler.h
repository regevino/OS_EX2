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

typedef void (*EntryPoint_t)();


class Thread
{
public:
	enum states
	{
		READY,
		BLOCKED,
		TERMINATED
	};

	Thread(int id, int priority, EntryPoint_t entry, bool mainThread = false);

	sigjmp_buf &getEnvironment();

	int getId() const;

	int getPriority() const;

	void setPriority(int _priority);

	states getState() const;

	void setState(states _state);

	void incrementTotalQuantum();

	int getTotalQuantum() const;

private:
	int id;
	int totalQuantum;
	int priority;
	states state;
	sigjmp_buf environment;
	std::unique_ptr<char[]> stack;
};


class Dispatcher
{
public:
	Dispatcher();

	void switchToThread(std::shared_ptr<Thread> &&currentThread, const std::shared_ptr<Thread> &targetThread);

private:
	int totalQuantums;
public:
	int getTotalQuantums() const;
};


class Scheduler
{
	static Scheduler *me;
public:
	explicit Scheduler(const std::map<int, int>& pQuantums);

	void setTimer(int priority);

	int addThread(EntryPoint_t entryPoint, int priority);

	static void timerHandler(int sig);
	int changePriority(int tid, int priority);
	int terminate(int tid);

	void clearAndExit();

	int block(int tid);

	int resume(int tid);

	int getRunningId();

	int getTotalQuantums();

	int getThreadsQuantums(int tid);

private:
	std::shared_ptr<Thread> threads[MAX_THREAD_NUM];
	size_t numOfThreads;
	std::map<int, itimerval> quantums;
	std::shared_ptr<Thread> running;
	std::deque<std::shared_ptr<Thread>> ready;
	Dispatcher dispatcher;
	struct sigaction sa = {{nullptr}};
};




#endif //THREADS_THREADSCHEDULER_H
