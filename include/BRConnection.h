#ifndef BRCONNECTION_H_
#define BRCONNECTION_H_

#include "CBByteArray.h"
#include "CBNetworkAddress.h"

typedef struct {
    int sock;
    CBNetworkAddress *address, *my_address;
    char *ip; /* dynamically allocated */
    uint16_t port;
    long flags;
    void *connector; /* BRConnector pointer */
} BRConnection;

BRConnection *BRNewConnection(char *, uint16_t, CBNetworkAddress *,
                                void *, int);
void BRCloseConnection(BRConnection *);
void BRPeerCallback(void *);
void BRSendVersion(BRConnection *);
void BRSendVerack(BRConnection *);
void BRSendPong(BRConnection *, CBByteArray *, uint32_t);
void BRSendGetAddr(BRConnection *);
void BRSendAddr(BRConnection *);
void BRHandleAddr(BRConnection *, CBByteArray *);

#endif
