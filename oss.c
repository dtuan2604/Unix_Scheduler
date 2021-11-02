#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "oss.h"
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <strings.h>
#include <signal.h>

char *log_filename = "logfile.log";
char *prog_name;

static int max_seconds = MAXSECONDS;
static unsigned int next_id = 1;

//shared memory id and pointer
static int sid = -1;
static int qid = -1;
static struct shmem *shm = NULL;

//define bmap to map to an index with empty process
static unsigned char bmap[processSize/8];

//high-priority queue, low-priority queue, and blocked queue
static struct queue pq[qCOUNT];

//keep track of the number of log line
static unsigned int logLine = 0;

//define time struct needed in this project
static const struct timespec maxTimeBetweenNewProcs = {.tv_sec = 2, .tv_nsec = 1000000000};
static struct timespec next_start = {.tv_sec = 0, .tv_nsec = 0};

//set a variable to hold current ready queues
static int qREADY = qHIGH; //initialize current ready queue as high-priority queue

//create an array of ossReport
static struct ossReport pReport;

static void helpMenu(){
	printf("This program is trying to stimulate an Operating System scheduler.\n");
	printf("The program will fork many children processes until it reachs to maximum\n");
	printf("number defined in the header file. Meanwhile, it also allocates time to\n");
	printf("try to put a process in ready queu, dispatch it, put it back or blocked it.\n");
	printf("Additionally, this program also stimulate a clock so that we could check\n");
	printf("our scheduler is working correctly or not.\n");
}

static int parseOpt(int argc,char** argv){
	int opt;
	prog_name = argv[0];
	
  	while ((opt = getopt(argc, argv, "hs:l:")) > 0){
    		switch (opt){
    			case 'h':
      				helpMenu();
     		 		return -1;
    			case 's':
 	     			max_seconds = atoi(optarg);
				break;
    			case 'l':
      				log_filename = optarg;
				break;
			case '?':
                                if(optopt == 's' || optopt == 'l')
                                	fprintf(stderr,"%s: ERROR: -%c without argument\n",prog_name,optopt);
                                else
                                        fprintf(stderr, "%s: ERROR: Unrecognized option: -%c\n",prog_name,optopt);
                                return -1;

    			default:
      				helpMenu();
      				return -1;
    		}
  	}
	
	stdout = freopen(log_filename, "w", stdout);
	if(stdout == NULL){
		fprintf(stderr,"%s: ",prog_name);
		perror("freopen");
		return -1;
	}
	return 0;
}
static void initOssReport(){
	pReport.c_highprior = 0;
	pReport.c_lowprior = 0;
	pReport.usersStarted = 0;
	pReport.usersTerminated = 0;
	pReport.t_wait.tv_sec = 0;
	pReport.t_wait.tv_nsec = 0;
        pReport.t_cpu.tv_sec = 0;
        pReport.t_cpu.tv_nsec = 0;
        pReport.t_sys.tv_sec = 0;
        pReport.t_sys.tv_nsec = 0;
	pReport.cpuIdleTime.tv_sec = 0;
        pReport.cpuIdleTime.tv_nsec = 0;
        pReport.t_blocked[qHIGH].tv_sec = 0;
        pReport.t_blocked[qHIGH].tv_nsec = 0;
	pReport.t_blocked[qLOW].tv_sec = 0;
        pReport.t_blocked[qLOW].tv_nsec = 0;	

}
static int createSHM(){
	sid = shmget(key_shmem, sizeof(struct shmem), IPC_CREAT | IPC_EXCL | S_IRWXU);
	if(sid < 0){
		fprintf(stderr,"%s: failed to get id for shared memory. ",prog_name);
                perror("Error");
                return -1;
	}
	
	shm = (struct shmem *)shmat(sid,NULL,0);
	if(shm == (void *)-1){
		fprintf(stderr,"%s: failed to get pointer for shared memory. ",prog_name);
                perror("Error");
                return -1;
	}
	
	qid = msgget(key_queue, IPC_CREAT | IPC_EXCL | 0666);
	if(qid == -1){
                fprintf(stderr,"%s: failed to get id for queue. ",prog_name);
                perror("Error");
                return -1;
        }
	
	return 0;

}
static void deallocateSHM(){
	if(shm != NULL){
		if(shmdt(shm) == -1){
			fprintf(stderr,"%s: failed to detach shared memory. ",prog_name);
                	perror("Error");
		}
	}
	if(sid != -1){
		if(shmctl(sid, IPC_RMID, NULL) == -1){
			fprintf(stderr,"%s: failed to delete shared memory. ",prog_name);
                        perror("Error");
		}
	}
	if(qid != -1){
		if(msgctl(qid, IPC_RMID, NULL) == -1){
			fprintf(stderr,"%s: failed to delete message queue.",prog_name);
                        perror("Error");
		}	
	}
}
static void addTime(struct timespec *a, const struct timespec *b){
	//function to add time to the clock	
	static const unsigned int max_ns = 1000000000;

  	a->tv_sec += b->tv_sec;
  	a->tv_nsec += b->tv_nsec;
  	if (a->tv_nsec > max_ns)
  	{
    		a->tv_sec++;
    		a->tv_nsec -= max_ns;
  	}
}

