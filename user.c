#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "user.h"
#include "oss.h"
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>


static int sid = -1;
static int qid = -1;
static struct shmem *shm = NULL;

char* prog_name;
static int createSHM(){
	sid = shmget(key_shmem, sizeof(struct shmem), 0);
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

        qid = msgget(key_queue,0);
        if(qid == -1){
                fprintf(stderr,"%s: failed to get id for queue. ",prog_name);
                perror("Error");
                return -1;
        }
	return 0;
}

static int deallocateSHM(){
	if(shm != NULL){
                if(shmdt(shm) == -1){
                        fprintf(stderr,"%s: failed to detach shared memory. ",prog_name);
                        perror("Error");
                }
        }
	return 0;
}

static void userProcess(const int IObound){

}

int main(int argc, char** argv){
	prog_name = argv[0];
	if (argc != 2)
	{
		fprintf(stderr, "%s: Please passed in IO bound arguments.\n",prog_name);
		return EXIT_FAILURE;
	}
	
	const int IObound = atoi(argv[1]);
	srand(getpid() + IObound); //seeding off
	
	if(createSHM() == -1)
		return EXIT_FAILURE;
	
	userProcess(IObound);

	if(deallocateSHM() == -1)
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
