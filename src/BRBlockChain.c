#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "CBDependencies.h"
#include "CBBlockChainStorage.h"

#include "BRCommon.h"
#include "BRBlockChain.h"

BRBlockChain *BRNewBlockChain(char *dir) {
    BRBlockChain *bc = calloc(1, sizeof(BRBlockChain));
    if (bc == NULL) {
        perror("calloc failed");
        exit(1);
    }

    bc->storage = CBNewBlockChainStorage(dir);
    if (bc->storage == 0) {
        fprintf(stderr, "Block chain could not be created\n");
        exit(1);
    }
#ifdef BRDEBUG
    printf("Storage [%ld] created\n", bc->storage);
#endif

    bool bad = false;
    bc->validator = CBNewFullValidator(bc->storage, &bad, 0);
    if (bad || bc->validator == NULL) {
        fprintf(stderr, "Full validator could not be created\n");
        exit(1);
    }

    return bc;
}
