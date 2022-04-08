/*
 * Copyright (c) 2022 Thomas Stuefe. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */



#include <assert.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <wait.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/prctl.h>


static int verbose = 0;
static int fault_tolerant = 0;
static int dont_reap = 0;

// This defines how long the process should run:
// per default we terminate when the direct child (the command we spawned) terminates
// if -w, we wait until all sub processes quit, which includes the command
// if -W, we wait forever
static int wait_for_all_sub_processes = 0;
static int wait_forever = 0;

static void LOG(const char* msg) {
	printf("%s\n", msg);
	fflush(stdout);
}

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
	LOG("       options:");
	LOG("       -v: verbose mode");
	LOG("       -f: fault tolerant mode");
	LOG("       -w: Wait for all child processes (default: only wait for command)");
	LOG("       -W: Wait forever, never finish");
	LOG("       -r: Dont reap child processes");
}

// Make process a sub reaper for all direct and indirect children
static void make_me_a_reaper() {
	int rc = prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0);
	if (rc == -1) {
		LOGf("Failed to set sub reaper state - errno: %d (%s)", errno, strerror(errno));
		if (!fault_tolerant) {
			exit(-1);
		}
	}
}

static int return_code_from_process_status(int status) {
	if (WIFSIGNALED(status)) {
		return -1;
	} else if (WIFEXITED(status)) {
		return WEXITSTATUS(status);
	}
	return -1;
}

static void infinite_sleep() {
	VERBOSE("sleeping forever...");
	for (;;) sleep(10);
}

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
				case 'e':
					fault_tolerant = 1;
					break;
				case 'w':
					wait_for_all_sub_processes = 1;
					break;
				case 'W':
					wait_forever = 1;
					break;
				case 'r':
					dont_reap = 1;
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

	// Make process a sub reaper
	make_me_a_reaper();

	// assemble NULL-terminated argument vector for exec
	int child_argc = argc - start_command;
	char** child_argv = (char**) malloc((child_argc + 1) * sizeof(char*));
	for (int i = start_command; i < argc; i ++) {
		child_argv[i - start_command] = argv[i];
	}
	child_argv[child_argc] = NULL;

	VERBOSEf("little-reaper (pid: %d, parent: %d)", getpid(), getppid());
//	for (int i = 0; child_argv[i] != NULL; i ++) {
//		VERBOSEf("argv[%d]: \"%s\"", i, child_argv[i]);
//	}

	// fork, then exec the child process
	pid_t command = fork();

	if (command == 0) {
		int rc = execv(child_argv[0], child_argv);
		if (rc == -1) {
			LOGf("Failed to exec \"%s\" - errno: %d (%s)",
                             child_argv[0], errno, strerror(errno));
			exit(-1);
		}
	} else {

		// If we were asked not to reap, we cannot wait; we either wait forever or quit immediately.
		if (dont_reap) {
			if (wait_forever) {
				infinite_sleep();
			}
			exit(0);
		}

		int command_status = 0;
		int command_terminated = 0;
		int no_child_left = 0;
		for (;;) {
			int status;
			pid_t child = wait(&status);
			if (child > 0) {
				if (WIFEXITED(status) || WIFSIGNALED(status)) {
					if (WIFEXITED(status)) {
						VERBOSEf("child %d exited with %d", child, WEXITSTATUS(status));
					}
					if (WIFSIGNALED(status)) {
						VERBOSEf("child %d terminated with signal %d", child, WTERMSIG(status));
					}
					if (child == command) {
						VERBOSE("Command finished.");
						command_status = status;
						command_terminated = 1;
					}
				}
			} else {
				if (errno == ECHILD) {
					no_child_left = 1;
					VERBOSE("All child processes finished");
				} else {
					VERBOSEf("wait error (%d %s)", errno, strerror(errno));
				}
			}

			if (wait_forever) {
				if (no_child_left) {
					infinite_sleep();
				}
			} else {
				if (wait_for_all_sub_processes) {
					if (no_child_left) {
						assert(command_terminated);
						exit(return_code_from_process_status(command_status));
					}
				} else {
					if (command_terminated) {
						exit(return_code_from_process_status(command_status));
					}
				}
			}
		}
	}
	
	return 0;
}
