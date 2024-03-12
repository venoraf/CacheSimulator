#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

typedef struct addr {
  char address[50];
  unsigned long addressVal;
  unsigned long Atag;
  int setinx;
  int blockOffset;
} addr;

typedef struct block {
  int id;
  //Tag and Valid for the block
  unsigned long tag;
  int evictPos;
  int valid;
} block;

typedef struct set {
  int id;
  int currentinx;
  block* blocks;
} set;

typedef struct cache {
  int id;
  set* sets;
  int memSize;
  int assoc;
  int blockSize;
  int setCt;
  int fifo; //0-fifo; 1-lru
  int currentPos;

  /* address size*/
  int blockbits;
  int setbits;
  int tagbits;

  /*Stats*/
  int memRead;
  int memWrite;
  int cacheHit;
  int cacheMiss;
}cache;

cache *l1cache;
addr* adPtr;

unsigned long getTagbits(unsigned long val, int blockbits, int setbits) {
    unsigned long ret = 0;
    for (int i = 0; i < setbits + blockbits; i++) {
        ret = (ret << 1) + 1;
    }
    return (val & ~ret);
}

unsigned long getBlockbits(unsigned long val, int bits) {
    unsigned long ret = 0;
    for (int i = 0; i < bits; i++) {
        ret = (ret << 1) + 1;
    }
    return (val & ret);
}

unsigned long getSetbits(unsigned long val, int blockbits, int setbits) {
    unsigned long ret = 0;
    for (int i = 0; i < blockbits; i++) {
        val = (val >> 1);
    }
    for (int i = 0; i < setbits; i++) {
        ret = (ret << 1) + 1;
    }
    ret = val & ret;
    return ret;
}

void convertAddress(cache *lcache, addr* ptr) {
    ptr->addressVal = strtol(ptr->address, NULL, 0);
    ptr->Atag = getTagbits(ptr->addressVal, lcache->blockbits, lcache->setbits);
    ptr->blockOffset = getBlockbits(ptr->addressVal, lcache->blockbits);
    ptr->setinx = getSetbits(ptr->addressVal, lcache->blockbits, lcache->setbits);
}

int logbase2(int val) {
    int logval = 0;
    while (val > 1) {
        logval++;
        val = val / 2;
    }
    return logval;
}

void createCache(cache *lcache) {
    lcache->setCt = lcache->memSize / (lcache->blockSize * lcache->assoc);
    lcache->blockbits = logbase2(lcache->blockSize);
    lcache->setbits = logbase2(lcache->setCt);
    lcache->memRead = 0;
    lcache->memWrite = 0;
    lcache->cacheHit = 0;
    lcache->cacheMiss = 0;
    //malloc array of set
    lcache->sets = (set*)malloc(sizeof(set) * lcache->setCt);
    for (int i = 0; i < lcache->setCt; i++) {
        //Create sets with the blocks
        lcache->sets[i].currentinx = 0;
        lcache->sets[i].id = i;
        lcache->sets[i].blocks = (block*)malloc(sizeof(block) * lcache->assoc);
        for (int j = 0; j < lcache->assoc; j++) {
            lcache->sets[i].blocks[j].id = j;
            lcache->sets[i].blocks[j].valid = 0;
            lcache->sets[i].blocks[j].evictPos = -1;
        }
    }
}

void destroyCache(cache *lcache) {
    for (int i = 0; i < lcache->setCt; i++) {
        free(lcache->sets[i].blocks);
    }
    free(lcache->sets);
    free(lcache);
};

int checkSetFor(cache *lcache, addr *ptr) {
    int evict = 1;
    int inx = ptr->setinx;
    for (int i = 0; i < lcache->assoc; i++) {
        if (lcache->sets[inx].blocks[i].valid == 0) {
            evict = 0;
        }
        else if (lcache->sets[inx].blocks[i].tag == ptr->Atag) {
            //if (lcache->id == 2) printf("found hit %x\n", ptr->Atag);
            return i;
        }
    }
    if (evict == 1) return -2;
    return -1;
}

int getFreeBlock(cache *lcache, addr *ptr) {
    for (int i = 0; i < lcache->assoc; i++) {
        if (lcache->sets[ptr->setinx].blocks[i].valid == 0) {
            return i;
        }
    }
    return -1;
}

