#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "CBDependencies.h"
#include "CBBlockChainStorage.h"
#include "CBChainDescriptor.h"
#include "CBBlock.h"
#include "CBByteArray.h"
#include "CBObject.h"

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

static void BRChainDescriptorAddBlockIndex(CBChainDescriptor *chain,
                    BRBlockChain *bc, uint8_t branch_idx, uint32_t block_idx) {
    CBBlock *block = (CBBlock *) CBBlockChainStorageLoadBlock(bc->validator,
                                        block_idx, branch_idx);
    CBByteArray *hash = CBNewByteArrayWithDataCopy(CBBlockGetHash(block), 32);

    /* add hash to chain; let chain take hash and free it later */
    CBChainDescriptorTakeHash(chain, hash);

#ifdef BRDEBUG
    printf("Adding block index %u from branch index %u\n", block_idx, branch_idx);
#endif

    CBFreeBlock(block);
}

CBChainDescriptor *BRKnownBlocks(BRBlockChain *bc) {
    CBChainDescriptor *chain = CBNewChainDescriptor();
    if (chain == NULL) {
        fprintf(stderr, "Failed to create chain descriptor\n");
        exit(1);
    }

    /* block locator algorithm adapted from https://en.bitcoin.it/wiki/Protocol_Specification#getblocks */
    uint8_t branch_idx = bc->validator->mainBranch;
    CBBlockBranch branch = bc->validator->branches[branch_idx];
    uint32_t top_depth = branch.numBlocks - 1, i, step = 1, start = 0;
    for (i = top_depth; i > 0; i -= step, ++start) {
        if (start >= 10)
            step *= 2;
        BRChainDescriptorAddBlockIndex(chain, bc, branch_idx, i);
    }
    BRChainDescriptorAddBlockIndex(chain, bc, branch_idx, 0);

    return chain;
}

CBInventoryBroadcast *BRUnknownBlocksFromInv(BRBlockChain *bc, CBInventoryBroadcast *inv) {
    CBInventoryBroadcast *new_inv = CBNewInventoryBroadcast();
    int i;
    for (i = 0; i < inv->itemNum; ++i) {
        CBInventoryItem *item = inv->items[i];
#ifdef BRDEBUG
        char *type = item->type == CB_INVENTORY_ITEM_ERROR ? "ERROR" :
                    (item->type == CB_INVENTORY_ITEM_BLOCK ? "BLOCK" :
                                                        "TRANSACTION");
        printf("Inventory item %d (type %s)\n", i, type);
#endif

        if (item->type == CB_INVENTORY_ITEM_BLOCK) {
            if (!CBBlockChainStorageBlockExists(bc->validator, item->hash)) {
                ++new_inv->itemNum;
                new_inv->items = (CBInventoryItem **) realloc(new_inv->items,
                                new_inv->itemNum * sizeof(CBInventoryItem *));
                if (new_inv->items == NULL) {
                    perror("realloc failed");
                    exit(1);
                }

                /* add inventory item to new_inv */
                new_inv->items[new_inv->itemNum - 1] = item;
                CBRetainObject(item);
            } else {
 #ifdef BRDEBUG
                printf("\tBlock exists\n");
#endif
            }
        }
    }

    return new_inv;
}