static void subTime(struct timespec *a, struct timespec *b, struct timespec *c){
  	//function to find time difference 
	if (b->tv_nsec < a->tv_nsec){
    		c->tv_sec = b->tv_sec - a->tv_sec - 1;
    		c->tv_nsec = a->tv_nsec - b->tv_nsec;
  	}else{
    		c->tv_sec = b->tv_sec - a->tv_sec;
    		c->tv_nsec = b->tv_nsec - a->tv_nsec;
  	}
}
static void divTime(struct timespec *a, const int d){
  	a->tv_sec /= d;
  	a->tv_nsec /= d;
}
static int checkBmap(const int byte, const int n){
  	//check a bit in byte
	return (byte & (1 << n)) >> n;
}
static void toggleBmap(const int u){
  	//mark the bitmap as used
	int byte = u / 8;
  	int mask = 1 << (u % 8);

  	bmap[byte] ^= mask;
}
static int getFreeBmap(){
	//get a free bit from bitmap
	int i;

  	for (i = 0; i < processSize; ++i){

    		int byte = i / 8;
    		int bit = i % 8;

    		if (checkBmap(bmap[byte], bit) == 0){
      			toggleBmap(i);
      			return i;
    		}
  	}
  	return -1;
}
static int pushQ(const int qid, const int bit){
	struct queue *q = &pq[qid];
  	q->ids[q->len++] = bit;
  	return qid;

}

static int popQ(struct queue *pq, const int pos){
	unsigned int i;
  	unsigned int u = pq->ids[pos];
	
	//Pop the items and then shift the rest to the left
  	for (i = pos; i < pq->len - 1; i++){
    		pq->ids[i] = pq->ids[i + 1];
  	}
  	return u;

}

static int startUserPCB(){
	char buf[10];
	const int u = getFreeBmap();
	if(u == -1)
		return EXIT_SUCCESS;
	struct userPCB *usr = &shm->users[u]; //allocate process control block for user process

	//generate random to determine if the user process is IO bound or CPU bound
	const int io_bound = ((rand() % 100) <= IO_BOUND_PROB) ? 1 : 0;

	const pid_t pid = fork();

	switch(pid){
		case -1:
			fprintf(stderr,"%s: failed to fork a process.",prog_name);
                        perror("Error");
			return EXIT_FAILURE;
			break;
		case 0: 
			snprintf(buf, sizeof(buf), "%d", io_bound);
			execl("./user", "./user", buf, NULL);
			fprintf(stderr,"%s: failed to execl.",prog_name);
                        perror("Error");			
			exit(EXIT_FAILURE);
			break;
		default:
			++pReport.usersStarted;
			if(io_bound == 1){
				usr->priority = qHIGH;
				pReport.c_highprior++;
			}else{
				usr->priority = qLOW;
				pReport.c_lowprior++;
			}		
		
			usr->id = next_id++;
			usr->pid = pid;

			usr->t_started = shm->clock; //save started time to record system time
			usr->state = sREADY; //mark process as ready
			
			pushQ(qHIGH, u); 
			
			++logLine;
			printf("OSS: Generating process with PID %u and putting it in queue %d at time %lu:%lu\n",
           			usr->id, qHIGH, shm->clock.tv_sec, shm->clock.tv_nsec);		
			break;	
	}
	
	return EXIT_SUCCESS;

}
static void advanceTimer(){
	struct timespec t = {.tv_sec = 0, .tv_nsec = 500000000}; //amount to update

	if(pReport.usersStarted >= USERS_MAX)
		addTime(&shm->clock, &t);
	else{
		if ((shm->clock.tv_sec >= next_start.tv_sec) ||((shm->clock.tv_sec == next_start.tv_sec) &&
       			(shm->clock.tv_nsec >= next_start.tv_nsec))){
    			next_start.tv_sec = rand() % maxTimeBetweenNewProcs.tv_sec;
    			next_start.tv_nsec = rand() % maxTimeBetweenNewProcs.tv_nsec;
			
    			addTime(&next_start, &shm->clock);
      			startUserPCB();
  		}else{
				addTime(&shm->clock, &t);
		}
	}
}

