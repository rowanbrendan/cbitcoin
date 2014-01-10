#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "CBMessage.h"
#include "CBVersion.h"
#include "CBNetworkAddress.h"

#include "BRConnection.h"

/* Adapted from examples/pingpong.c */

#define NETMAGIC 0xd0b4bef9 /* umdnet */

typedef enum {
    CB_MESSAGE_HEADER_NETWORK_ID = 0, /**< The network identifier bytes */
    CB_MESSAGE_HEADER_TYPE = 4, /**< The 12 character string for the message type */
    CB_MESSAGE_HEADER_LENGTH = 16, /**< The length of the message */
    CB_MESSAGE_HEADER_CHECKSUM = 20, /**< The checksum of the message */
} CBMessageHeaderOffsets;

/* networking adapted from TCP/IP Sockets in C, Second Edition */

BRConnection *BRNewConnection(char *ip, int port, CBNetworkAddress *my_address) {
    int rtn;
    struct sockaddr_in remote;
    BRConnection *c = malloc(sizeof(BRConnection));
    if (c == NULL) {
        perror("malloc failed");
        exit(1);
    }

    c->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (c->sock < 0) {
        perror("socket failed");
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
        perror("connect failed");
        exit(1);
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

        c->address = CBNewNetworkAddress(last_seen,
                                        ip_arr, port, services, is_public);
        c->my_address = my_address;
    }

    return c;
}


/* TODO get rid of this */
static void print_hex(CBByteArray *str) {
    int i = 0;
    uint8_t *ptr = str->sharedData->data;
    for (; i < str->length; i++) printf("%02x", ptr[str->offset + i]);
    printf("\n");
}
static void print_header(char h[24]) {
    int i = 0;
    for (; i < 24; ++i) printf("%02x ", h[i]);
    printf("\n");
}


void BRSendMessage(BRConnection *c, CBMessage *message, char *command) {
    /* partially adapted from examples/pingpong.c */
    char header[24] = {0}; /* zeros help us out places */
    
    memcpy(header + CB_MESSAGE_HEADER_TYPE, command, strlen(command));
    
    if (message->bytes) {
        uint8_t hash[32];
        uint8_t hash2[32];
        CBSha256(CBByteArrayGetData(message->bytes), message->bytes->length, hash);
        CBSha256(hash, 32, hash2);
        message->checksum[0] = hash2[0];
        message->checksum[1] = hash2[1];
        message->checksum[2] = hash2[2];
        message->checksum[3] = hash2[3];
    }

    CBInt32ToArray(header, CB_MESSAGE_HEADER_NETWORK_ID, NETMAGIC);
    if (message->bytes)
        CBInt32ToArray(header, CB_MESSAGE_HEADER_LENGTH, message->bytes->length);
    memcpy(header + CB_MESSAGE_HEADER_CHECKSUM, message->checksum, 4);

    send(c->sock, header, 24, 0);
    if (message->bytes)
        send(c->sock, message->bytes->sharedData->data + message->bytes->offset,
                message->bytes->length, 0);

    /* TODO get rid of this */
    print_header(header);
    printf("message len: %d\n", message->bytes->length);
    printf("checksum: %x\n", *((uint32_t *) message->checksum));
    print_hex(message->bytes);
}

void BRSendVersion(BRConnection *c) {
    /* current version number according to http://bitcoin.stackexchange.com/questions/13537/how-do-i-find-out-what-the-latest-protocol-version-is */
    int32_t version = 70001;
    CBVersionServices services = CB_SERVICE_FULL_BLOCKS;
    int64_t t = time(NULL);
    CBNetworkAddress *r_addr = c->address;
    CBNetworkAddress *s_addr = c->my_address;
    uint64_t nonce = rand();
    CBByteArray *ua = CBNewByteArrayFromString("br_cmsc417_v0.1", false);
    int32_t block_height = 0; /* TODO get real number */

    CBVersion *v = CBNewVersion(version, services, t, r_addr, s_addr,
            nonce, ua, block_height);
    uint32_t length = CBVersionCalculateLength(v);
    v->base.bytes = CBNewByteArrayOfSize(length);
    CBVersionSerialise(v, false);

    BRSendMessage(c, &v->base, "version");

    CBFreeVersion(v);
}













