# smallsh
A small shell in C.

This shell, coded for CS344 Operating Systems, does the following:
Provides a prompt for running commands; handles blank lines and comments; provides expansion for the variable $$; executes 3 commands exit, cd, and status via code built into the shell; executes other commands by creating new processes using a function from the exec family of functions; supports input and output redirection; supports running commands in foreground and background processes; implements custom handlers for 2 signals, SIGINT and SIGTSTP.
