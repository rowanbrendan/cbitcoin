#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <fcntl.h>

#include "BRCommon.h"
#include "BRSelector.h"

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
        void *arg, int interval) {
    ++s->num_calls;
    s->callbacks = realloc(s->callbacks, s->num_calls * sizeof(BRSelectable));
    if (s->callbacks == NULL) {
        perror("realloc failed");
        exit(1);
    }

    /* TODO set to nonblocking */
//    fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | O_NONBLOCK);

    s->callbacks[s->num_calls - 1].fd = fd;
    s->callbacks[s->num_calls - 1].callback = callback;
    s->callbacks[s->num_calls - 1].arg = arg;
    s->callbacks[s->num_calls - 1].last = 0;
    s->callbacks[s->num_calls - 1].interval = interval;

#ifdef BRDEBUG
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

        FD_ZERO(&fds);
#ifdef BRDEBUG
        printf("Checking...");
#endif
        for (i = 0; i < s->num_calls; ++i) {
            if (s->callbacks[i].interval == 0) {
                fd = s->callbacks[i].fd;
                FD_SET(fd, &fds);
                if (fd > n)
                    n = fd;
#ifdef BRDEBUG
                printf("socket %d...", fd);
#endif
            }
        }
        fflush(stdout);

        /* TODO timeout? */
        t.tv_sec = 1; /* don't let select wait on input for too long */
        t.tv_usec = 0;
        i = select(n + 1, &fds, NULL, NULL, &t);
        /* i = select(n + 1, &fds, NULL, NULL, NULL); */
        
#ifdef BRDEBUG
        printf("%d sockets ready\n", i);
#endif

        if (i < 0) {
            perror("select failed");
            exit(1);
        } else if (i > 0) {
            for (i = 0; i < s->num_calls; ++i) {
                if (s->callbacks[i].interval == 0) {
                    fd = s->callbacks[i].fd;
                    if (FD_ISSET(fd, &fds)) {
                        BRTrigger(&s->callbacks[i]);
                    }
                }
            }
        }

        /* Check timed callbacks */
#ifdef BRDEBUG
        printf("Checking timed callbacks\n");
#endif
        for (i = 0; i < s->num_calls; ++i) {
            if (s->callbacks[i].interval > 0) {
                time_t now = time(NULL);
                if (now - s->callbacks[i].last > s->callbacks[i].interval) {
                    s->callbacks[i].last = now;
                    BRTrigger(&s->callbacks[i]);
                }
            }
        }
    }
}
