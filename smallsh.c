#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#define MAX_ARGS 512    // 512 arguments are allowed
#define UNINITIALIZED_CONST -5
#define BUFFERSIZE 2048
#define MAXCHILDREN 5000

int foregroundOnlyMode = 0; // Tracks whether commands can run in the background

// Counts the number of digits in a number (for use in variable expansion function)
int digitCounter(int num)
{
    int count = 0;
    while (num > 0)
    {
        num = num/10; 
        count++;
    }

    return count;
}

// Receives a string and expands all instances of $$ into the 
// process ID of the shell.
char* performVariableExpansion(char* str)
{
    char* expandHere = strstr(str, "$$");   // point to the first occurrence of $$ in the string
    pid_t shellPid = getpid();
    int shellPidDigits = digitCounter((int) shellPid);
    char* newStr = str;

    while (expandHere != NULL)
    {
        // Allocate space for the new token (subtract two to eliminate the $$, add space for the pid)
        newStr = calloc(strlen(str) - 2 + shellPidDigits, sizeof(char));
        int numBytes = expandHere - str;  // get how many bytes exist before the $$
        strncpy(newStr, str, numBytes);
        char pidStr[digitCounter((int) shellPid)];

        // Get the pid as a string and concatenate it to the new string
        sprintf(pidStr, "%d", (int) shellPid);   
        strcat(newStr, pidStr);   

        // Concatenate the rest of the string (beyond the original $$)
        expandHere = expandHere + 2; 
        strcat(newStr, expandHere); 

        // Search the rest of the string
        expandHere = strstr(newStr, "$$");   
        str = newStr; 
    }

    return newStr;
}


// A struct representing a user's command to the shell
struct userCommand
{
    char* command; 
    char* args[MAX_ARGS];
    char* complete[MAX_ARGS + 1]; // to store the command and arguments, e.g. "ls -a"
    char* inputFile;    // If user specified an input file, save it here. Otherwise this will be null
    char* outputFile;   // If user specified an output file, save it here. Otherwise this will be null
    bool bgCommand;     // True or False (whether intended to run in background)
};

// Parses user's input into a command struct
// A command will have the following structure
// "command [arg1 arg2 ...] [< input_file] [> output_file] [&]""
// where arguments in brackets are optional
struct userCommand *parseCommand(char* input)
{
    // Strip newline character 
    char *pos;
    if ((pos=strchr(input, '\n')) != NULL)
        *pos = '\0';
    
    struct userCommand *com = malloc(sizeof(struct userCommand));
    char *saveptr; // for use with strtok_r
    com->inputFile = NULL;  // assume no input redirection
    com->outputFile = NULL; // assume no output redirection
    com->bgCommand = false; // assume it is not a background command
    int argInd = 0; // track number of arguments
    char *inputRedir = calloc(1, sizeof(char)); // Not sure how else to compare the token (which is a char*) to a single char...
    char *outputRedir = calloc(1, sizeof(char)); // Not sure how else to compare the token (which is a char*) to a single char...
    strcpy(outputRedir, ">");
    strcpy(inputRedir, "<");

    for (int i = 0; i < MAX_ARGS; i++) // initialize argument array to NULL
    {
        com->args[i] = NULL;
    }
    for (int i = 0; i < MAX_ARGS - 1; i++) // initialize complete command array to NULL
    {
        com->complete[i] = NULL;
    }

    // If the last token is an &, it is a background command
    if (input[strlen(input) - 1] == '&')
    {
        com->bgCommand = true;
        input[strlen(input) - 1] = ' '; // replace with a space so & is not read as an argument
    }

    // Get the command (first token)
    char *token = strtok_r(input, " ", &saveptr);
    com->command = calloc(strlen(token) + 1, sizeof(char));
    strcpy(com->command, token);
    com->complete[0] = com->command;    // point the first element of complete command to the command string
    
