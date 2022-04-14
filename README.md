# tinyreaper

`tinyreaper` is a small command line utility that registers itself as sub process reaper, then 
starts a command as a sub process.

Child processes that get orphaned will be adopted by tinyreaper, and properly reaped upon termination.

After command exits, `tinyreaper` will terminate any remaining orphans, wait a short while for them
to finish, reap them, then exit.

`tinyreaper` will also handle termination (TERM, INT, QUIT) and in turn terminate (send SIGTERM) to
all child processes.

Usage:

```
tinyreaper [Options] <command> [<command arguments>]

tinyreaper registers itself as sub process reaper, then starts a command as a sub process.

Child processes that get orphaned will be adopted by tinyreaper, and properly reaped upon termination.

After command exits, `tinyreaper` will terminate any remaining orphans, wait a short while for them
to finish, reap them, then exit.

`tinyreaper` will also handle termination (TERM, INT, QUIT) and in turn terminate (send SIGTERM) to
all child processes.

Options:
    `-v`: verbose mode
```
