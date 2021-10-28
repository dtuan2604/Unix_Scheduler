#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "oss.h"
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/shm.h>

char *log_filename = "logfile.log";
char *prog_name;

static int max_seconds = MAXSECONDS;

//shared memory id and pointer
int sid = -1;
int qid = -1;
static struct shmem *shm = NULL;

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

int main(int argc, char** argv){
	prog_name = argv[0];
	
	signal(SIGINT, signalHandler);

	srand(getpid());
	if((parseOpt(argc, argv) < 0) || (createSHM() < 0)){
		return EXIT_FAILURE;
	}
	deallocateSHM();		
	return EXIT_SUCCESS;	

}
