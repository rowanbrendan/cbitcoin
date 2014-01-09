#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>

#define DELIMS " "

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
static void listen();
static void quit();

static Command commands[] = {
    {"help", help, "Displays a help message"},
    {"quit", quit, "Exits the Bitcoin client"},
    {"listen", listen, "Starts listening for messages on an address and port"},
    {"connect", connect, "Connects to a given address and port"},
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
    exit(0);
}

static void listen() {

}

static void connect() {

}

int main() {
    char *line;
    while (line = readline("$ ")) {
        char *tok = strtok(line, DELIMS);
        if (tok != NULL) {
            Command *c = find_command(tok);
            if (c != NULL)
                c->callback();
            else
                printf("Command not found: %s\n", tok);
        }

        if (line != NULL && *line != '\0')
            add_history(line);
        free(line);
    }
    return 0;
}
