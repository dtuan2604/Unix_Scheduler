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


char *log_filename = "logfile.log";
char *prog_name;

static int max_seconds = MAXSECONDS;
static unsigned int next_id = 1;

//shared memory id and pointer
int sid = -1;
int qid = -1;
static struct shmem *shm = NULL;

//define bmap to map to an index with empty process
static unsigned char bmap[processSize/8];

//high-priority queue, low-priority queue, and blocked queue
static struct queue pq[qCOUNT];

//keep track of the number of users running
static unsigned int usersStarted = 0;
static unsigned int usersTerminated = 0;
static unsigned int usersBlocked = 0;

//keep track of the number of log line
static unsigned int logLine = 0;

//define time struct needed in this project
static const struct timespec maxTimeBetweenNewProcs = {.tv_sec = 1, .tv_nsec = 1};
static struct timespec next_start = {.tv_sec = 0, .tv_nsec = 0};
static struct timespec schedulerTurnTime = {.tv_sec = 0, .tv_nsec = 0};
static struct timespec schedulerWaitTime = {.tv_sec = 0, .tv_nsec = 0};
static struct timespec cpuIdleTime = {.tv_sec = 0, .tv_nsec = 0};

//set a variable to hold current ready queues
int qREADY = qHIGH; //initialize current ready queue as high-priority queue

static void helpMenu(){
	printf("Menu\n");
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
      				printf("Max seconds is %d\n",max_seconds);
				break;
    			case 'l':
      				log_filename = optarg;
      				printf("Log file is %s\n",log_filename);
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
static void signalHandler(int sig){
	printf("Signal Handler are triggered\n");
	exit(1);
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
			//execl

			exit(EXIT_FAILURE);
			break;
		default:
			++usersStarted;

			usr->id = next_id++;
			usr->pid = pid;

			usr->t_started = shm->clock; //save started time to record system time
			usr->state = sREADY; //mark process as ready
			
			//pushQ: push to ready queue
			
			++logLine;
			printf("OSS: Generating process with PID %u and putting it in queue %d at time %lu:%lu\n",
           			usr->id, qREADY, shm->clock.tv_sec, shm->clock.tv_nsec);		
			break;	
	}
	
	return EXIT_SUCCESS;

}
static void advanceTimer(){
	struct timespec t = {.tv_sec = 1, .tv_nsec = 0}; //amount to update
	addTime(&shm->clock, &t);

	if ((shm->clock.tv_sec >= next_start.tv_sec) ||((shm->clock.tv_sec == next_start.tv_sec) &&
       		(shm->clock.tv_nsec >= next_start.tv_nsec))){
    		next_start.tv_sec = rand() % maxTimeBetweenNewProcs.tv_sec;
    		next_start.tv_nsec = rand() % maxTimeBetweenNewProcs.tv_nsec;
			
    		addTime(&next_start, &shm->clock);
		if (usersStarted < USERS_MAX){
      			//startUserPCB();
    		}
  	}
}
static void ossSchedule(){
	while(usersTerminated < USERS_MAX){
		advanceTimer();
		//unblock user before scheduling
		//schedule users
		//check output log
	}
	
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
	
	ossSchedule();
		
	return EXIT_SUCCESS;	

}