void evictBlockl1(cache *lcache, addr *ptr) {
    for (int i = 0; i < lcache->assoc; i++) {
        if (lcache->sets[ptr->setinx].blocks[i].evictPos == 0) {
            lcache->sets[ptr->setinx].blocks[i].valid = 0;
            lcache->sets[ptr->setinx].blocks[i].evictPos = -1;
        }
        else
            lcache->sets[ptr->setinx].blocks[i].evictPos--;
    }
    lcache->sets[ptr->setinx].currentinx--;
}

int readCachel1(addr *ptr) {
    int setInx = ptr->setinx;
    int inx = checkSetFor(l1cache, ptr);
    if (inx >= 0) {
        if (l1cache->fifo == 1) {
            for (int i = 0; i < l1cache->assoc; i++) {
                if (l1cache->sets[setInx].blocks[i].evictPos > l1cache->sets[setInx].blocks[inx].evictPos) {
                    l1cache->sets[setInx].blocks[i].evictPos--;
                }
            }
            l1cache->sets[setInx].blocks[inx].evictPos = l1cache->sets[setInx].currentinx - 1;
        }
        l1cache->cacheHit++;
    }
    else {
        l1cache->cacheMiss++;
        if (inx == -2) {
            evictBlockl1(l1cache, ptr);
        } 
        l1cache->memRead++;
        inx = getFreeBlock(l1cache, ptr);
        l1cache->sets[setInx].blocks[inx].valid = 1;
        l1cache->sets[setInx].blocks[inx].tag = ptr->Atag;
        l1cache->sets[setInx].blocks[inx].evictPos = l1cache->sets[setInx].currentinx++;
    }
    return inx;
}

void writeCachel1(addr *ptr) {
  readCachel1(ptr);
  l1cache->memWrite++;
}

int main(int argc, char* argv[]) {
    l1cache = malloc(sizeof(cache));
    l1cache->id = 1;
    if (argc == 1)
    {
        printf("\nNo arg recieved");
        return -1;
    }

    if (argc > 9)
    {
        printf("\nToo many arguments: %d where only 9 expected", argc - 1);
        return -2;
    }
    FILE* dataFile;
    char str[1000];
    char* token;

    //L1 Cache Properties
    l1cache->memSize = atoi(argv[1]);
    token = strtok(argv[2], ":");
    token = strtok(NULL, ":");
    l1cache->assoc = atoi(token);
    if (strcmp(argv[3], "fifo") == 0)
        l1cache->fifo = 0;
    else if (strcmp(argv[3], "lru") == 0)
        l1cache->fifo = 1;
    l1cache->blockSize = atoi(argv[4]);
    //Create the Cache DataStructure
    createCache(l1cache);
    l1cache->blockbits = logbase2(l1cache->blockSize);
    l1cache->setbits = logbase2(l1cache->setCt);
    //printf("blocksize %d, block bits : %d, setCt : %d, set Bits: %d\n", l1cache->blockSize, l1cache->blockbits, l1cache->setCt, l1cache->setbits);
    //Cache Size = Sets × Cache Lines × Block Size
    
    dataFile = fopen(argv[5], "r");
    adPtr = malloc(sizeof(addr));

    while (fgets(str, 50, dataFile) != NULL) {
        char *op, *addr;
        op = strtok(str, " ");
        addr = strtok(NULL, " ");
        addr[strcspn(addr, "\r\n")] = '\0';
        strcpy(adPtr->address, addr);
        convertAddress(l1cache, adPtr);
        //printf("op %s, addr %s, set : %d\n", op, addr, adPtr->setinx);
        if (strcmp(op, "R") == 0) {
            readCachel1(adPtr);
        }
        else if (strcmp(op, "W") == 0) {
            writeCachel1(adPtr);
        }
        else {
            printf("invalid op\n");
        }
    }
    printf("memread:%d\n", l1cache->memRead);
    printf("memwrite:%d\n", l1cache->memWrite);
    printf("cachehit:%d\n", l1cache->cacheHit);
    printf("cachemiss:%d\n", l1cache->cacheMiss);
    fclose(dataFile);
    destroyCache(l1cache);
    free(adPtr);    
}
