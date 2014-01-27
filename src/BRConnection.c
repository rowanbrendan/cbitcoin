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
#include <errno.h>

#include "CBObject.h"
#include "CBMessage.h"
#include "CBVersion.h"
#include "CBNetworkAddress.h"
#include "CBByteArray.h"
#include "CBAddressBroadcast.h"
#include "CBInventoryBroadcast.h"
#include "CBInventoryItem.h"
#include "CBChainDescriptor.h"
#include "CBGetBlocks.h"

#include "BRCommon.h"
#include "BRConnection.h"
#include "BRConnector.h"
#include "BRBlockChain.h"

/* Adapted from examples/pingpong.c */

#define NETMAGIC 0xd0b4bef9 /* umdnet */
#define VERSION_NUM 70001

typedef enum {
    CB_MESSAGE_HEADER_NETWORK_ID = 0, /**< The network identifier bytes */
    CB_MESSAGE_HEADER_TYPE = 4, /**< The 12 character string for the message type */
    CB_MESSAGE_HEADER_LENGTH = 16, /**< The length of the message */
    CB_MESSAGE_HEADER_CHECKSUM = 20, /**< The checksum of the message */
} CBMessageHeaderOffsets;

/* networking adapted from TCP/IP Sockets in C, Second Edition */

/*  socket_fd < 0 implies we need to use non-blocking connect
 * socket_fd >= 0 implies socket already bound to connection */
BRConnection *BRNewConnection(char *ip, uint16_t port,
        CBNetworkAddress *my_address, void *connector, int socket_fd) {
    int rtn;
    struct sockaddr_in remote;
    BRConnection *c = calloc(1, sizeof(BRConnection));
    if (c == NULL) {
        perror("calloc failed");
        exit(1);
    }

    if (socket_fd >= 0) {
        c->sock = socket_fd;
        c->flags = fcntl(c->sock, F_GETFL);
    } else {
        c->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (c->sock < 0) {
            perror("socket failed");
            exit(1);
        }
        /* set to non-blocking, saving flags */
        c->flags = fcntl(c->sock, F_GETFL);
        if (fcntl(c->sock, F_SETFL, c->flags | O_NONBLOCK) == -1) {
            perror("fcntl failed");
            exit(1);
        }

        memset(&remote, 0, sizeof(remote));
        remote.sin_family = AF_INET;
        rtn = inet_pton(AF_INET, ip, &remote.sin_addr.s_addr);
        if (rtn == 0)
            fprintf(stderr, "Address %s not valid\n", ip);
        else if (rtn < 0) {
            perror("inet_pton failed");
            exit(1);
        }
        remote.sin_port = htons(port);
        
        if (connect(c->sock, (struct sockaddr *) &remote,
                    sizeof(remote)) < 0) {
            if (errno == EINPROGRESS) {
#ifdef BRDEBUG
                printf("Connection to %s:%d on socket %d in progress\n", ip, port, c->sock);
#endif
            } else {
                perror("connect failed");
                exit(1);
            }
        }
    }

    if (ip != NULL) {
        uint64_t last_seen = time(NULL);
        /* use IPv4 mapped IPv6 addresses as in https://en.bitcoin.it/wiki/Protocol_Specification#Network_address */
        struct in_addr addr;
        inet_aton(ip, &addr);
        uint8_t *octets = (uint8_t *) &addr.s_addr;
        uint8_t ipmap[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF,
                            octets[0], octets[1], octets[2], octets[3]};
        CBByteArray *ip_arr = CBNewByteArrayWithDataCopy(ipmap, 16);
        CBVersionServices services = CB_SERVICE_FULL_BLOCKS;
        bool is_public = true;

        printf("port: %d\n", port);
        c->address = CBNewNetworkAddress(last_seen,
                                        ip_arr, port, services, is_public);
        c->my_address = my_address;

        /* decrement reference counter */
        CBReleaseObject(ip_arr);

        /* copy ip and port */
        c->port = port;
        c->ip = calloc(1, strlen(ip) + 1);
        if (c->ip == NULL) {
            perror("calloc failed");
            exit(1);
        }
        strcpy(c->ip, ip);
    }

    c->ver_acked = c->ver_received = 0;
    c->addr_sent = c->getblocks_sent = 0;
    c->connector = connector; /* In reality is a (BRConnector *) */
    return c;
}

void BRCloseConnection(BRConnection *conn) {
    /* remove from connector */
    BRConnector *connector = (BRConnector *) conn->connector;
    BRRemoveConnection(connector, conn);

    /* remove from selector */
    BRRemoveSelectable(connector->selector, conn->sock);

    /* free object */
    free(conn->ip);
    CBReleaseObject(conn->address); /* should free all associated data */
    close(conn->sock);
    free(conn);
}

