/*
 * Ben Foxman | netid=btf28 | HW5-bashLT | Due 12/11/20
 * An implementation of the bash backend.
 */

#include "/c/cs323/Hwk5/process-stub.h"

// Print error message and die with STATUS - cred. Stan Eisenstat
#define ERROREXIT(name, status)  {saved_err = errno; (perror(name), exit(status));}
#define BUILTIN(name) (!(strcmp(name, "cd")) || !(strcmp(name, "export")) || !(strcmp(name, "wait")))

int simples(const CMD *cmd);
int pipelines(const CMD *cmd);
int conditionals(const CMD *cmd);
int commands (const CMD *cmd, int backgrounded);
int builtins(const CMD *cmd, int run);
void addToEnv(int s);
void reap_zombies();
pid_t pid, pid1, pid2, status, status1, status2, saved_err, *zombies = NULL, zombie_count = 0;

int process (const CMD *cmd) {
    int s = commands(cmd, 0); // default is not to background
    signal (SIGINT, SIG_IGN); // ignore interrupts
    reap_zombies(); // reap zombies
    signal (SIGINT, SIG_DFL); // allow interrupts
    return s;
}

// return status of a sequence of commands
int commands (const CMD *cmd, int backgrounded){
    
    // recursively decide if each sequence should be backgrounded or not
    if (cmd -> type == SEP_END || cmd -> type == SEP_BG){
        status = commands(cmd -> left, cmd -> type == SEP_BG ? 1 : 0);
        if (cmd -> right){
            status = commands(cmd -> right, backgrounded); // status = last command
        }
    }
    // handle sequence
    else { 
        if (backgrounded){
            status = 0;
            // fork subshell for backgrounded command
            if ((pid = fork()) < 0){
                ERROREXIT("fork(): ", saved_err);
            }
            else if (pid == 0){
                zombie_count = 0;
                REALLOC(zombies, 0);
                exit(conditionals(cmd));
            }
            else {
                // add pid to list to clean
                zombie_count++;
                REALLOC(zombies, zombie_count);
                zombies[zombie_count - 1] = pid;
                fprintf(stderr, "Backgrounded: %d\n", pid);
            }
        }
        else {
            status = conditionals(cmd);
        }
    }
    addToEnv(status); // update status 
    return status;
}


// return status of last non-skipped pipeline
int conditionals (const CMD *cmd){
    // built in commands only affect parent at and-or level
    if (cmd -> type == SIMPLE && BUILTIN(cmd -> argv[0])){
        status = builtins(cmd, 1); 
        return status;
    }
    if (cmd -> type != SEP_OR && cmd -> type != SEP_AND){
        status = pipelines(cmd);
        return status;
    }
    status = conditionals(cmd -> left); // status of next pipeline
    if (status != 0 && cmd -> type == SEP_AND){  // AND skip
        return STATUS(status);
    }
    if (status == 0 && cmd -> type == SEP_OR){  // OR skip
        return STATUS(status);
    }

    return conditionals(cmd -> right); 
}

// if cmd -> left = a and cmd -> right = b, this will perform a | b. return status 
int pipelines (const CMD *cmd){
    // handle simple
    if (cmd -> type != PIPE){
        status = simples(cmd);
        return status;
    }
    //create pipe
    int fd[2];
    if (pipe(fd) < 0){
        ERROREXIT("pipe(): ", saved_err);
    }
    //fork left child
    if ((pid1 = fork()) < 0){
        ERROREXIT("fork(): ", saved_err);
    }
    else if (pid1 == 0){
        // left child - need write
        close (fd[0]);                      
        if (fd[1] != fileno(stdout)) {                   
            dup2 (fd[1], fileno(stdout));
            close (fd[1]);
        }
        exit(pipelines(cmd -> left));
    }
    else {
        close (fd[1]); // close off the write end             
        if ((pid2 = fork()) < 0){
            ERROREXIT("fork(): ", saved_err);
        }
        else if (pid2 == 0){
            // right child - need write end
            signal (SIGINT, SIG_DFL); // allow interrupts
            close (fd[1]);                      
            if (fd[0] != fileno(stdin)) {                
                dup2 (fd[0], fileno(stdin));
                close (fd[0]);
            }
            exit(pipelines(cmd -> right));
        }
        else {
            close (fd[1]); // close off the write end
            signal (SIGINT, SIG_IGN); // ignore interrupts
            waitpid(pid1, &status1, 0); 
            waitpid(pid2, &status2, 0);
            signal (SIGINT, SIG_DFL); // allow interrupts
            status = status2 != 0 ? status2 : status1;
            return STATUS(status); // return a nonzero second status or the first status
        }
    }
}

