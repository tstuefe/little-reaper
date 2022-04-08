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
static int dont_quit = 0;

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

static void print_usage() {
	LOG("Usage: little-reaper [options] <command> [<command arguments> ...]");
	LOG("       options:");
	LOG("       -v: verbose mode");
	LOG("       -f: fault tolerant mode");
	LOG("       -w: start command, but don't quit when its finished");
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
				switch (argv[i][letter]) {
				case 'v': 
					verbose = 1;
					break;
				case 'e':
					fault_tolerant = 1;
					break;
				case 'w':
					dont_quit = 1;
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

	if (verbose) {
		LOGf("little-reaper (pid: %d, parent: %d)", getpid(), getppid());
		for (int i = 0; child_argv[i] != NULL; i ++) {
			LOGf("argv[%d]: \"%s\"", i, child_argv[i]);
		}
		LOGf("verbose: %d", verbose);
		LOGf("fault_tolerant: %d", 1);
		LOGf("dont_quit: %d", dont_quit);
	}

	// fork, then exec the child process
	pid_t child = fork();
	if (child == 0) {
		int rc = execv(child_argv[0], child_argv);
		if (rc == -1) {
			LOGf("Failed to exec \"%s\" - errno: %d (%s)",
                             child_argv[0], errno, strerror(errno));
			exit(-1);
		}
	} else {
		int status;
		pid_t p = waitpid(child, &status, 0);
		if (dont_quit) {
			for (;;) {
				sleep(120);
			}
		}
	}

	exit(0);
}
