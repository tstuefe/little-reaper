#include <sys/types.h>
#include <sys/prctl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>


#define VERSION "1.0.1"

static int verbose = 0;

// How much time we give children to terminate before terminating ourselves.
static const int shutdown_timeout_seconds = 5;

static pid_t command_pid = -1;

////////////////// logging        ////////////////////////////////////

// Note: LOG is signal safe, LOGf is not.
#define WRITE_LITERAL_STRING(s) 	{ write(STDOUT_FILENO, s, sizeof(s)); } 
#define LOG(msg)  					WRITE_LITERAL_STRING("tinyreaper: " msg "\n")

static void LOGf(const char* fmt, ...) {
	WRITE_LITERAL_STRING("tinyreaper: ");
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	fflush(stdout);
}

#define VERBOSEf(fmt, ...) 	if (verbose) { LOGf(fmt, __VA_ARGS__); }
#define VERBOSE(msg) 		if (verbose) { LOG(msg); }

static void print_usage() {
	printf("tinyreaper [Options] <command> [<command arguments>]\n");
	printf("\n");
	printf("Registers itself as sub reaper for child processes, then starts <command>.\n");
	printf("\n");
	printf("Options:\n");
    printf("`-v`: verbose mode\n");
	printf("`-V`: version\n");
	printf("`-h`: this help\n");
}

// Signal safe writing of a decimal number
static void write_num(unsigned n) {
	int a = n;
	if (n > 9) {
		write_num(n/10);
	}
	const char* const digits = "0123456789";
	write(STDOUT_FILENO, digits + (a % 10), 1);
}

// Signal safe logging of process exit status
static void LOG_process_state(pid_t pid, int status) {
	if (pid > 0) {
		if (WIFEXITED(status)) {
			WRITE_LITERAL_STRING("child ");
			write_num((unsigned)pid);
			WRITE_LITERAL_STRING(" exited with ");
			write_num((unsigned)WEXITSTATUS(status));
			WRITE_LITERAL_STRING("\n");
		} else if (WIFSIGNALED(status)) {
			WRITE_LITERAL_STRING("child ");
			write_num((unsigned)pid);
			WRITE_LITERAL_STRING(" terminated with ");
			write_num((unsigned)WTERMSIG(status));
			WRITE_LITERAL_STRING("\n");
		}
	}
}


////////////////// Child handling ////////////////////////////////////

static void send_signal_to_all_children(int sig) {
	// We send a signal to the process group, which includes ourselves.
	// We filter out signals from ourselves in the signal handler.
	if (getpgrp() == getpid()) { // sanity check, am I really process group leader?
		kill(0, sig);
	}
}

////////////////// signal handling ////////////////////////////////////

static volatile sig_atomic_t shutdown_in_progress = 0;

static void start_shutdown() {
	if (shutdown_in_progress) {
		VERBOSE("Shutdown already in progress.");
		return;
	}
	shutdown_in_progress = 1;
	// send SIGTERM to all kids, then start the death clock.
	LOG("Terminating children...");
	send_signal_to_all_children(SIGTERM);

	VERBOSE("tick tock...");
	alarm(shutdown_timeout_seconds);
}

// Note: called from signal handler.
static void handle_alarm() {
	// We receive this signal because we set the alarm after getting
	// a termination request, and we timeouted. So here we exit
	// right away.
	if  (shutdown_in_progress) {
		LOG("Shutdown timeout. Terminating.");
		exit(-1);
	}
}

static void signal_handler(int sig, siginfo_t* siginfo, void* context) {

	// Ignore SIGTERM send by myself to myself (see send_signal_to_all_children)
	if (sig == SIGTERM && siginfo->si_pid == getpid()) {
		VERBOSE("Ignoring SIGTERM sent by myself.");
		return;
	}
	
	if (verbose) {
		WRITE_LITERAL_STRING("Signal: ");
		write_num(sig);
		WRITE_LITERAL_STRING("\n");
	}

	switch (sig) {
		case SIGTERM:
		case SIGINT:
		case SIGQUIT:
			start_shutdown();
			break;
		case SIGALRM:
			handle_alarm();
			break;			
	}
}

