# tinyreaper

`tinyreaper` is a small command line utility that registers itself as sub process reaper, then 
starts a command as a sub process.

Child processes that get orphaned will be adopted by tinyreaper, and properly reaped upon termination.

`tinyreaper` will also handle termination (TERM, INT, QUIT) and in turn terminate (send SIGTERM) to
all child processes.

Usage:

```
tinyreaper [Options] <command> [<command arguments>]

Registers itself as sub reaper for child processes, then starts <command>.

Options:
    `-v`: verbose mode
    `-w`: wait for all orphans to terminate after <command> exits
    `-t`: terminate orphans after <command> exits
```

## Notes:

    `-w` and `-t` can be used separately, or combined:
    - `tinyreaper -w` starts a command and does not exit before all childs terminated, including orphans.
    - `tinyreaper -t` terminates orphans after <command> terminates and exits immediately.
    - `tinyreaper -tw` terminates orphans after <command> terminates and waits for them to exit gracefully.
