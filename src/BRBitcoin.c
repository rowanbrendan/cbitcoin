#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "BRCommon.h"
#include "BRSelector.h"
#include "BRConnection.h"
#include "BRConnector.h"
#include "BRBlockChain.h"

#define DELIMS " "

BRSelector *selector;
BRConnector *connector;
BRBlockChain *block_chain;

/* Command notation adapted from http://sunsite.ualberta.ca/Documentation/Gnu/readline-4.1/html_node/readline_45.html */

typedef struct {
    const char *name;
    void (*callback)(void); /* callbacks expect values from strtok */
    const char *help;
} Command;

static void command_error(char *);
static Command *find_command(const char *);
static void help();
static void connect();
static void connections();
static void listen();
static void quit();

static Command commands[] = {
    {"help", help, "Displays a help message"},
    {"quit", quit, "Exits the Bitcoin client"},
    {"listen", listen, "Starts listening for messages on an address and port"},
    {"connect", connect, "Connects to a given address and port"},
    {"connections", connections, "Lists all open and opening connections"},
    {NULL, NULL, NULL}
};

static void command_error(char *name) {
    fprintf(stderr, "Command not found: %s\n", name);
}

static Command *find_command(const char *tok) {
    int i;
    for (i = 0; commands[i].name != NULL; ++i)
        if (strcmp(tok, commands[i].name) == 0)
            return &commands[i];
    return NULL;
}

static void help() {
    int i;
    char *tok = strtok(NULL, DELIMS);
    if (tok != NULL) {
        Command *c = find_command(tok);
        if (c != NULL)
            printf("%s\n", c->help);
        else
            command_error(tok);
        return;
    }

    for (i = 0; commands[i].name != NULL; ++i)
        printf("%s: %s\n", commands[i].name, commands[i].help);
}

static void quit() {
    /* TODO free memory */
    exit(0);
}

static void listen() {
    char *ip = strtok(NULL, DELIMS);
    char *port = strtok(NULL, DELIMS);
    int nport = atoi(port == NULL ? "" : port);

    if (connector != NULL) {
        printf("listen can only be used once\n");
    } else if (ip != NULL && port != NULL) {
        connector = BRNewConnector(ip, nport, selector, block_chain);
        printf("Listening at %s on port %d\n", ip, nport);
    } else
        printf("usage: listen <ip> <port>\n");
}

static void connect() {
    char *ip = strtok(NULL, DELIMS);
    char *port = strtok(NULL, DELIMS);
    int nport = atoi(port == NULL ? "" : port);

    if (connector == NULL) {
        printf("Before connecting, start listening first\n");
    } else if (ip != NULL && port != NULL) {
        BROpenConnection(connector, ip, (uint16_t) nport);
        printf("Connecting to %s on port %hu\n", ip, (uint16_t) nport);
        /* TODO print message when connected */
    } else
        printf("usage: connect <ip> <port>\n");
}

static void connections() {
    int i;
    if (connector->num_conns)
        printf("Opened connections:\n");
    for (i = 0; i < connector->num_conns; ++i) {
        BRConnection *c = connector->conns[i];
        printf("\t%s:%hu on socket %d\n", c->ip, c->port, c->sock);
    }
    if (connector->num_ho)
        printf("Opening connections:\n");
    for (i = 0; i < connector->num_ho; ++i) {
        BRConnection *c = connector->half_open_conns[i];
        printf("\t%s:%hu on socket %d\n", c->ip, c->port, c->sock);
    }
}

void handle_line(char *line) {
    if (line != NULL) {
        char *cpy = calloc(1, strlen(line) + 1), *tok;
        strcpy(cpy, line);
        tok = strtok(line, DELIMS);
        if (tok != NULL) {
            Command *c = find_command(tok);
            if (c != NULL)
                c->callback();
            else
                printf("Command not found: %s\n", tok);

            add_history(cpy);
        }
        
        free(cpy);
        free(line);
    }
}

/* rl_callback_read_char doesn't have the right type */
void readline_callback(void *arg) {
    rl_callback_read_char();
}

/* adapted from http://stackoverflow.com/questions/41400/how-to-wrap-a-function-with-variable-length-arguments */
void CBLogError(char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

static char *make_dir(char *orig_dir) {
    char *dir = calloc(1, strlen(orig_dir) + 2);
    if (dir == NULL) {
        perror("calloc failed");
        exit(1);
    }
    
    strcpy(dir, orig_dir);
    dir[strlen(orig_dir)] = '/';

    /* mode still restricted by umask */
    if (mkdir(dir, 0777) != 0 && errno != EEXIST) {
        perror("mkdir failed");
        exit(1);
    }
    return dir; /* dynamically allocated */
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        char usage[] = "%s block_directory\n\t"
                        "Starts the bitcoin client with block chain storage "
                        "in the specified directory\n";
        printf(usage, argv[0]);
        exit(2);
    }

    srand(time(NULL));

    /* initialize block chain and selector */
    char *dir = make_dir(argv[1]);
    block_chain = BRNewBlockChain(dir);
    selector = BRNewSelector();

    /* allow readline to work with select */
    rl_callback_handler_install("$ ", handle_line);
    BRAddSelectable(selector, STDIN_FILENO, readline_callback, NULL, 0, FOR_READING);

    BRLoop(selector);

    free(dir);
    return 0;
}