static void unblockUsers(){
	unsigned int i;
  	for (i = 0; i < pq[qBLOCKED].len; ++i){
		//get first block index
		const int u = pq[qBLOCKED].ids[i];
		struct userPCB *usr = &shm->users[u];

		//check if it is time to unblock
		if ((usr->t_blocked.tv_sec < shm->clock.tv_sec) ||
        		((usr->t_blocked.tv_sec == shm->clock.tv_sec) &&
         		(usr->t_blocked.tv_nsec <= shm->clock.tv_nsec))){
			//mark user states as ready
			usr->state = sREADY;
      			usr->t_blocked.tv_sec = 0;
      			usr->t_blocked.tv_nsec = 0;
			
			++logLine;
			printf("OSS: Unblocked PID %d at %lu:%lu and put it in the back of queue %d\n", usr->id, shm->clock.tv_sec, shm->clock.tv_nsec, usr->priority);

			//pop from blocked queue
			popQ(&pq[qBLOCKED], i);
		        pq[qBLOCKED].len--;
		
			//push at the end of its ready queue
			pushQ(usr->priority, u);
		}
	
	}
}
static void stopUserProcess(){
	struct ossMsg m;

	int i;
	for (i = 0; i < processSize; i++){
		if (shm->users[i].pid > 0){
      			m.timeslice = 0;
      			m.mtype = shm->users[i].pid;
      			m.from = getpid();
      			if (msgsnd(qid, (void *)&m, MESSAGE_SIZE, 0) == -1){
        			break;
      			}
			
			waitpid(shm->users[i].pid, NULL, 0); //wait for the process to exit
      			shm->users[i].pid = 0;
		}
	}

}
static void clearUserPCB(const int u){
	struct timespec res;
  	struct userPCB *usr = &shm->users[u];

  	++pReport.usersTerminated;
	//record the CPU time
	addTime(&pReport.t_cpu,&usr->t_cpu);	

	//get the system time
	subTime(&usr->t_started, &shm->clock, &usr->t_sys);
	addTime(&pReport.t_sys, &usr->t_sys);

	//get wait time
	subTime(&usr->t_cpu, &usr->t_sys, &res);
	addTime(&pReport.t_wait, &res);	

	waitpid(shm->users[u].pid, NULL, 0);
	bzero(usr, sizeof(struct userPCB)); //Clear all data
	
	usr->state = sNEW;
	toggleBmap(u); //mark the usr as free
}
static int scheduleRunUser(){
	struct ossMsg m;
	static struct timespec dispatch = {.tv_sec = 0, .tv_nsec = 0};
	dispatch.tv_nsec = rand() % MAXDISPATCH;
	addTime(&shm->clock, &dispatch);

	//qREADY is currently assigned to either qLOW or qHIGH from this function's parent
	const int u = popQ(&pq[qREADY], 0);
	pq[qREADY].len--;

	struct userPCB *usr = &shm->users[u];
	bzero(&m, sizeof(struct ossMsg));
	
	++logLine;
  	printf("OSS: Dispatching process with PID %u from queue %d at time %lu:%lu\n", usr->id, qREADY, shm->clock.tv_sec, shm->clock.tv_nsec);
	
	++logLine;
  	printf("OSS: total time this dispatch was %lu nanoseconds\n", dispatch.tv_nsec);
	
	//Evaluate if the popped queue is either high or low priority
	const int TIMESLICE = ((qREADY == qHIGH) ? H_TIMESLICE : L_TIMESLICE);
	m.timeslice = TIMESLICE;
  	m.mtype = usr->pid;
  	m.from = getpid();

	//Send message to the queue
	if ((msgsnd(qid, (void *)&m, MESSAGE_SIZE, 0) == -1) ||
      		(msgrcv(qid, (void *)&m, MESSAGE_SIZE, getpid(), 0) == -1)){
    		fprintf(stderr,"%s: Couldn't send and receive message. ",prog_name);
		perror("Error");
    		return -1;
  	}
	
	const int new_state = m.timeslice;
	switch (new_state){
		case sREADY:
			usr->state = sREADY;
			
			//Getting burst time
			usr->t_burst.tv_sec = 0;
    			usr->t_burst.tv_nsec = m.clock.tv_nsec;

			//add burst time
			addTime(&shm->clock, &usr->t_burst); 
			
			//update how long user ran on cpu
			addTime(&usr->t_cpu, &usr->t_burst);
			

			++logLine;
    			printf("OSS: Receiving that process with PID %u ran for %lu nanoseconds\n", usr->id, usr->t_burst.tv_nsec);
			
			++logLine;
    			printf("OSS: Putting process with PID %u into end of queue %d\n", usr->id, usr->priority);
    			pushQ(usr->priority, u);
			break;
		case sBLOCKED:
    			usr->state = sBLOCKED;
			
			usr->t_burst.tv_sec = 0;
    			usr->t_burst.tv_nsec = m.clock.tv_nsec;			
			addTime(&shm->clock, &usr->t_burst);
			addTime(&usr->t_cpu, &usr->t_burst);
			
			//Get blocked time from the message from the child process
			usr->t_blocked.tv_sec = m.io.tv_sec;
    			usr->t_blocked.tv_nsec = m.io.tv_nsec;
			
			//Update blocked time in the OSS Report
			addTime(&pReport.t_blocked[usr->priority], &m.io);

			addTime(&usr->t_blocked, &shm->clock); //add clock to wait time
			
			++logLine;
                        printf("OSS: Receiving that process with PID %u ran for %lu nanoseconds\n", usr->id, usr->t_burst.tv_nsec);

			if (m.clock.tv_nsec != TIMESLICE){
                                ++logLine;
                                printf("OSS: not using its entire quantum\n");
                        }

			++logLine;
                        printf("OSS: Receiving that process with PID %u is blocking for event(%li:%li)\n",
                                usr->id, usr->t_blocked.tv_sec, usr->t_blocked.tv_nsec);

			//insert to blocked queue
			++logLine;
    			printf("OSS: Putting process with PID %u into blocked queue %d\n", usr->id, qBLOCKED);
    			pushQ(qBLOCKED, u);
    			break;	
		case sTERMINATED:
			usr->state = sTERMINATED;
    			
			usr->t_burst.tv_sec = 0;
    			usr->t_burst.tv_nsec = m.clock.tv_nsec;
			addTime(&shm->clock, &usr->t_burst);
			addTime(&usr->t_cpu, &usr->t_burst); //add burst to total cpu time
			
			
			++logLine;
                        printf("OSS: Receiving that process with PID %u ran for %lu nanoseconds\n", usr->id, usr->t_burst.tv_nsec);

                        if (m.clock.tv_nsec != TIMESLICE){
                                ++logLine;
                                printf("OSS: not using its entire quantum\n");
                        }
			
			++logLine;
    			printf("OSS: Receiving that process with PID %u has terminated\n", usr->id);
			
			clearUserPCB(u);
			break;
		default:
			printf("OSS: Invalid response from user\n");
    			break;
	}
	return 0;
	
	
}
static int runChildProcess(){
	//static struct timespec t_idle = {.tv_sec = 0, .tv_nsec = 0};
	static struct timespec t_idle;
	static struct timespec diff_idle;
	static int flag = 0;

	if(pq[qHIGH].len == 0){
		if(flag == 0){
			++logLine;
			printf("OSS: No ready process in queue %d at %li:%li\n", qHIGH, shm->clock.tv_sec, shm->clock.tv_nsec);
			t_idle = shm->clock;
			flag = 1;
		}
       	}else{
		qREADY = qHIGH;
		flag = 0;
		if(t_idle.tv_sec != 0 && t_idle.tv_nsec != 0){
			subTime(&t_idle, &shm->clock, &diff_idle);
			addTime(&pReport.cpuIdleTime, &diff_idle);
			t_idle.tv_sec = 0;
                	t_idle.tv_nsec = 0;
		}
	}
	
	//which means the high-priority queue is currently empty
	if(flag == 1){
		if(pq[qLOW].len == 0){
			++logLine;
                	printf("OSS: No ready process in queue %d at %li:%li\n", qLOW, shm->clock.tv_sec, shm->clock.tv_nsec);
			flag = 2;
		}else{
			qREADY = qLOW;
			flag = 0;
			if(t_idle.tv_sec != 0 && t_idle.tv_nsec != 0){
	                        subTime(&t_idle, &shm->clock, &diff_idle);
        	                addTime(&pReport.cpuIdleTime, &diff_idle);
                	        t_idle.tv_sec = 0;
                        	t_idle.tv_nsec = 0;
                	}
		}
	
	}
	
	//which means both ready queue are empty at the moment
	if(flag == 2){
		if(pq[qBLOCKED].len == 0 && pReport.usersTerminated >= USERS_MAX){
                        ++logLine;
                        printf("OSS: No blocked process in queue %d at %li:%li\n", qBLOCKED, shm->clock.tv_sec, shm->clock.tv_nsec);
                        ++logLine;
                        printf("OSS: The operating system will terminate right away\n");
                        return -1; //return -1 to let OSS know there isn't any more process to schedule
		}else{
			return 0;
		}
	}

	scheduleRunUser();	
	return 0;
}
static void checkLog(){
	if(logLine >= LOG_LINES){
		printf("OSS: Current log has exceed %d lines (%d lines), the system will terminate now\n",LOG_LINES, logLine);
		raise(SIGINT);
	}
}
static void ossSchedule(){
	while(pReport.usersTerminated < USERS_MAX){
		advanceTimer();
		unblockUsers();
		if(runChildProcess() == -1) //there isn't any more processes to schedule
			break;
		checkLog();
	}
	
}
static void printReport(){
	divTime(&pReport.t_cpu, pReport.usersTerminated);
	divTime(&pReport.t_wait, pReport.usersTerminated);
	divTime(&pReport.t_sys, pReport.usersTerminated);
	divTime(&pReport.t_blocked[qHIGH], pReport.c_highprior);
	divTime(&pReport.t_blocked[qLOW], pReport.c_lowprior);
	printf("*****************************************************\n");
	printf("\t\tSCHEDULING REPORT\n");
	printf("Processes Statistic:\n");
	printf("\tNumber of started processes: %d\n", pReport.usersStarted);
	printf("\tNumber of terminated processes: %d\n", pReport.usersTerminated);
	printf("\tNumber of IO bound processes: %d\n", pReport.c_highprior);
	printf("\tNumber of CPU bound processes: %d\n", pReport.c_lowprior);
	printf("\tCPU idle time: %lu:%lu\n",pReport.cpuIdleTime.tv_sec,pReport.cpuIdleTime.tv_nsec);
	printf("Average Records:\n");
	printf("\tAverage CPU utilization time: %lu:%lu\n",pReport.t_cpu.tv_sec,pReport.t_cpu.tv_nsec);
	printf("\tAverage wait time: %lu:%lu\n",pReport.t_wait.tv_sec,pReport.t_wait.tv_nsec);
	printf("\tAverage time in the system: %lu:%lu\n",pReport.t_sys.tv_sec,pReport.t_sys.tv_nsec);
	printf("\tAverage blocked time:\n");
	printf("\t\tIO Bound Processes: %lu:%lu\n",pReport.t_blocked[qHIGH].tv_sec,pReport.t_blocked[qHIGH].tv_nsec);
	printf("\t\tCPU Bound Processes: %lu:%lu\n",pReport.t_blocked[qLOW].tv_sec,pReport.t_blocked[qLOW].tv_nsec);
	printf("*****************************************************\n");
}
static void signalHandler(int sig){
        printf("OSS: Signaled with %d\n", sig);
     	stopUserProcess();
	printReport();
	exit(1);
}
int main(int argc, char** argv){
	prog_name = argv[0];
	
	signal(SIGINT, signalHandler);
	signal(SIGALRM, signalHandler);

	srand(getpid());

	if((parseOpt(argc, argv) < 0) || (createSHM() < 0)){
		return EXIT_FAILURE;
	}
	
	//clear shared memory, bitmap, and queues
	bzero(shm, sizeof(struct shmem));
	bzero(bmap, sizeof(bmap));
	bzero(pq, sizeof(struct queue)*qCOUNT);
	
	alarm(max_seconds);
	atexit(deallocateSHM);		
	
	initOssReport();	
	ossSchedule();
	
	printReport();
	return EXIT_SUCCESS;	

}
