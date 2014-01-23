#ifndef BRCONNECTOR_H_
#define BRCONNECTOR_H_

#include "CBNetworkAddress.h"

#include "BRSelector.h"
#include "BRConnection.h"

typedef struct {
    int num_conns, num_ho; /* number of open connections and half-open connections */
    BRConnection **conns;
    BRConnection **half_open_conns;
    int sock;
    char *my_ip; /* dynamically allocated */
    uint16_t my_port;
    CBNetworkAddress *my_address;
    BRSelector *selector;
} BRConnector;

BRConnector *BRNewConnector(char *, int, BRSelector *);
void BROpenConnection(BRConnector *, char *, uint16_t);
void BRRemoveConnection(BRConnector *, BRConnection *);
void BRListenerCallback(void *);
void BRPingCallback(void *);
void BRConnectedCallback(void *);

#endif
