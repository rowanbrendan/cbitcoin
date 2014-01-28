#ifndef BRCONNECTION_H_
#define BRCONNECTION_H_

#include "CBByteArray.h"
#include "CBNetworkAddress.h"

typedef struct {
    int sock;
    CBNetworkAddress *address, *my_address;
    char *ip; /* dynamically allocated */
    uint16_t port;
    long flags; /* original flags used for socket */
    void *connector; /* BRConnector pointer */
    char ver_acked, ver_received; /* both need to be true for version exchange */
    char addr_sent, getblocks_sent;
} BRConnection;

BRConnection *BRNewConnection(char *, uint16_t, CBNetworkAddress *,
                                void *, int);
void BRCloseConnection(BRConnection *);
void BRPeerCallback(void *);
void BRSendVersion(BRConnection *);
void BRSendVerack(BRConnection *);
void BRSendPing(BRConnection *);
void BRSendPong(BRConnection *, CBByteArray *, uint32_t);
void BRSendGetAddr(BRConnection *);
void BRSendGetBlocks(BRConnection *);
void BRSendAddr(BRConnection *);
void BRHandleAddr(BRConnection *, CBByteArray *);
void BRHandleInv(BRConnection *, CBByteArray *);
void BRHandleBlock(BRConnection *, CBByteArray *);
bool BRVersionExchanged(BRConnection *); /* if ver_sent and ver_received */

#endif
