#ifndef OSS_H
#define OSS_H

#define MAXSECONDS 3
#define processSize 18

#define H_TIMESLICE 1000000
#define L_TIMESLICE 10000000
#define MAXDISPATCH 10000 // Maximum dispatch time
#define USERS_MAX 50
#define LOG_LINES 10000

#define IO_BOUND_PROB 30

//define key for shared memory
const key_t key_shmem = 2704;
const key_t key_queue = 1708;

//define states for process
enum state{
	sNEW = 0,
	sREADY,
	sBLOCKED,
	sTERMINATED
};

//define message format to communicate between scheduler and child process
struct ossMsg{
	long mtype;
	pid_t from;
	
	int timeslice;
	
	struct timespec clock;
	struct timespec io;

};
#define MESSAGE_SIZE (sizeof(struct ossMsg) - sizeof(long))

//mark queue types: high-priority, low-priority, and blocked
enum qTypes{
	qHIGH = 0,
	qLOW,
	qBLOCKED,
	qCOUNT
};

//define queue
struct queue{
	unsigned int ids[processSize];
	unsigned int len;
};

//define process control block
struct userPCB{
	pid_t pid;
	int priority;
	unsigned int id;
	enum state state;

	struct timespec t_cpu;
	struct timespec t_sys;
	struct timespec t_burst;
	struct timespec t_blocked;
	struct timespec t_started;
};

//define shared memory
struct shmem{
	struct timespec clock;
	struct userPCB users[processSize];
};

//define oss report
struct ossReport{
	struct timespec t_wait;
	struct timespec t_sys;
	struct timespec t_cpu;
	struct timespec t_blocked;
}; 


#endif