static void initialize_signal_handler() {
	struct sigaction sa;
	sa.sa_sigaction = signal_handler;
	sigemptyset(&(sa.sa_mask));
	sa.sa_flags = SA_SIGINFO;
	const int sigs[] = { SIGTERM, SIGINT, SIGQUIT, SIGALRM, -1 };
	for (int i = 0; sigs[i] != -1; i ++) {
		if (sigaction(sigs[i], &sa, NULL) == -1) {
			LOGf("Failed to install signal handler for %d - errno: %d (%s)",
			     sigs[i], errno, strerror(errno));
		}
	}
}

////////////////// misc stuff ////////////////////////////////////

// Make process a sub reaper for all direct and indirect children
static void make_me_a_reaper() {
	int rc = prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0);
	if (rc == -1) {
		LOGf("Failed to set sub reaper state - errno: %d (%s)", errno, strerror(errno));
		LOG("Note: Will not adopt orphans.");
	}
}

////////////////// main ////////////////////////////////////

int main(int argc, char** argv) {
	
	// parse arguments
	int start_command = -1;
	for (int i = 1; i < argc && start_command == -1; i ++) {
		if (argv[i][0] == '-') {
			int len = (int)strlen(argv[i]);
			if (len == 1) {
				LOG("Missing option");
				print_usage();
				exit(-1);
			}
			for (int letter = 1; letter < len; letter ++) {
				VERBOSEf("read flag: %c", argv[i][letter]);
				switch (argv[i][letter]) {
				case 'v': 
					verbose = 1;
					break;
				case 'V': 
					LOG("version: " VERSION);
					exit(0);
					break;
				case 'h': 
					print_usage();
					exit(0);
					break;
				default: 
					LOGf("Unknown flag: %c", argv[i][letter]);
					print_usage();
					exit(-1);
				}
			}
		} else {
			start_command = i;	
		}
	}

	if (start_command == -1) {
		LOG("Missing command");
		print_usage();
		exit(-1);
	}

	// Make me process group leader
	setpgrp();

	// Make us subreaper
	make_me_a_reaper();
	
	// Init handler to handle SIGTERM and SIGINT
	initialize_signal_handler();
	
	// assemble NULL-terminated argument vector for exec
	int child_argc = argc - start_command;
	char** child_argv = (char**) malloc((child_argc + 1) * sizeof(char*));
	for (int i = start_command; i < argc; i ++) {
		child_argv[i - start_command] = argv[i];
	}
	child_argv[child_argc] = NULL;

	VERBOSEf("tinyreaper (pid: %d, parent: %d, pgrp: %d)", getpid(), getppid(), getpgrp());

	// fork, then exec <command>
	command_pid = fork();

	if (command_pid == 0) {
		// --- Child ---
		int rc = execv(child_argv[0], child_argv);
		if (rc == -1) {
			LOGf("Failed to exec \"%s\" - errno: %d (%s)",
                             child_argv[0], errno, strerror(errno));
			exit(-1);
		}
	} else {
		// --- Parent ---
		int command_status = 0;
		for (;;) {
			int status;
			pid_t child = wait(&status);
			if (child > 0) {
				LOG_process_state(child, status);
				if (child == command_pid) {
					VERBOSEf("%s finished.", child_argv[0]);
					command_status = status;
					// The command finished.
					// We now terminate any remaining children and continue to wait
					// until they finish too. We also set a death clock. If all children
					// finish in time, or if there are no remaining children, we will leave
					// the loop and exit. If children remain, we will eventually run out of
					// time and terminate ourselves.
					start_shutdown();
				}
			} else if (child == (pid_t) -1 && errno == ECHILD) {
				VERBOSE("all child processes terminated.");
				break;
			}
		}

		// We return -1 if <command> was terminated by signal, or if its exit status was != 0
		int rc = ((WIFEXITED(command_status) && WEXITSTATUS(command_status) != 0) || 
		           WIFSIGNALED(command_status)) ? -1 : 0;
		VERBOSEf("Returning %d", rc);

		exit(rc);
	}

	return 0;
}
