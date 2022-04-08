A little tool that, when started, registers itself as child process sub reaper.
It then spawns off a command with arguments.

It will adopt orphaned child processes and prevent them from zombifying.


