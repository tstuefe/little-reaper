#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char** argv) {
	if (argc != 3) {
		printf("Use: make-orphans <num> <orphan lifetime (seconds)>\n");
		exit(-1);
	}
	const int num = atoi(argv[1]);
	const int secs = atoi(argv[2]);
	printf("I'm %d\n", getpid());
	printf("Will create %d Orphans which will each live %d seconds\n", num, secs);

	for (int i = 0; i < num; i ++) {
		pid_t c = fork();
		if (c == 0) {
			sleep(secs);
			exit(0);
		} else {
			printf("%c%d", (i > 0 ? ',' : ' '), c);
			fflush(stdout); // prevent repeated output from cloned output buffers
		}
	}
	printf("\n");
	fflush(stdout);
	return 0;

}