    // Get the arguments (read the rest of the input, ignoring <, >, &)
    token = strtok_r(NULL, " ", &saveptr);
    while (token) 
    { 
        // Perform any required variable expansion on this token
        token = performVariableExpansion(token);

        // If encountering a > at any point, get the output file (the next token)
        if (strcmp(token, outputRedir) == 0)    
        {
            token = strtok_r(NULL, " ", &saveptr); 
            com->outputFile = calloc(strlen(token) + 1, sizeof(char));
            strcpy(com->outputFile, token);
            token = strtok_r(NULL, " ", &saveptr);  // Get the next token
        }
        // If encountering a > at any point, get the iput file (the next token)
        else if (strcmp(token, inputRedir) == 0)   
        {
            token = strtok_r(NULL, " ", &saveptr); 
            com->inputFile = calloc(strlen(token) + 1, sizeof(char));
            strcpy(com->inputFile, token);
            token = strtok_r(NULL, " ", &saveptr);  // Get the next token
        }
        else    // Otherwise, add the argument to the arguments/complete command list (& has already been filtered out)
        {
            char* arg = calloc(strlen(token) + 1, sizeof(char));
            strcpy(arg, token);
            com->args[argInd] = arg;    
            com->complete[argInd + 1] = arg;   
            argInd++;
            token = strtok_r(NULL, " ", &saveptr);
        }
    }

// DEBUG PRINT STATEMENTS
    // printf("FINISHED PARSING\n"); fflush(stdout);
    // printf("The command is %s\n", com->command); fflush(stdout);
    // printf("The arguments are: \n");  fflush(stdout);
    // argInd = 0;
    // while (com->args[argInd] != NULL) 
    // {
    //     printf("Arg Number %d: %s \n", argInd, com->args[argInd]);  fflush(stdout);
    //     argInd++;
    // }
    // if (argInd==0) {printf("There were no arguments\n"); fflush(stdout);}
    // else {printf("There were %d args in total\n", argInd); fflush(stdout);}
    // if (com->bgCommand) {printf("Background command: True\n\n");fflush(stdout);}
    // else {printf("Background command: False\n\n");fflush(stdout);}
    // fflush(stdout);
//END DEBUG PRINT STATEMENTS

    free(inputRedir);
    free(outputRedir);
    return com;
}

// Redirects the input from STDIN to the user's specified file.
// If user has not specified a file for a background command, 
// STDIN will be redirected to /dev/null
// If user has not specified a file for a foreground command,
// this function will do nothing (i.e. stdin will not be redirected)
int redirectInput(struct userCommand* userCom)
{
    if (userCom->bgCommand && userCom->inputFile == NULL)   // No output file specified for bg comand
    {
        char* defaultRedir = calloc(strlen("/dev/null" + 1), sizeof(char));
        strcpy(defaultRedir, "/dev/null");
        int sourceFD = open(defaultRedir, O_RDONLY);
        if (sourceFD == -1) 
        { 
            perror("source open()"); 
            exit(1); 
        }
    }

    // If user specified a file for input redirection, attempt to
    // open it for reading only
    // (This small snippet of code was taken from the Exploration Module)
    else if (userCom->inputFile != NULL)
    {
        // Open the source file
        int sourceFD = open(userCom->inputFile, O_RDONLY);
        if (sourceFD == -1) 
        { 
            perror("source open()"); 
            exit(1); 
        }
        // Redirect STDIN to the source file 
        int result = dup2(sourceFD, 0);
        if (result == -1) 
        { 
            perror("source dup2()"); 
            exit(1); 
        }
    }

    return 0;
}

