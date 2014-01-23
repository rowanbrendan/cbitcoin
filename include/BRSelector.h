#ifndef BRSELECTOR_H_
#define BRSELECTOR_H_

/* used for type attribute of BRSelectable */
#define FOR_TIMING  2
#define FOR_WRITING 1
#define FOR_READING 0

typedef struct {
    int fd;
    void (*callback)(void *);
    void *arg;
    time_t last, start; /* last/start time it was called */
    int interval; /* when it should be periodically called, in seconds (0 for non-timed) */
    char type; /* see types above */
} BRSelectable;

void BRTrigger(BRSelectable *);

typedef struct {
    int num_calls;
    BRSelectable *callbacks;
} BRSelector;

BRSelector *BRNewSelector();
void BRAddSelectable(BRSelector *, int, void (*)(void *), void *, int, char);
void BRRemoveSelectable(BRSelector *, int);
void BRLoop(BRSelector *);

#endif
