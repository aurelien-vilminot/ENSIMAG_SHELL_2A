/*****************************************************
 * Copyright Grégory Mounié 2008-2015                *
 *           Simon Nieuviarts 2002-2009              *
 * This code is distributed under the GLPv3 licence. *
 * Ce code est distribué sous la licence GPLv3+.     *
 *****************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#include "readcmd.h"
#include "variante.h"

#ifndef VARIANTE
#error "Variante non défini !!"
#endif

/* Guile (1.8 and 2.0) is auto-detected by cmake */
/* To disable Scheme interpreter (Guile support), comment the
 * following lines.  You may also have to comment related pkg-config
 * lines in CMakeLists.txt.
 */

#if USE_GUILE == 1
#include <libguile.h>

void execute(char** cmd, struct cmdline* l, int nb_args);

int question6_executer(char* line) {
    /* Question 6: Insert your code to execute the command line
     * identically to the standard execution scheme:
     * parsecmd, then fork+execvp, for a single command.
     * pipe and i/o redirection are not required.
     */

     /* parsecmd free line and set it up to 0 */
    struct cmdline* l = parsecmd(&line);

    if (l->err) {
        /* Syntax error, read another command */
        printf("error: %s\n", l->err);
    }

    if (l->seq[0] != NULL) {
        // If it is a unique command
        int nb_args = 0;
        char** cmd = l->seq[0];
        for (int j = 0; cmd[j] != 0; j++) {
            ++nb_args;
        }
        execute(cmd, l, nb_args);
        printf("\n");
    }

    return 0;
}

SCM executer_wrapper(SCM x) {
    return scm_from_int(question6_executer(scm_to_locale_stringn(x, 0)));
}
#endif

void terminate(char* line) {
#if USE_GNU_READLINE == 1
    /* rl_clear_history() does not exist yet in centOS 6 */
    clear_history();
#endif
    if (line) free(line);
    printf("exit\n");
    exit(0);
}

struct jobc {
    char** cmd;
    pid_t pid;
    struct jobc* next;
};

struct jobc* jobs = NULL;

void push_jobc(char** cmd, int pid, int nb_args) {
    struct jobc* new = malloc(sizeof(struct jobc));
    new->cmd = calloc(nb_args, sizeof(char*));
    for (int i = 0; cmd[i] != NULL; i++) {
        new->cmd[i] = malloc(strlen(cmd[i]) + 1);
        strcpy(new->cmd[i], cmd[i]);
    }
    new->pid = pid;
    new->next = jobs;
    jobs = new;
}

// Remove pid from jobs
void remove_jobc(int pid) {
    struct jobc* ptr;
    struct jobc* old = NULL;
    for (ptr = jobs; ptr != NULL; ptr = ptr->next) {
        if (ptr->pid == pid) {
            if (old == NULL) {
                jobs = ptr->next;
            }
            else {
                old->next = ptr->next;
            }
            free(ptr->cmd);
            free(ptr);
        }
        old = ptr;
    }
}

// Return process name
struct jobc* search_jobc(int pid) {
    struct jobc* ptr;
    for (ptr = jobs; ptr != NULL; ptr = ptr->next) {
        if (ptr->pid == pid) {
            return ptr;
        }
    }
    return NULL;
}


void print_jobc() {
    int state;
    int process_state;
    for (struct jobc* ptr = jobs; ptr != NULL; ptr = ptr->next) {
        process_state = waitpid(ptr->pid, &state, WNOHANG);
        if (process_state == ptr->pid) {
            // If process ptr->pid has ended, remove it from jobs list
            remove_jobc(ptr->pid);
            continue;
        }
        printf("\n");
        printf("[%i]\t", ptr->pid);
        for (int i = 0; ptr->cmd[i] != NULL; i++) {
            printf("%s ", ptr->cmd[i]);
        }
    }
}

void exec_pipe(struct cmdline* l) {
    char*** cmd = l->seq;
    int tuyau[2], fd_in = 0, to_close = -1;
    for (int i = 0; cmd[i] != NULL; i++) {
        if (pipe(tuyau) == -1) {
            printf("Pipe creation has failed");
        }
        pid_t pid = fork();

        if (pid == 0) {
            // Connect the standard input
            int stdin_copy, stdout_copy, fd_out;
            if (l->in && i == 0) {
                stdin_copy = dup(0);
                close(0);
                // fd_in must be 0
                fd_in = open(l->in, O_RDONLY);
                if (fd_in == -1) {
                    perror("[ERROR] open");
                    exit(EXIT_FAILURE);
                }
            } else {
                // fd_in is either the standard input or the previous pipe output
                dup2(fd_in, 0);
                // Close the previous pipe output duplicated
                if (to_close != -1) {
                    close(to_close);
                }
            }

            // Connect the standard output
            if (l->out && cmd[i + 1] == NULL) {
                stdout_copy = dup(1);
                close(0);
                // fd_out must be 1
                fd_out = open(l->out, O_WRONLY | O_TRUNC | O_CREAT, 0644);
                if (fd_out == -1) {
                    perror("[ERROR] open");
                    exit(EXIT_FAILURE);
                }
            } else if (cmd[i + 1] != NULL) {
                // If there is one more command after this one,
                // connect the standard output to the input of the pipe
                dup2(tuyau[1], 1);
            }

            close(tuyau[0]);
            close(tuyau[1]);
            execvp(cmd[i][0], cmd[i]);

            if (l->in && i == 0) {
                close(fd_in);
                dup2(stdin_copy, 0);
                close(stdin_copy);
            }
            if (l->out && cmd[i + 1] == NULL) {
                close(fd_out);
                dup2(stdout_copy, 1);
                close(stdout_copy);
            }
            printf("Command %s not recognized\n", cmd[i][0]);
            exit(1);
        } else {
            // Wait for the end of child process just created before
            waitpid(0,0,0);
        }
        close(tuyau[1]);
        // Backup the pipe output in order to reuse it for the next command as a standard input
        fd_in = tuyau[0];
        to_close = tuyau[0];
    }
}

