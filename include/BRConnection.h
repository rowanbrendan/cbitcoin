#ifndef BRCONNECTION_H_
#define BRCONNECTION_H_

#include "CBNetworkAddress.h"

typedef struct {
    int sock;
    CBNetworkAddress *address, *my_address;
} BRConnection;

BRConnection *BRNewConnection(char *, int, CBNetworkAddress *);

#endif
