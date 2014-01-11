#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "BRCommon.h"
#include "BRSelector.h"
#include "BRConnector.h"
#include "BRConnection.h"

#define MAXPENDING 100

/* networking adapted from TCP/IP Sockets in C, Second Edition */

BRConnector *BRNewConnector(char *ip, int port, BRSelector *s) {
    struct sockaddr_in addr;
    BRConnector *c = calloc(1, sizeof(BRConnector));
    if (c == NULL) {
        perror("calloc failed");
        exit(1);
    }

    /* setup listen socket */
    c->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (c->sock < 0) {
        perror("socket failed");
        exit(1);
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(c->sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("bind failed");
        exit(1);
    }
    if (listen(c->sock, MAXPENDING) < 0) {
        perror("listen failed");
        exit(1);
    }

    /* TODO bind ping listener */
    /* ping each peer every 60 seconds */
    BRAddSelectable(s, 0, BRPingCallback, c, 60);
    /* TODO bind listener for selector */
    BRAddSelectable(s, c->sock, BRListenerCallback, NULL, 0);
    c->selector = s;

    if (ip != NULL) {
        uint64_t last_seen = time(NULL);
        /* use IPv4 mapped IPv6 addresses as in https://en.bitcoin.it/wiki/Protocol_Specification#Network_address */
        struct in_addr addr;
        inet_aton(ip, &addr);
        uint8_t *octets = &addr.s_addr;
        uint8_t ipmap[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF,
                            octets[0], octets[1], octets[2], octets[3]};
        CBByteArray *ip_arr = CBNewByteArrayWithDataCopy(ipmap, 16);
        CBVersionServices services = CB_SERVICE_FULL_BLOCKS;
        bool is_public = true;

        c->my_address = CBNewNetworkAddress(last_seen,
                                        ip_arr, port, services, is_public);
    }

    return c;
}

void BRAddConnection(BRConnector *c, char *ip, int port) {
    BRConnection *conn = BRNewConnection(ip, port, c->my_address);
    ++c->num_conns;
    c->conns = realloc(c->conns, c->num_conns * sizeof(BRConnection *));
    if (c->conns == NULL) {
        perror("realloc failed");
        exit(1);
    }
    c->conns[c->num_conns - 1] = conn;

    BRAddSelectable(c->selector, conn->sock, BRPeerCallback, conn, 0);

    BRSendVersion(conn);
}

void BRListenerCallback(void *arg) {
    printf("Someone wants to connect!\n");
}

void BRPingCallback(void *arg) {
    BRConnector *c = (BRConnector *) arg;
    int i;
    for (i = 0; i < c->num_conns; ++i)
        BRSendPing(c->conns[i]);
}