// Redirects the input from STDOUT to the user's specified file
// If user has not specified a file for a background command, 
// STDOUT will be redirected to /dev/null
// If user has not specified a file for a foreground command,
// this function will do nothing (i.e. stdout will not be redirected)
int redirectOutput(struct userCommand* userCom)
{
    if (userCom->bgCommand && userCom->outputFile == NULL)  
    {
        char* defaultRedir = calloc(strlen("/dev/null" + 1), sizeof(char));
        strcpy(defaultRedir, "/dev/null");
        int targetFD = open(defaultRedir, O_WRONLY | O_CREAT | O_TRUNC, 0640);
        if (targetFD == -1) 
        {
            perror("open() failed");
            exit(1);
        }
    }

    // If user specified output redirection
    // (The I/O module heavily influenced this section of code.)
    else if (userCom->outputFile != NULL)
    {
        // Open the file for writing
        int targetFD = open(userCom->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0640);
        if (targetFD == -1) 
        {
            perror("open() failed");
            exit(1);
        }
        // Redirect STDOUT to the target file
        int result = dup2(targetFD, 1);
        if (result ==  -1)
        {
            perror("dup2() failed");
            exit(1);
        }
    }

    return 0;
}

/* 
Checks if command is built in to the shell.
Currently only exit, cd and status are built in.
Returns True if built in, false if not
*/
bool builtInCommand(struct userCommand* userCom)
{
    char *exit = calloc(strlen("exit"), sizeof(char)); 
    strcpy(exit, "exit");
    char *status = calloc(strlen("status"), sizeof(char)); 
    strcpy(status, "status");
    char *cd = calloc(strlen("cd"), sizeof(char)); 
    strcpy(cd, "cd");
    bool builtIn = false;

    if (strcmp(userCom->command, exit) == 0) builtIn = true;
    else if (strcmp(userCom->command, status) == 0) builtIn = true;
    else if (strcmp(userCom->command, cd) == 0) builtIn = true;

    return builtIn;
}

// Adds a given pid to the list of pids that are still running
void addToBackgroundPids(pid_t pid, pid_t* array)
{
    // Find the first junk place to store the process id and save it there
    for (int i = 0; i < MAXCHILDREN; i++) {
        if (array[i] == UNINITIALIZED_CONST) 
        {
            array[i] = pid;
            break;
        }
    }

}

// Searches for a given pid in the list of pids still running and removes it
void removeFromBackgroundPids(pid_t pid, pid_t* array)
{
    // Find the pid in the array and replace with junk value
    for (int i = 0; i < MAXCHILDREN; i++) {
        if (array[i] == pid)
        {
            array[i] = UNINITIALIZED_CONST;
            break;
        }
    }
}

// Prints the given array of child processes that are currently running
// (For debugging purposes)
void printRunningChildren(pid_t* backgroundPids)
{
    for (int i = 0; i < MAXCHILDREN; i++)
    {
        if (backgroundPids[i] != UNINITIALIZED_CONST)
            printf("%d\n", backgroundPids[i]);
    }
}

// Runs the exit command for the shell.
// This function kills all processes the shell has started before terminating 
// the shell itself.
void runExit(pid_t * bgPids)
{
    // Loop through pid array and kill those processes
    for (int i = 0; i < MAXCHILDREN; i++)
    {
        if (bgPids[i] != UNINITIALIZED_CONST)
        {
            kill(bgPids[i], SIGTERM);
        }
    }
    exit(0); 
}

// Handler for SIGTSTP.
// When the shell receives SIGTSTP, it will toggle the globla variable
// that tracks the foreground only state.
void handle_SIGTSTP(int signo)
{
    if (foregroundOnlyMode == 0)  
    {
        foregroundOnlyMode = 1;
        char* message = "Entering foreground-only mode (& is now ignored)\n: ";
	    write(STDOUT_FILENO, message, 52);
    }    
    else if (foregroundOnlyMode == 1)  
    {
        foregroundOnlyMode = 0;
        char* message = "Exiting foreground-only mode\n: ";
        write(STDOUT_FILENO, message, 32);
    }
}

/* 
Execute a non-built in command.
This function registers signal handlers for child processes' 
SIGINT behavior. It then forks a new child process and
calls an exec() family function to execute the user's desired program.
Meanwhile, the parent process will block if the process was
specified to run in the foreground (or if 
foreground only mode is enabled). If the process was specified to run in the background,
and foreground only mode is disabled, the program will perform a non-blocking wait. 
 */
