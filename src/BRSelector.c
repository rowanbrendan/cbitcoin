#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <fcntl.h>

#include "BRCommon.h"
#include "BRConnection.h"
#include "BRSelector.h"

/* timeout for non-blocking connects */
#define CONNECT_TIMEOUT 10

void BRTrigger(BRSelectable *s_able) {
    s_able->callback(s_able->arg);
}

BRSelector *BRNewSelector() {
    BRSelector *s = calloc(1, sizeof(BRSelector));
    if (s == NULL) {
        perror("calloc failed");
        exit(1);
    }
    return s;
}

void BRAddSelectable(BRSelector *s, int fd, void (*callback)(void *),
        void *arg, int interval, char type) {
    ++s->num_calls;
    s->callbacks = realloc(s->callbacks, s->num_calls * sizeof(BRSelectable));
    if (s->callbacks == NULL) {
        perror("realloc failed");
        exit(1);
    }

    s->callbacks[s->num_calls - 1].fd = fd;
    s->callbacks[s->num_calls - 1].callback = callback;
    s->callbacks[s->num_calls - 1].arg = arg;
    s->callbacks[s->num_calls - 1].last = 0;
    s->callbacks[s->num_calls - 1].start = time(NULL);
    s->callbacks[s->num_calls - 1].interval = interval;
    s->callbacks[s->num_calls - 1].type = type;

#ifdef BRDEBUG_SELECT
    printf("Added selectable for socket %d at interval %d\n", fd, interval);
#endif
}

void BRRemoveSelectable(BRSelector *s, int fd) {
    int i;
    for (i = 0; i < s->num_calls; ++i)
        if (s->callbacks[i].fd == fd) {
            memmove(&s->callbacks[i], &s->callbacks[i + 1],
                    sizeof(BRSelectable) * (s->num_calls - i - 1));
            break;
        }

    if (i < s->num_calls) {
        --s->num_calls;
        s->callbacks = realloc(s->callbacks, s->num_calls * sizeof(BRSelectable));
        if (s->callbacks == NULL) {
            perror("realloc failed");
            exit(1);
        }
    }
}

void BRLoop(BRSelector *s) {
    while (1) {
        struct timeval t;
        int n = 0, i, fd;
        fd_set fds;
        fd_set wrfds;

        FD_ZERO(&fds);
        FD_ZERO(&wrfds);
#ifdef BRDEBUG_SELECT
        printf("Checking...");
#endif
        for (i = 0; i < s->num_calls; ++i) {
            if (s->callbacks[i].type != FOR_TIMING) {
                fd = s->callbacks[i].fd;
                if (s->callbacks[i].type == FOR_WRITING)
                    FD_SET(fd, &wrfds);
                else if (s->callbacks[i].type == FOR_READING)
                    FD_SET(fd, &fds);

                if (fd > n)
                    n = fd;
#ifdef BRDEBUG_SELECT
                printf("socket %d...", s->callbacks[i].fd);
#endif
            }
        }
        fflush(stdout);

        /* TODO timeout? */
        t.tv_sec = 1; /* don't let select wait on input for too long */
        t.tv_usec = 0;
        i = select(n + 1, &fds, &wrfds, NULL, &t);
        /* i = select(n + 1, &fds, NULL, NULL, NULL); */
        
#ifdef BRDEBUG_SELECT
        printf("%d sockets ready\n", i);
#endif

        if (i < 0) {
            perror("select failed");
            continue;
            /*exit(1);*/
        } else if (i > 0) {
            for (i = 0; i < s->num_calls; ++i) {
                if (s->callbacks[i].type != FOR_TIMING) {
                    fd = s->callbacks[i].fd;
                    if (s->callbacks[i].type == FOR_WRITING && FD_ISSET(fd, &wrfds)) {
                        /* partially adapted from http://mff.devnull.cz/pvu/src/tcp/non-blocking-connect.c */
                        int optval = -1;
                        socklen_t optlen = sizeof(optval);
                        if (getsockopt(fd, SOL_SOCKET,
                                    SO_ERROR, &optval, &optlen) == -1) {
                            perror("getsockopt failed");
                            exit(1);
                        }

                        /* TODO bad assumption that the argument is the connection */
                        if (optval == 0) {
                            BRConnection *conn = (BRConnection *) s->callbacks[i].arg;
#ifdef BRDEBUG
                            printf("Connection to %s:%hu on socket %d opened\n",
                                        conn->ip, conn->port, fd);
#endif
                            BRTrigger(&s->callbacks[i]);
                            /* TODO fix this -- once half-opened connections are opened
                             * for writing, they should be removed from the selector which
                             * changes s->num_calls.  Here, index i could point to the
                             * wrong index and skip over a socket available for
                             * reading/writing. */
                            break;
                        } else {
                            BRConnection *conn = (BRConnection *) s->callbacks[i].arg;
#ifdef BRDEBUG
                            printf("Connection to %s:%hu on socket %d failed\n",
                                        conn->ip, conn->port, fd);
#endif
                            BRCloseConnection(conn);
                            BRRemoveSelectable(s, s->callbacks[i].fd);
                        }
                    } else if (s->callbacks[i].type == FOR_READING && FD_ISSET(fd, &fds)) {
                        BRTrigger(&s->callbacks[i]);
                        break;
                    }
                }
            }
        }

        /* Check timed callbacks */
#ifdef BRDEBUG_SELECT
        printf("Checking timed callbacks\n");
#endif
        for (i = 0; i < s->num_calls; ++i) {
            if (s->callbacks[i].type == FOR_TIMING) {
                time_t now = time(NULL);
                if (now - s->callbacks[i].last > s->callbacks[i].interval) {
                    s->callbacks[i].last = now;
                    BRTrigger(&s->callbacks[i]);
                    break;
                }
            } else if (s->callbacks[i].type == FOR_WRITING) {
                time_t now = time(NULL);
                fd = s->callbacks[i].fd;
                if (now - s->callbacks[i].start > CONNECT_TIMEOUT) {
                    BRConnection *conn = (BRConnection *) s->callbacks[i].arg;
#ifdef BRDEBUG
                    printf("Connection to %s:%hu on socket %d timed out\n",
                                conn->ip, conn->port, fd);
#endif
                    BRCloseConnection(conn);
                    BRRemoveSelectable(s, s->callbacks[i].fd);
                }
            }
        }
    }
}