bool BRVersionExchanged(BRConnection *c) {
    return c->ver_acked && c->ver_received;
}

#ifdef BRDEBUG
/* TODO get rid of this */
static void print_hex(CBByteArray *str) {
    int i = 0;
    if (str == NULL || str->length == 0) return;
    uint8_t *ptr = str->sharedData->data;
    for (; i < str->length; i++) printf("%02x", ptr[str->offset + i]);
    printf("\n");
}
static void print_header(char h[24]) {
    int i = 0;
    for (; i < 24; ++i) printf("%02x ", h[i]);
    printf("\n");
}
#endif


void BRPeerCallback(void *arg) {
    /* adapted from pingpong.c example */
    BRConnection *c = (BRConnection *) arg;
    char header[24];

    int bytes = recv(c->sock, header, 24, 0), n;
    if (bytes < 0) {
        perror("recv failed");
        exit(1);
    } else if (bytes == 0) {
        fprintf(stderr, "Connection closed on socket %d\n", c->sock);
        BRCloseConnection(c);
        return;
    } else if (bytes != 24) {
        fprintf(stderr, "Read %d bytes, not 24\n", bytes);
        exit(1);
    }

    /* check magic */
    uint32_t magic = *(uint32_t *)(header + CB_MESSAGE_HEADER_NETWORK_ID);
    if (magic != NETMAGIC) {
        fprintf(stderr, "Netmagic %u incorrect (isn't %u)\n", magic, NETMAGIC);
        exit(1);
    }

    /* read message */
    uint32_t length = *(uint32_t *)(header + CB_MESSAGE_HEADER_LENGTH);
    char *message = malloc(length);
    if (message == NULL) {
        perror("malloc failed");
        exit(1);
    }
    bytes = 0;
    while (bytes < length) {
        /* TODO don't read message all at once if not ready */
        n = recv(c->sock, message + bytes, length - bytes, 0);
        if (n < 0) {
            perror("recv failed");
            exit(1);
        } else if (n == 0) {
            fprintf(stderr, "Connection closed on socket %d\n", c->sock);
            BRCloseConnection(c);
            return;
        }

        bytes += n;
    }
    if (bytes != length) {
        fprintf(stderr, "Too many bytes read (%d, not %d)\n", bytes, length);
        exit(1);
    }

    /* Generate CBByteArray of message for use in certain protocol commands */
    CBByteArray *ba = CBNewByteArrayWithDataCopy((uint8_t *) message, length);
#ifdef BRDEBUG
    print_header(header);
    printf("message len: %d\n", length);
    print_hex(ba);
#endif

    /* TODO verify checksum? */

    if (!strncmp(header + CB_MESSAGE_HEADER_TYPE, "version\0\0\0\0\0", 12)) {
        printf("Received version header\n\n");
        c->ver_received = 1; /* we received their version */
        BRSendVerack(c);
    } else if (!strncmp(header + CB_MESSAGE_HEADER_TYPE, "verack\0\0\0\0\0\0", 12)) {
        printf("Received verack header\n\n");
        c->ver_acked = 1; /* they've acknowledged our version */
        BRSendGetAddr(c);
    }
    
    if (BRVersionExchanged(c)) {
        if (!strncmp(header + CB_MESSAGE_HEADER_TYPE, "ping\0\0\0\0\0\0\0\0", 12)) {
            printf("Received ping header\n\n");
            BRSendPong(c, ba, length);
        } else if (!strncmp(header + CB_MESSAGE_HEADER_TYPE, "pong\0\0\0\0\0\0\0\0", 12)) {
            printf("Received pong header\n\n");
            /* TODO verify nonce */
        } else if (!strncmp(header + CB_MESSAGE_HEADER_TYPE, "inv\0\0\0\0\0\0\0\0\0", 12)) {
            printf("Received inv header\n\n");
            BRHandleInv(c, ba); /* possibly sends a getdata message in response */
        } else if (!strncmp(header + CB_MESSAGE_HEADER_TYPE, "addr\0\0\0\0\0\0\0\0", 12)) {
            printf("Received addr header\n\n");
            BRHandleAddr(c, ba);
        } else if (!strncmp(header + CB_MESSAGE_HEADER_TYPE, "getaddr\0\0\0\0\0", 12)) {
            printf("Received getaddr header\n\n");
            BRSendAddr(c);
        } else if (!strncmp(header + CB_MESSAGE_HEADER_TYPE, "tx\0\0\0\0\0\0\0\0\0\0", 12)) {
            printf("Received tx header\n\n");
            /* TODO handle transaction */
        } else if (!strncmp(header + CB_MESSAGE_HEADER_TYPE, "block\0\0\0\0\0\0\0", 12)) {
            printf("Received block header\n\n");
            /* TODO handle block, call ProcessBlock */
        }



        if (!c->addr_sent) {
            BRSendAddr(c);
        }
        if (!c->getblocks_sent) {
            /* TODO should wait for blocks instead of spamming getblocks
             * after every inv received; maybe change it to
             * blocks_received instead of getblocks_sent */
            BRSendGetBlocks(c);
        }
    }

    /* reference counter should be 0 now */
    CBReleaseObject(ba);
    free(message);
}


