#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "oss.h"

char *log_filename = "logfile.log";
char *prog_name;

static int max_seconds = MAXSECONDS;

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
		perror("stdout");
		return -1;
	}
	return 0;
}


int main(int argc, char** argv){
	parseOpt(argc, argv);
	return EXIT_SUCCESS;	

}