// execute the simple command specified by cmd. Return the status returned by waitpid().
int simples (const CMD *cmd){

    if ((pid = fork()) < 0){
        ERROREXIT("fork(): ", saved_err);
    }
    else if (pid == 0){
        int fd;
        // set local variables
        for (int i = 0; i < cmd -> nLocal; i++){
            setenv(cmd -> locVar[i], cmd -> locVal[i], -1);
        }

        // RED_IN + RED_IN_HERE
        if (cmd -> fromType == 1 || cmd -> fromType == 2){
            // RED_IN
            if (cmd -> fromType == 1){
                fd = open(cmd -> fromFile, O_RDONLY);
            }
            // RED_IN_HERE
            else {
                FILE *temp = tmpfile();
                if (!temp){
                    ERROREXIT(cmd -> fromFile, saved_err);
                }
                fd = fileno(temp);
                write(fd, cmd -> fromFile, strlen(cmd -> fromFile));
                rewind(temp);
            }
            
            if (fd < 0){
                ERROREXIT(cmd -> fromFile, saved_err);
            }
            dup2(fd, fileno(stdin));
            close(fd);
        }

        // RED_OUT + RED_OUT_APP + RED_OUT_ERR
        if (cmd -> toType >= 3 && cmd -> toType <= 5){
            // RED_OUT + RED_OUT_ERR
            if (cmd -> toType != 4){ 
                fd = open(cmd -> toFile, O_WRONLY | O_TRUNC | O_CREAT, 0666);
            }
            // RED_OUT_APP
            else { 
                fd = open(cmd -> toFile, O_WRONLY | O_APPEND | O_CREAT, 0666);
            }
            
            if (fd < 0){
                ERROREXIT(cmd -> toFile, saved_err);
            }
            dup2(fd, fileno(stdout));
            close(fd);
        }

        if (cmd -> type == SUBCMD){
            exit(process(cmd -> left));
        }
        else { // cmd -> type == SIMPLE
            if (BUILTIN(cmd -> argv[0])){ // handle builtins with no effect on parent
                exit(builtins(cmd, 0));
            }
            else {
                execvp(cmd -> argv[0], cmd -> argv);
                ERROREXIT(cmd -> argv[0], saved_err);
            }
        }
    }
    else {
        signal (SIGINT, SIG_IGN); // ignore interrupts
        waitpid(pid, &status, 0);
        signal (SIGINT, SIG_DFL); // allow interrupts
        status = STATUS(status);
        return status;
    }
}

