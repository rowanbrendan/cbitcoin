#ifndef BRBLOCKCHAIN_H_
#define BRBLOCKCHAIN_H_

#include <stdint.h>
#include "CBFullValidator.h"
#include "CBChainDescriptor.h"
#include "CBInventoryBroadcast.h"

typedef struct {
    uint64_t storage;
    CBFullValidator *validator;
} BRBlockChain;

BRBlockChain *BRNewBlockChain(char *);
CBChainDescriptor *BRKnownBlocks(BRBlockChain *);
CBInventoryBroadcast *BRUnknownBlocksFromInv(BRBlockChain *, CBInventoryBroadcast *);

#endif
