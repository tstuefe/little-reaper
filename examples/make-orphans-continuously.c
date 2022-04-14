#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>



static void LOG(const char* msg, ...) {
	printf("[%d]: ", getpid());
	va_list argp;
	va_start(argp, msg);
	vprintf(msg, argp);
	va_end(argp);
	fputc('\n', stdout);
	fflush(stdout);
}

int main(int argc, char** argv) {

	if (argc != 4) {
		printf("Use: make-orphans-continuously <num> <orphan lifetime (seconds)> <interval seconds>\n");
		exit(-1);
	}

	const int num = atoi(argv[1]);
	int orphan_lifetime_secs = atoi(argv[2]);
	int interval_seconds = atoi(argv[3]);

	if (interval_seconds == 0) {
		interval_seconds = 1;
	}

	if (orphan_lifetime_secs == 0) {
		orphan_lifetime_secs = 1;
	}

	LOG("Parent started");

	for (;;) {
		// create one child, which creates n grandchilds; child exits immediately. Grandchilds live on as orphans.
		pid_t child = fork();
		if (child == 0) {
			// Child
			for (int n = 0; n < num; n++) {
				pid_t grandchild = fork();
				if (grandchild == 0) {
					// Grandchild
					LOG("I'm an Orphan");
					sleep(orphan_lifetime_secs);
					LOG("Orphan terminates (my reaper would be: %d).", getppid());
					// Grandchild dies
					exit(0);
				}
			}
			// Child dies
			exit(0);
		} else {
			// Parent reaps child
			int status;
			int rc = waitpid(child, &status, 0);
			sleep(interval_seconds);
		}
	}
}
