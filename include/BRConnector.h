#ifndef BRCONNECTOR_H_
#define BRCONNECTOR_H_

#include "CBNetworkAddress.h"

#include "BRConnection.h"

typedef struct {
    int num_conns;
    BRConnection **conns;
    int sock;
    CBNetworkAddress *my_address;
} BRConnector;

BRConnector *BRNewConnector(char *, int);
void BRAddConnection(BRConnector *, char *, int);

#endif
