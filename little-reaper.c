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
#include <wait.h>

static int verbose = 0;
static int wait_for_all_children = 0;
static int terminate_all_children_after_command_terminated = 0;

// How much time we give children to terminate before terminating ourselves.
static const int shutdown_timeout_seconds = 5;

static pid_t command_pid = -1;

////////////////// logging        ////////////////////////////////////

// Note: LOG is signal safe, LOGf is not.
#define WRITE_LITERAL_STRING(s) 	{ write(STDOUT_FILENO, s, sizeof(s)); } 
#define LOG(msg)  					WRITE_LITERAL_STRING(msg "\n")

static void LOGf(const char* fmt, ...) {
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
	LOG("Usage: little-reaper [options] <command> [<command arguments> ...]");
	LOG(" little-reaper will make itself reaper for all child processes, and");
	LOG(" make itself process group leader. It then will start <command> as sub");
	LOG(" process.");
	LOG(" While <command> is running, it will adopt any orphaned child processes");
	LOG(" and reap them if they terminate. After <command> is finished, it will");
	LOG(" optionally terminate any remaining orphans, then exit.");
	LOG(" If little-reaper gets terminated via SIGTERM or SIGINT, it will terminate");
	LOG(" all child processes, including <command> itself, then exit.");
	LOG("Options:");
	LOG(" -v: verbose mode");
	LOG(" -w: wait for all childs to terminate before exiting.");	
	LOG(" -t: terminate remaining child processes after <command> terminates.");		
}

// Simple utility function to print an unsigned integer in signal safe fashion
static void write_num(unsigned n) {
	int a = n;
	if (n > 9) {
		write_num(n/10);
	}
	const char* const digits = "0123456789";
	write(STDOUT_FILENO, digits + (a % 10), 1);
}

// Simple utility function to log, in a signal safe fashion, the exit state of a process
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

// Note: there are two strategies for this
// 1) let the direct child open up a new process group, then send a signal to
//    that process group. That only works reliably as long as the direct child 
//    is alive. This is because if it died, we have no guarantee that anyone
//    in that group is still alive. The process group may have been empty and
//    the process group id may have been reused. Astronomically unlikely, but
//    still possible.
// 2) Make yourself leader of a process group. Then send signals to that process
//    group. That also works if the direct child process terminated, but will
//    require us to filter, in our signal handler, the case where the signal
//    was sent by myself to myself.
//
// (1) does not allow to terminate orphans we adopted after the <command> terminated.
// (2) does allow that, but is a bit more work. We do (2) here.
static void send_signal_to_all_children(int sig) {
	if (getpgrp() == getpid()) { // sanity check, am I really process group leader?
		kill(0, sig);
	}
}

////////////////// signal handling ////////////////////////////////////

static volatile sig_atomic_t shutdown_in_progress = 0;
static int shutdown_signal = -1;

// Note: called from signal handler.
static void handle_shutdown_signal(int sig) {
	if (!shutdown_in_progress) {
		shutdown_in_progress = 1;
		shutdown_signal = sig;

		// send SIGTERM to all kids, then start the death clock.
		LOG("Terminating children...");
		send_signal_to_all_children(SIGTERM);
		VERBOSE("tick tock...");
		alarm(shutdown_timeout_seconds);
	} else {
		VERBOSE("shutdown in progress, ignoring further attempts.");
	}
}

// Note: called from signal handler.
static void handle_alarm() {
	// We receive this signal because we set the alarm after getting
	// a termination request, and we timeouted. So here we exit
	// right away.
	if (shutdown_in_progress) {
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
	
	WRITE_LITERAL_STRING("Signal: ");
	write_num(sig);
	WRITE_LITERAL_STRING("\n");

	switch (sig) {
		case SIGTERM:
		case SIGINT:
			handle_shutdown_signal(sig);
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
	const int sigs[] = { SIGTERM, SIGINT, SIGALRM, -1 };
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
				case 'w': 
					wait_for_all_children = 1;
					break;
				case 't': 
					terminate_all_children_after_command_terminated = 1;
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

	VERBOSEf("little-reaper (pid: %d, parent: %d, pgrp: %d)", getpid(), getppid(), getpgrp());

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
					// The command finishes. Handle -t and -w:
					// -t: we now send SIGTERM to all remaining children (orphans still running)
					// -w: we wait for all children to exit before exiting ourselves.
					if (terminate_all_children_after_command_terminated) {
						send_signal_to_all_children(SIGTERM);
					}
					if (!wait_for_all_children) {
						break;
					}
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