void execute(char** cmd, struct cmdline* l, int nb_args) {
    if (!strcmp(cmd[0], "jobs")) {
        print_jobc();
        return;
    }

    pid_t pid;
    if ((pid = fork()) == 0) {
        // TODO: if cmd[0] is not recognized, do not write
        // ex: "a.txt" > cat --> "a.txt" n'est pas une commande, mais tente tout de même d'écrire dans le fichier cat
        // https://stackoverflow.com/questions/9084099/re-opening-stdout-and-stdin-file-descriptors-after-closing-them
        int fd_in, fd_out, stdin_copy, stdout_copy;
        if (l->in) {
            stdin_copy = dup(0);
            close(0);
            // fd_in must be 0
            fd_in = open(l->in, O_RDONLY);
            if (fd_in == -1) {
                perror("[ERROR] open");
                exit(EXIT_FAILURE);
            }
        }
        if (l->out) {
            stdout_copy = dup(1);
            close(1);
            // fd_out must be 1
            fd_out = open(l->out, O_WRONLY | O_TRUNC | O_CREAT, 0644);
            if (fd_out == -1) {
                perror("[ERROR] open");
                exit(EXIT_FAILURE);
            }
        }
        execvp(cmd[0], cmd);
        if (l->in) {
            close(fd_in);
            dup2(stdin_copy, 0);
            close(stdin_copy);
        }
        if (l->out) {
            close(fd_out);
            dup2(stdout_copy, 1);
            close(stdout_copy);
        }
        printf("\nCommand %s not recognized\n", cmd[0]);
        return;
    }
    else {
        if (!l->bg) {
            // Wait for the end of the child process just created before
            wait(NULL);
        }
        else {
            push_jobc(cmd, pid, nb_args);
        }
    }
}

void signal_handler(int sig) {
    // Reap zombies
    pid_t pid;
    int status;
    while((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        struct jobc* j = search_jobc(pid);
        if (j == NULL) {
            continue;
        }
        printf("\nLe fils [%d: ", pid);
        for (int i = 0; j->cmd[i] != NULL; i++) {
            printf("%s ", j->cmd[i]);
        }
        printf("\b] est terminé\n");
        // Process pid has ended, remove it from jobs list
        remove_jobc(pid);
    }
}

int main() {
    signal(SIGCHLD, signal_handler);
    printf("Variante %d: %s\n", VARIANTE, VARIANTE_STRING);

#if USE_GUILE == 1
    scm_init_guile();
    /* register "executer" function in scheme */
    scm_c_define_gsubr("executer", 1, 0, 0, executer_wrapper);
#endif

    while (1) {
        struct cmdline* l;
        char* line = 0;
        int j;
        char* prompt = "ensishell>";

        /* Readline use some internal memory structure that
           can not be cleaned at the end of the program. Thus
           one memory leak per command seems unavoidable yet */
        line = readline(prompt);
        if (line == 0 || !strncmp(line, "exit", 4)) {
            terminate(line);
        }

#if USE_GNU_READLINE == 1
        add_history(line);
#endif

#if USE_GUILE == 1
        /* The line is a scheme command */
        if (line[0] == '(') {
            char catchligne[strlen(line) + 256];
            sprintf(catchligne,
                "(catch #t (lambda () %s) (lambda (key . parameters) "
                "(display \"mauvaise expression/bug en scheme\n\")))",
                line);
            scm_eval_string(scm_from_locale_string(catchligne));
            free(line);
            continue;
        }
#endif

        /* parsecmd free line and set it up to 0 */
        l = parsecmd(&line);

        /* If input stream closed, normal termination */
        if (!l) {
            terminate(0);
        }

        if (l->err) {
            /* Syntax error, read another command */
            printf("error: %s\n", l->err);
            continue;
        }

        if (l->in) printf("in: %s\n", l->in);
        if (l->out) printf("out: %s\n", l->out);
        if (l->bg) printf("background (&)\n");

        if (l->seq[0] != NULL) {
            if (l->seq[1] != NULL) {
                // If there is one or more pipes
                exec_pipe(l);
            }
            else {
                // If it is a unique command
                int nb_args = 0;
                char** cmd = l->seq[0];
                printf("seq[0]: ");
                for (j = 0; cmd[j] != 0; j++) {
                    printf("'%s' ", cmd[j]);
                    ++nb_args;
                }
                execute(cmd, l, nb_args);
                printf("\n");
            }
        }
    }
}