#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "CBObject.h"
#include "CBNetworkAddress.h"
#include "CBByteArray.h"

#include "BRCommon.h"
#include "BRSelector.h"
#include "BRConnector.h"
#include "BRConnection.h"

#define MAXPEERS 500
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
    BRAddSelectable(s, 0, BRPingCallback, c, 60, FOR_TIMING);
    /* TODO bind listener for selector */
    BRAddSelectable(s, c->sock, BRListenerCallback, c, 0, FOR_READING);
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
        c->my_port = port;
        c->my_ip = calloc(1, strlen(ip) + 1);
        if (c->my_ip == NULL) {
            perror("calloc failed");
            exit(1);
        }
        strcpy(c->my_ip, ip);

        CBReleaseObject(ip_arr);
    }

    return c;
}

void BROpenConnection(BRConnector *c, char *ip, uint16_t port) {
    /* check number of connections */
    if (c->num_conns + c->num_ho >= MAXPEERS)
        return;

    /* check unique connection */
    int i;
    if (strcmp(c->my_ip, ip) == 0 && c->my_port == port) /* don't connect to me */
        return;
    for (i = 0; i < c->num_conns; ++i)
        if (strcmp(c->conns[i]->ip, ip) == 0 && c->conns[i]->port == port)
            return;
    for (i = 0; i < c->num_ho; ++i)
        if (strcmp(c->half_open_conns[i]->ip, ip) == 0 &&
                c->half_open_conns[i]->port == port)
            return;

    BRConnection *conn = BRNewConnection(ip, port, c->my_address, c, -1);
    ++c->num_ho;
    c->half_open_conns = realloc(c->half_open_conns, c->num_ho * sizeof(BRConnection *));
    if (c->half_open_conns == NULL) {
        perror("realloc failed");
        exit(1);
    }
    c->half_open_conns[c->num_ho - 1] = conn;

    /* wait for connection to fully open */
    BRAddSelectable(c->selector, conn->sock, BRConnectedCallback, conn, 0, FOR_WRITING);
}

static void BRAddOpenedConnection(BRConnector *, BRConnection *);
static void BRRemoveOpenedConnection(BRConnector *, BRConnection *);
static void BRRemoveHalfOpenConnection(BRConnector *, BRConnection *);

void BRConnectedCallback(void *arg) {
    BRConnection *conn = (BRConnection *) arg;
    BRConnector *c = (BRConnector *) conn->connector;

    printf("Connected to %s on port %hu\n", conn->ip, conn->port);
    BRAddOpenedConnection(c, conn);
}

static void BRAddOpenedConnection(BRConnector *c, BRConnection *conn) {
    BRSelector *s = c->selector;

    /* remove from writing selectables */
    BRRemoveSelectable(s, conn->sock);

    /* add to opened connections */
    ++c->num_conns;
    c->conns = realloc(c->conns, c->num_conns * sizeof(BRConnection *));
    if (c->conns == NULL) {
        perror("realloc failed");
        exit(1);
    }
    c->conns[c->num_conns - 1] = conn;

    BRRemoveHalfOpenConnection(c, conn);

    /* reset original flags */
    if (fcntl(conn->sock, F_SETFL, conn->flags) == -1) {
        perror("fcntl failed");
        exit(1);
    }
    /* add to selector and send version */
    BRAddSelectable(c->selector, conn->sock, BRPeerCallback, conn, 0, FOR_READING);
    BRSendVersion(conn);
}

/* removes connection conn out of both connection lists */
void BRRemoveConnection(BRConnector *c, BRConnection *conn) {
    BRRemoveOpenedConnection(c, conn);
    BRRemoveHalfOpenConnection(c, conn);
}

static void BRRemoveOpenedConnection(BRConnector *c, BRConnection *conn) {
    /* remove from opened connections */
    int i;
    for (i = 0; i < c->num_conns; ++i) {
        if (c->conns[i] == conn) {
            memmove(&c->conns[i], &c->conns[i + 1],
                    sizeof(BRConnection *) * (c->num_conns - i - 1));
            --c->num_conns;
            c->conns = realloc(c->conns, c->num_conns * sizeof(BRConnection *));
            if (c->num_conns != 0 && c->conns == NULL) {
                perror("realloc failed");
                exit(1);
            }
            break;
        }
    }
}

static void BRRemoveHalfOpenConnection(BRConnector *c, BRConnection *conn) {
    /* remove from half opened connections */
    int i;
    for (i = 0; i < c->num_ho; ++i) {
        if (c->half_open_conns[i] == conn) {
            memmove(&c->half_open_conns[i], &c->half_open_conns[i + 1],
                    sizeof(BRConnection *) * (c->num_ho - i - 1));
            --c->num_ho;
            c->half_open_conns = realloc(c->half_open_conns,
                                    c->num_ho * sizeof(BRConnection *));
            if (c->num_ho != 0 && c->half_open_conns == NULL) {
                perror("realloc failed");
                exit(1);
            }
            break;
        }
    }
}

void BRListenerCallback(void *arg) {
    /* partially adapted from http://www.gnu.org/software/libc/manual/html_node/Server-Example.html */
    BRConnector *c = (BRConnector *) arg; /* argument is connector */
    struct sockaddr_in client;
    size_t size = sizeof(client);

    int new = accept(c->sock, (struct sockaddr *) &client, &size);
    if (new < 0) {
        perror("accept failed");
        exit(1);
    }

    /* TODO probably using ephemeral ports, not the advertised port */
    printf("Accepted connection from %s:%hu on socket %d\n",
            inet_ntoa(client.sin_addr), ntohs(client.sin_port), new);

    BRConnection *conn = BRNewConnection(inet_ntoa(client.sin_addr),
                            ntohs(client.sin_port), c->my_address, c, new);
    BRAddOpenedConnection(c, conn);
}

void BRPingCallback(void *arg) {
    /* called when it's time to ping again */
    BRConnector *c = (BRConnector *) arg;
    int i;
    for (i = 0; i < c->num_conns; ++i)
        BRSendPing(c->conns[i]);
}