int execute(struct userCommand* com, pid_t* backgroundPids)
{
	pid_t spawnpid = UNINITIALIZED_CONST;
    int childExitMethod;
    int exitStatus;
    int result;
    bool inForeground = !com->bgCommand;
    struct sigaction ignoreAction = {0};

    // Define SIGINT behavior for a child running in the foreground
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = SIG_DFL; // set default behavior
    SIGINT_action.sa_flags = 0; // No flags set
    
    // Fork a new process
    spawnpid = fork();
    pid_t pidCopy = spawnpid;

    switch (spawnpid)
    {
        case -1:
            perror("fork() failed!");
            exit(1);
            break;

        // The child will execute user's desired program
        case 0: 
            // Set any children to ignore SIGTSP
            ignoreAction.sa_handler = SIG_IGN;
            sigaction(SIGTSTP, &ignoreAction, NULL); 

            // Foreground children will terminate upon receipt of SIGINT. Background children
            // have inherited parent's behavior and will ignore it.
            if (inForeground || foregroundOnlyMode)
            {
                sigaction(SIGINT, &SIGINT_action, NULL);    // Register default behavior
            }

            // Handle IO redirecton before executing the program
            redirectInput(com);
            redirectOutput(com);  // If there was an error redirecting output

            // Attempt to execute the user's specified program
            if (execvp(com->command, com->complete) < 0)
            {
                perror(com->command);
                exit(1);
            }
            break;

        default:    // Parent branch
            // If user requested a foreground command, or the command must be run in the foreground
            // because foreground only mode is enabled, then parent will WAIT for the process to end.
            if (inForeground || foregroundOnlyMode == 1)   
            {
                spawnpid = waitpid(spawnpid, &childExitMethod, 0);
                if (spawnpid != 0) // If waitpid returned a nonzero value, the process has finished
                { 
                    if (WIFEXITED(childExitMethod)) // Process exited normally
                    {
                        exitStatus = WEXITSTATUS(childExitMethod); 
                    } 
                    else    // Process terminated with a signal
                    {
                        exitStatus = WTERMSIG(childExitMethod);
                        printf("terminated by signal %d\n", exitStatus);
                    }  
                }       
            }
            else if (!inForeground && foregroundOnlyMode == 0)   // Running in the background--track the pid and return control to the user
            {
                printf("background pid is %d\n", spawnpid);
                addToBackgroundPids(spawnpid, backgroundPids); // save the pid to the background processes array
            }
            return exitStatus;
            break;
        }   
}
  

// Permits the changing of directory. Supports relative and absolute paths.
int cd(struct userCommand *com)
{
    int result;

    // If user entered more than one argument, this is an error.
    if (com->args[1] != NULL)
    {
        printf("too many arguments\n"); fflush(stdout);
        exit(1);
    }
    // If user entered no arguments, change to the directory specified in the HOME environment variable
    else if (com->args[0] == NULL) 
    {
        result = chdir(getenv("HOME"));
    }
    // If user entered one argument, it is either an absolute or relative path
    else
    {
        char* path = com->args[0];
        printf("You entered the path %s\n", path); fflush(stdout);
        
        // User entered an absolute path.
        if (path[0] == '/' || path[0] == '.') 
        {
            result = chdir(path);
        }
        // User entered a relative path (look within current working directory)
        else if (path[0] != '.')   
        {
            // get the current working directory and build it up into an absolute path
            // by appending '/' and the user's desired directory
            char absPath[BUFFERSIZE - 3]; // max line length - 3 (for "cd ")
            getcwd(absPath,sizeof(absPath));
            strcat(absPath, "/");
            strcat(absPath, path);
            result = chdir(absPath);
        }
        else // User entered something invalid. Try to change directory and print the error.
        {
            result = chdir(path);
        }        
    }

    if (result != 0) 
    {
        perror("Failed to change directory");
        exit(1);
    }

    return result;
}

