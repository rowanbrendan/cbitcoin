#ifndef BRCONNECTOR_H_
#define BRCONNECTOR_H_

#include "CBNetworkAddress.h"

#include "BRSelector.h"
#include "BRConnection.h"

typedef struct {
    int num_conns;
    BRConnection **conns;
    int sock;
    CBNetworkAddress *my_address;
    BRSelector *selector;
} BRConnector;

BRConnector *BRNewConnector(char *, int, BRSelector *);
void BRAddConnection(BRConnector *, char *, int);
void BRListenerCallback(void *);
void BRPingCallback(void *);

#endif