// handle built-in commands - return status
int builtins(const CMD *cmd, int run){
    status = 0; // good until bad
    // first, error check redirection
    int fd;
    // RED_IN + RED_IN_HERE
    if (cmd -> fromType == 1 || cmd -> fromType == 2){
        // RED_IN
        if (cmd -> fromType == 1){
            fd = open(cmd -> fromFile, O_RDONLY);
        }
        // RED_IN_HERE
        else {
            FILE *temp = tmpfile();
            if (!temp){
                perror("builtin");
                return errno;
            }
            fd = fileno(temp);
            write(fd, cmd -> fromFile, strlen(cmd -> fromFile));
            rewind(temp);
        }
        
        if (fd < 0){
            perror("builtin");
            return errno;
        }
    }
    fd = 1;
    // RED_OUT + RED_OUT_APP + RED_OUT_ERR
    if (cmd -> toType >= 3 && cmd -> toType <= 5){
        // RED_OUT + RED_OUT_ERR
        if (cmd -> toType != 4){ 
            fd = open(cmd -> toFile, O_WRONLY | O_TRUNC | O_CREAT, 0666);
        }
        // RED_OUT_APP
        else { 
            fd = open(cmd -> toFile, O_WRONLY | O_APPEND | O_CREAT, 0666);
        }
        
        if (fd < 0){
            perror("builtin");
            return errno;
        }
    }
    if (run == 0){ // don't run if not necessary
        return status;
    }
    // CD
    if (!strcmp(cmd -> argv[0], "cd")){
        if (cmd -> argc == 1){ // HOME directory
            status = chdir(getenv("HOME"));
            if (status < 0){
                status = errno;
                fprintf(stderr, "cd: bad HOME\n");
            }
        }
        else if (cmd -> argc == 2){
            if (!strcmp(cmd -> argv[1], "-p")){ // print working directory
                char *name = getcwd(NULL, 0);
                if (!name){
                    status = errno;
                    fprintf(stderr, "cd: bad current dirname\n");
                }
                else {
                    // RED_OUT + RED_OUT_APP + RED_OUT_ERR
                    dprintf(fd, "%s\n", name); // stdout is saved from before
                    free(name);
                }
            }
            else { // change working directory
                status = chdir(cmd -> argv[1]);
                if (status < 0){
                    status = errno;
                    fprintf(stderr, "cd: bad name\n");
                }
            }
        }
        else { // error 
            status = 1;
            fprintf(stderr, "cd: bad usage\n");
        }   
    }
    // EXPORT
    else if (!strcmp(cmd -> argv[0], "export")){
        if (cmd -> argc == 2){
            char *val = strchr(cmd -> argv[1], '='); // value to set
            if (!val || val == cmd -> argv[1]){ // no equals sign or no name
                status = 1;
                fprintf(stderr, "export: bad usage\n");
            }
            else {
                cmd -> argv[1][val - cmd -> argv[1]] = '\0'; // set equals sign to null
                int valid_name = 0;
                for (int i = 0; i < strlen(cmd -> argv[1]); i++){
                    if (!strchr(VARCHR, cmd -> argv[1][i]) || (i == 0 && isdigit(cmd -> argv[1][i]))){
                        valid_name = 1;
                        break;
                    }
                }
                if (valid_name != 0 || setenv(cmd -> argv[1], val + 1, 1) != 0){ // add to env
                    if (valid_name != 0){
                        status = 1;
                    }
                    else {
                        status = errno;
                    }
                    fprintf(stderr, "export: bad usage\n");
                }
            }
        }
        else if (cmd -> argc == 3 && !strcmp(cmd -> argv[1], "-n")){ // remove from env
            int valid_name = 0;
            for (int i = 0; i < strlen(cmd -> argv[2]); i++){
                if (!strchr(VARCHR, cmd -> argv[2][i]) || (i == 0 && isdigit(cmd -> argv[2][i]))){
                    valid_name = 1;
                    break;
                }
            }
            if(valid_name != 0 || unsetenv(cmd -> argv[2]) < 0){
                if (valid_name != 0){
                    status = 1;
                }
                else {
                    status = errno;
                }
                fprintf(stderr, "export: error on removal\n");
            }
        }
        else {
            status = 1;
            fprintf(stderr, "export: bad usage\n");
        }
    }
    // WAIT
    else { // wait command 
        if (cmd -> argc == 1){
            signal (SIGINT, SIG_IGN); // ignore interrupts
            while (zombie_count > 0){
                reap_zombies();
            }
            signal (SIGINT, SIG_DFL); // allow interrupts
        }
        else {
            status = 1;
            fprintf(stderr, "wait: bad usage\n");
        }
    }
    return status;
}

// called once per process(), called until no zombies exist for wait. 
void reap_zombies(){
    for (int i = 0; i < zombie_count; i++){
        int res = waitpid(zombies[i], &status, WNOHANG);
        if (res > 0){  // child has finished, remove from list
            // shift pid's down one
            for (int j = i; j < zombie_count - 1; j++){
                zombies[j] = zombies[j + 1];
            }
            zombie_count--;
            REALLOC(zombies, zombie_count);
            fprintf(stderr, "Completed: %d (%d)\n", res, STATUS(status));
        }
    }
}

// set env variable $? to string-ified status
void addToEnv(int s){
    char sbuf[64];
    sprintf(sbuf, "%d", s);
    if (setenv("?", sbuf, 1) < 0){
        ERROREXIT("setenv(): ", saved_err);
    }
}