// Loops through the given array of process IDs, checking
// to see if they are still running.
void backgroundChecker(pid_t *pid_array)
{
    int childExitMethod;
    int childStatus;
    pid_t pid;
    pid_t pidCopy;
    int statusVar;  // holds the value that will indicate how the process finished

    for (int i = 0; i < MAXCHILDREN; i++)
    {
        if (pid_array[i] != UNINITIALIZED_CONST)   // Check on this pid to see if it finished
        {
            pid = pid_array[i];
            pidCopy = pid;
            childStatus = waitpid(pid, &childExitMethod, WNOHANG);
            if (childStatus != 0) // If waitpid returned a nonzero value, the process has finished
            { 
                // Evaluate exit method
                if (WIFEXITED(childExitMethod)) // If exited normally
                {
                    statusVar = WEXITSTATUS(childExitMethod); 
                    printf("background pid %d is done: exit value: %d\n", pidCopy, statusVar); fflush(stdout);
                    removeFromBackgroundPids(pidCopy, pid_array);
                } 
                else    // If terminated with a signal
                {
                    statusVar = WTERMSIG(childExitMethod); 
                    printf("background pid %d is done: exit value: %d\n", pidCopy, statusVar); fflush(stdout);
                    removeFromBackgroundPids(pidCopy, pid_array);
                }  
            }             
        }
    }
}

/* 
Runs the small shell. Displays a command prompt to the user
and gets their input. Then executes the command as either a built in function
or a non built in function by forking a new process and calling exec. 
The shell will run until the user chooses to quit by entering exit.
*/
int main(int argc, char *argv[])
{
    // Set the shell to ignore CTRL-C
    struct sigaction ignoreAction = {0};
	ignoreAction.sa_handler = SIG_IGN;
    sigaction(SIGINT, &ignoreAction, NULL);

    // Set up handling for Ctrl-Z (toggling foreground only mode)
	struct sigaction SIGTSTP_action = {0};
	SIGTSTP_action.sa_handler = handle_SIGTSTP;
	SIGTSTP_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);  

    pid_t backgroundPids[MAXCHILDREN]; // track the pids still running in the background
    for (int i = 0; i < MAXCHILDREN; i++) {
        backgroundPids[i] = UNINITIALIZED_CONST; // initialize background pids to junk values
    }
    int statusVar = 0;  // track the status of most recent call for use in status command

    // Display first command prompt
    printf(": ");   
    fflush(stdout);

    while (true) 
    {
        // Get the input from the user
        ssize_t bytesRead = 0;
        char *input = NULL;
        size_t n = 0;
        bytesRead = getline(&input, &n, stdin); 

        //Check if shell should act on the input
        if (bytesRead == -1)        // if getline() failed
        {
            perror("Getting input failed");  fflush(stdout);
            break;
        }
        else if (bytesRead == 1)    // if user entered nothing (just newline char), ignore and reprompt
        {
            printf(": ");   // Display command prompt
            fflush(stdout);
            continue;
        }
        else if (input[0] == '#')  // If it's a comment line, ignore and reprompt
        {
            printf(": ");   // Display command prompt
            fflush(stdout);
            continue;
        } 
        else if (bytesRead > 2048)
        {
            printf("Your input was too long\n"); fflush(stdout);
        }

        // Parse the input
        struct userCommand *com = parseCommand(input);
        free(input);

        // Act on the input
        // If command is not built-in, it will be run using a child process and an exec() function
        if (!builtInCommand(com))
        {
            statusVar = execute(com, backgroundPids); // execute sets the statusVar to the result of its exec() call
        }

        // Command is built in (exit, status, or cd)
        // All of these will run in the foreground.
        else
        {
            if (strcmp(com->command, "exit") == 0) // Exit command
            {
                runExit(backgroundPids);
            }
            else if (strcmp(com->command, "cd") == 0) // cd command
            {
                cd(com);
            }
            else // only remaining built in command is status
            {
                printf("exit status %d\n", statusVar); fflush(stdout);
            }
        }

        // Check status of background processes, cleaning up any that need to be cleaned
        backgroundChecker(backgroundPids);
         
        printf(": ");   // Display command prompt
        fflush(stdout);
    }
    return 0;
}