#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <fcntl.h>

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

    /* set to nonblocking */
//    fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | O_NONBLOCK);

    s->callbacks[s->num_calls - 1].fd = fd;
    s->callbacks[s->num_calls - 1].callback = callback;
    s->callbacks[s->num_calls - 1].arg = arg;
    s->callbacks[s->num_calls - 1].last = 0;
    s->callbacks[s->num_calls - 1].interval = interval;
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
//        struct timeval t;
        int n = 0, i, fd;
        fd_set fds;

        FD_ZERO(&fds);
        for (i = 0; i < s->num_calls; ++i) {
            if (s->callbacks[i].interval == 0) {
                fd = s->callbacks[i].fd;
                FD_SET(fd, &fds);
                if (fd > n)
                    n = fd;
            }
        }

 //       t.tv_sec = 1;
  //      t.tv_usec = 0;
       // i = select(n + 1, &fds, NULL, NULL, &t);
        i = select(n + 1, &fds, NULL, NULL, NULL);
        
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