void BRSendMessage(BRConnection *c, CBMessage *message, char *command) {
    /* partially adapted from examples/pingpong.c */
    char header[24] = {0}; /* zeros help us out places */
    
    memcpy(header + CB_MESSAGE_HEADER_TYPE, command, strlen(command));
    
    uint8_t hash[32];
    uint8_t hash2[32];
    if (message->bytes)
        CBSha256(CBByteArrayGetData(message->bytes), message->bytes->length, hash);
    else {
        /* Get the checksum right -- handles problem where checksum not calculated
         * for messages with no payload */
        CBSha256(NULL, 0, hash);
    }
    CBSha256(hash, 32, hash2);
    message->checksum[0] = hash2[0];
    message->checksum[1] = hash2[1];
    message->checksum[2] = hash2[2];
    message->checksum[3] = hash2[3];

    CBInt32ToArray(header, CB_MESSAGE_HEADER_NETWORK_ID, NETMAGIC);
    if (message->bytes) {
        CBInt32ToArray(header, CB_MESSAGE_HEADER_LENGTH, message->bytes->length);
    }
    memcpy(header + CB_MESSAGE_HEADER_CHECKSUM, message->checksum, 4);

    int sent = send(c->sock, header, 24, 0);
    if (sent < 0) {
        perror("send failed");
        exit(1);
    }
    if (sent != 24) {
        fprintf(stderr, "send sent %d, not 24 bytes\n", sent);
        exit(1);
    }
    if (message->bytes) {
        sent = send(c->sock, message->bytes->sharedData->data + message->bytes->offset,
                    message->bytes->length, 0);
        if (sent < 0) {
            perror("send failed");
            exit(1);
        }
        if (sent != message->bytes->length) {
            fprintf(stderr, "send sent %d, not %d bytes\n", sent, message->bytes->length);
            exit(1);
        }
    }

#ifdef BRDEBUG
    print_header(header);
    printf("message len: %d\n", message->bytes ? message->bytes->length : 0);
    printf("checksum: %x\n", *((uint32_t *) message->checksum));
    print_hex(message->bytes);
    printf("\n");
#endif
}

void BRSendGetBlocks(BRConnection *c) {
    BRConnector *connector = (BRConnector *) c->connector;
    CBChainDescriptor *chain = BRKnownBlocks(connector->block_chain);

    /* 0 to get as many blocks as possible (500) */
    uint8_t zero[32] = {0};
    CBByteArray *stop = CBNewByteArrayWithDataCopy(zero, 32);

    CBGetBlocks *get_blocks = CBNewGetBlocks(VERSION_NUM, chain, stop);

    uint32_t length = CBGetBlocksCalculateLength(get_blocks);
    get_blocks->base.bytes = CBNewByteArrayOfSize(length);
    CBGetBlocksSerialise(get_blocks, false);
    BRSendMessage(c, &get_blocks->base, "getblocks");

    CBReleaseObject(stop);
    CBReleaseObject(chain);
    CBFreeGetBlocks(get_blocks);

    /* update with sent getblocks */
    c->getblocks_sent = 1;
}

void BRSendGetAddr(BRConnection *c) {
    printf("Sending getaddr\n");
    CBMessage *m = CBNewMessageByObject();
    BRSendMessage(c, m, "getaddr");
    CBFreeMessage(m);
}

/* sends a getdata if needed */
void BRHandleInv(BRConnection *c, CBByteArray *message) {
    BRConnector *connector = (BRConnector *) c->connector;

    /* find blocks that are needed */
    CBInventoryBroadcast *inv = CBNewInventoryBroadcastFromData(message);
    CBInventoryBroadcastDeserialise(inv);

    CBInventoryBroadcast *new_inv = BRUnknownBlocksFromInv(connector->block_chain, inv);
    if (new_inv->itemNum > 0) {
        uint32_t length = CBInventoryBroadcastCalculateLength(new_inv);
        new_inv->base.bytes = CBNewByteArrayOfSize(length);
        CBInventoryBroadcastSerialise(new_inv, false);
        BRSendMessage(c, &new_inv->base, "getdata");

        /* ask for more */
        c->getblocks_sent = 0;
    }

    CBFreeInventoryBroadcast(new_inv);
    CBFreeInventoryBroadcast(inv);
}

