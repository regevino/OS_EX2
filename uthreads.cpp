//
// Created by Dan Regev on 4/22/2020.
//

#include "uthreads.h"
#include <map>
#include <queue>
#include <memory>
#include <setjmp.h>

#include <signal.h>

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
		BLOCKED
	};

	Thread(int id, int priority, EntryPoint_t entry)
			: id(id), priority(priority), state(READY), stack(new char[STACK_SIZE]), entry(entry)
	{
		address_t sp = (address_t) stack.get() + STACK_SIZE - sizeof(address_t);
		auto pc = (address_t) entry;
		sigsetjmp(environment, 1);
		(environment->__jmpbuf)[JB_SP] = translate_address(sp);
		(environment->__jmpbuf)[JB_PC] = translate_address(pc);
		sigemptyset(&environment->__saved_mask);
	}

	sigjmp_buf &getEnvironment()
	{
		return environment;
	}

private:
	int id;
	int quantum;
	int totalQuantum;
	int priority;
	states state;
	sigjmp_buf environment;
	std::unique_ptr<char[]> stack;
	EntryPoint_t entry;
};


class Scheduler
{
public:

private:
	std::map<int, std::shared_ptr<Thread>> threads;
	std::shared_ptr <Thread> running;
	std::queue <std::shared_ptr<Thread>> ready;
};


class Dispatcher
{
public:
	void switchToThread(std::shared_ptr <Thread> currentThread,
						std::shared_ptr <Thread> targetThread)
	{
		int ret_val = sigsetjmp(currentThread->getEnvironment(), 1);
		if (ret_val == 1)
		{
			return;
		}
		siglongjmp(targetThread->getEnvironment(), 1);
	}

private:

};
//#include <stdio.h>
//#include <setjmp.h>
//#include <unistd.h>
//#include <sys/time.h>
//
//#define SECOND 1000000
//
//char stack1[STACK_SIZE];
//char stack2[STACK_SIZE];
//
//sigjmp_buf env[2];


//void switchThreads(void)
//{
//    static int currentThread = 0;
//
//    int ret_val = sigsetjmp(env[currentThread], 1);
//    printf("SWITCH: ret_val=%d\n", ret_val);
//    if (ret_val == 1)
//    {
//        return;
//    }
//    currentThread = 1 - currentThread;
//    siglongjmp(env[currentThread], 1);
//}