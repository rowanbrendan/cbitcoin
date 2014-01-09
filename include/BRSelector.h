#ifndef BRSELECTOR_H_
#define BRSELECTOR_H_

typedef struct {
    int fd;
    void (*callback)(void *);
    void *arg;
    time_t last; /* last time it was called */
    int interval; /* when it should be periodically called, in seconds (0 for non-timed) */
} BRSelectable;

void BRTrigger(BRSelectable *);

typedef struct {
    int num_calls;
    BRSelectable *callbacks;
} BRSelector;

BRSelector *BRNewSelector();
void BRAddSelectable(BRSelector *, int, void (*)(void *), void *, int);
void BRRemoveSelectable(BRSelector *, int);
void BRLoop(BRSelector *);

#endif