void BRHandleAddr(BRConnection *c, CBByteArray *message) {
    CBAddressBroadcast *b = CBNewAddressBroadcastFromData(message, true);
    CBAddressBroadcastDeserialise(b);

    int i;
    for (i = 0; i < b->addrNum; ++i) {
        CBByteArray *ba = b->addresses[i]->ip;
        uint8_t *addr = CBByteArrayGetData(ba);

        if (addr[10] == 0xFF && addr[11] == 0xFF) {
            /* octets:      255   .  255  .  255  .  255  \0 */
            char *ip = malloc(3 + 1 + 3 + 1 + 3 + 1 + 3 + 1);
            if (ip == NULL) {
                perror("malloc failed");
                exit(1);
            }
            sprintf(ip, "%d.%d.%d.%d", addr[12], addr[13], addr[14], addr[15]);
            printf("Found address %s on port %hu\n", ip, b->addresses[i]->port);

            BRConnector *connector = (BRConnector *) c->connector;
            BROpenConnection(connector, ip, b->addresses[i]->port);

            free(ip);
        } else {
            fprintf(stderr, "Real IPv6 addresses not supported\n");
            exit(1);
        }
    }

    CBFreeAddressBroadcast(b);
}

void BRSendAddr(BRConnection *c) {
    BRConnector *connector = (BRConnector *) c->connector;
    CBAddressBroadcast *b = CBNewAddressBroadcast(true);
    
    int i;
    for (i = 0; i < connector->num_conns; ++i)
        CBAddressBroadcastAddNetworkAddress(b, connector->conns[i]->address);
    /* add mine too */
    CBAddressBroadcastAddNetworkAddress(b, connector->my_address);

    uint32_t length = CBAddressBroadcastCalculateLength(b);
    b->base.bytes = CBNewByteArrayOfSize(length);
    CBAddressBroadcastSerialise(b, false);
    BRSendMessage(c, &b->base, "addr");

    CBFreeAddressBroadcast(b);

    /* update addr sent */
    c->addr_sent = 1;
}

void BRSendPing(BRConnection *c) {
    printf("Sending ping\n");
    
    CBMessage *m = CBNewMessageByObject();
    uint64_t nonce = rand();
    CBByteArray *ba = CBNewByteArrayWithDataCopy((uint8_t *) &nonce, 8);
    CBInitMessageByData(m, ba);

    BRSendMessage(c, m, "ping");

    CBReleaseObject(ba);
    CBFreeMessage(m);
}

void BRSendPong(BRConnection *c, CBByteArray *nonce, uint32_t length) {
    if (length != 8) {
        fprintf(stderr, "Ping nonce size %d not 8\n", length);
        exit(1);
    }
    printf("Sending pong reply\n");
    CBMessage *m = CBNewMessageByObject();
    CBInitMessageByData(m, nonce);
    BRSendMessage(c, m, "pong");
    CBFreeMessage(m);
}

void BRSendVerack(BRConnection *c) {
    printf("Sending verack reply\n");
    CBMessage *m = CBNewMessageByObject();
    BRSendMessage(c, m, "verack");
    CBFreeMessage(m);
}

void BRSendVersion(BRConnection *c) {
    /* current version number according to http://bitcoin.stackexchange.com/questions/13537/how-do-i-find-out-what-the-latest-protocol-version-is */
    CBVersionServices services = CB_SERVICE_FULL_BLOCKS;
    int64_t t = time(NULL);
    CBNetworkAddress *r_addr = c->address;
    CBNetworkAddress *s_addr = c->my_address;
    uint64_t nonce = rand();
    CBByteArray *ua = CBNewByteArrayFromString("br_cmsc417_v0.1", false);
    int32_t block_height = 0; /* TODO get real number */

    CBVersion *v = CBNewVersion(VERSION_NUM, services, t, r_addr, s_addr,
            nonce, ua, block_height);
    uint32_t length = CBVersionCalculateLength(v);
    v->base.bytes = CBNewByteArrayOfSize(length);
    CBVersionSerialise(v, false);

    BRSendMessage(c, &v->base, "version");

    /* don't release remote and local addresses just yet.
     * need them for other messages */
    CBReleaseObject(ua);
    CBFreeVersion(v);
}

