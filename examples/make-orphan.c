#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char** argv) {

	int secs = 0;
	if (argc == 2) {
		secs = atoi(argv[1]);
	}
	if (secs == 0) {
		secs = 10;
	}

	pid_t child = fork();
	if (child) {
		// I'm the paren
		printf("[parent]: me,child: %d,%d\n", getpid(), child);
		printf("[parent]: exit\n");
	} else {
		// I'm the child
		sleep(2); // let parent die...
		pid_t parent_now = getppid();
		printf("[child]: me,parent now: %d,%d\n", getpid(),getppid());
		printf("[child]: will sleep now for %d seconds...\n", secs);
		sleep(secs);
		printf("[child]: exit\n");
	}
	return 0;

}
