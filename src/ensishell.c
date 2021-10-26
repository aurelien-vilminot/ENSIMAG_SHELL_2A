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

int question6_executer(char* line) {
    /* Question 6: Insert your code to execute the command line
     * identically to the standard execution scheme:
     * parsecmd, then fork+execvp, for a single command.
     * pipe and i/o redirection are not required.
     */
    printf("Not implemented yet: can not execute %s\n", line);

    /* Remove this line when using parsecmd as it will free it */
    free(line);

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
    int pid;
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
    struct jobc* ptr = malloc(sizeof(struct jobc));
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

void print_jobc() {
    int state;
    int process_sate;
    for (struct jobc* ptr = jobs; ptr != NULL; ptr = ptr->next) {
        process_sate = waitpid(ptr->pid, &state, WNOHANG);
        if (process_sate == ptr->pid) {
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

void exec_pipe(char** cmd1, char** cmd2) {
    int tuyau[2];
    int pid = fork();

    pipe(tuyau);
    if (pid == 0) {
        dup2(tuyau[0], 0);
        close(tuyau[1]);
        close(tuyau[0]);
        execvp(cmd2[0], cmd2);
    }
    dup2(tuyau[1], 1);
    close(tuyau[0]);
    close(tuyau[1]);
    execvp(cmd1[0], cmd1);
}

void execute(char** cmd, struct cmdline* l, int nb_args) {
    if (!strcmp(cmd[0], "jobs")) {
        print_jobc();
        return;
    }

    int pid, w_status;
    pid = fork();
    if (pid == 0) {
        execvp(cmd[0], cmd);
        printf("\nCommand not recognized");
        // KILL NE MARCHE PAS
        kill(0, 0);
        return;
    }
    else {
        if (!l->bg) {
            waitpid(pid, &w_status, 0);
        }
        else {
            push_jobc(cmd, pid, nb_args);
        }
    }
}

int main() {
    printf("Variante %d: %s\n", VARIANTE, VARIANTE_STRING);

#if USE_GUILE == 1
    scm_init_guile();
    /* register "executer" function in scheme */
    scm_c_define_gsubr("executer", 1, 0, 0, executer_wrapper);
#endif

    while (1) {
        struct cmdline* l;
        char* line = 0;
        int i, j;
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

        /* Display each command of the pipe */
        for (i = 0; l->seq[i] != 0; i++) {
            int nb_args = 0;
            char** cmd = l->seq[i];
            printf("seq[%d]: ", i);
            for (j = 0; cmd[j] != 0; j++) {
                printf("'%s' ", cmd[j]);
                ++nb_args;
            }
            if (l->seq[i+1] != NULL) {
                // If there is a pipe
                printf("PIPE");
                exec_pipe(cmd, l->seq[++i]);
            } else {
                // If it is a unique command
                printf("\nNO PIPE");
                execute(cmd, l, nb_args);
            }
            printf("\n");
        }
    }
}