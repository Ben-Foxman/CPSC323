/*
 * Ben Foxman | netid=btf28 | HW4-lzw | Due 11/13/2020
 * This program implements compression and decompression filters
 * using the LZW compression algorithm.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include "/c/cs323/Hwk4/code.h"

typedef struct __attribute__((packed, aligned(1))) entry{ // entry in the string table
    unsigned int PREFIX : 21; // PREF (max 20 bits + special values)
    unsigned int NEXT: 21; // NEXT (for encode chaining)
    unsigned int PRUNE: 1; // boolean value: prune this entry or not
    char CHAR; // CHAR
}Entry;

short NBITS = 9; // start with 9 bits
short MAXBITS = 12; // can't change table anymore
unsigned int nChains = 63; // number of chains in the hash table
unsigned int numEntries = 0; // number of entries in the table (originally empty)
unsigned int SIZE = 512; //size of decode table (originally 2^9 = 512)
unsigned int pruneOn = 0; // decide to prune or not
unsigned int NULLNEXT = 1 << 20; // null value which is unsigned

unsigned int *t; // encode hashtable is array of integers (heads of chains)
Entry *entries;  // array of entries for encode/decode

void encode();
void decode();
void initializeTable(char encode);
void insertEncode(unsigned int C, char K, unsigned int prune);
void insertDecode(unsigned int C, char K, unsigned int prune);
unsigned int searchEncode(unsigned int C, char K);
void rehashEncode(unsigned int newSize);
void pruneTable(unsigned int encode);
unsigned int hash(unsigned int PREFIX, char CHAR);
void dumpTable(char *nameOfFile);

int main(int argc, char *argv[]) {
    static char bin[64], bout[64];             // Use small static buffers
    setvbuf (stdin,  bin,  _IOFBF, 64);
    setvbuf (stdout, bout, _IOFBF, 64);
    setenv("STAGE", "3", 1);
    if (!strcmp(argv[0], "./encode")){ // encode
        for (int i = 1; i < argc; i++){
            if (!strcmp(argv[i], "-m")){
                if (i + 1 == argc){ // -m with no argument
                    fprintf(stderr, "Encode takes the form [-m MAXBITS] -p\n");
                    exit(1);
                }
                MAXBITS = atoi(argv[i + 1]); // assume MAXBITS is representable as long int
                if (MAXBITS <= 8 || MAXBITS > 20){
                    MAXBITS = 12;
                }
                i++;
            }
            else if (!strcmp(argv[i], "-p")){
                pruneOn = 1;
            }
            else {
                fprintf(stderr, "Encode takes the form [-m MAXBITS] -p\n");
                exit(1);
            }
        }
        t = malloc(sizeof(int) * nChains); // hashtable has 63 chains to start
        for (int i = 0; i < nChains; i++){
            t[i] = NULLNEXT; // null chain
        }
        entries = malloc(SIZE * sizeof(Entry)); // encode array has 512 entries to start
        initializeTable(0);
        encode();
    }
    else if (!strcmp(argv[0], "./decode")){ // decode
        if (argc > 1){ // decode takes no arguments
            fprintf(stderr, "Decode takes no arguments.\n");
            exit(1);
        }
        entries = malloc(SIZE * sizeof(Entry));
        initializeTable(1);
        decode();
    }
    else {
        fprintf(stderr, "To use lzw: [encode|decode].\n");
        exit(1);
    }
}

// encode follows the exact pseudocode which was laid out in the spec
void encode(){
    putBits(NBITS, MAXBITS); // add the MAXBITS so decode will know it
    unsigned int C = 0; // empty string
    short K;
    while ((K = getchar()) != EOF){
        unsigned int code;
        if ((code = searchEncode(C, K)) != NULLNEXT){ // in table
            C = code;
        }
        else {
            putBits(NBITS, C);
            insertEncode(C, K, 1);
            C = searchEncode(0, K);
        }
    }
    if (C > 0){ // not the empty string
        putBits(NBITS, C);
    }
    flushBits();
}

// decode follows the d-delay decode psuedocode laid out in the spec
void decode(){
    MAXBITS = getBits(NBITS); // the first code is the number of maxbits
    char *stack = malloc(1); // string emulates stack of chars
    int stackSize = 0; // size of stack
    char finalK;
    unsigned int newC, C, oldC = 0;
    while ((newC = C = getBits(NBITS)) != EOF){ // one code per line of stdin
        if (C == 257){ // grow the table
            NBITS++;
            SIZE = SIZE * 2;
            entries = realloc(entries, SIZE * sizeof(Entry));
            continue;
        }
        if (C == 258){ // prune the table
            pruneTable(0);
            continue;
        }
        if (C >= numEntries){
            stackSize++;
            stack = realloc(stack, stackSize);
            stack[stackSize - 1] = finalK;
            C = oldC;
        }
        // encode can't write this (0 = empty code, C should be lower than numEntries by now)
        if (C >= numEntries || C == 0){
            free(stack);
            fprintf(stderr, "Decode: trying to decode a file encode could not have written.\n");
            exit(0);
        }
        while (entries[C].PREFIX != 0){
            stackSize++;
            stack = realloc(stack, stackSize);
            stack[stackSize - 1] = entries[C].CHAR;
            C = entries[C].PREFIX;
        }

        finalK = entries[C].CHAR;
        putchar(finalK);

        while (stackSize > 0){
            stackSize--;
            putchar(stack[stackSize]);
            stack[stackSize] = '\0';
        }
        if (oldC != 0){
            insertDecode(oldC, finalK, 1); // entries must "prove" they shouldn't be pruned
        }
        oldC = newC;
    }
    free(stack);
}

// insert initial 256 ASCII codes + EMPTY/GROW/PRUNE into string table
void initializeTable(char encode){
    for (int i = 0; i < 259; i++){
        if (i == 0){
            if (encode == 0){
                insertEncode(NULLNEXT + 1, -1, 0); // empty string
            }
            else{
                insertDecode(NULLNEXT + 1, -1, 0);
            }
        }
        if (i > 0 && i <= 256){
            if (encode == 0) {
                insertEncode(0, i - 1, 0); // one character strings
            }
            else{
                insertDecode(0, i - 1, 0);
            }

        }
        if (i == 257){
            if (encode == 0) {
                insertEncode(NULLNEXT + 2, -1, 0); // grow
            }
            else{
                insertDecode(NULLNEXT + 2, i -1, 0);
            }
        }
        if (i == 258){
            if (encode == 0){
                insertEncode(NULLNEXT + 3, -1, 0); // prune
            }
            else{
                insertDecode(NULLNEXT + 3, -1, 0);
            }
        }
    }
}

void insertEncode(unsigned int C, char K, unsigned int prune){ // insert into hashtable for encode
    /// resize if necessary and possible
    if (NBITS < MAXBITS && numEntries == SIZE){
        SIZE = SIZE * 2; // Size of array
        nChains = nChains * 2; // Chains in hashtable
        entries = realloc(entries, SIZE * sizeof(Entry));
        rehashEncode(nChains); // rehash the encode
        putBits(NBITS, 257);
        NBITS++;
    }
    else if (pruneOn == 1 && numEntries == SIZE){ // prune if possible
        putBits(NBITS, 258);
        pruneTable(1);
    }

    if (numEntries < SIZE) { // entries not full
        /// INSERTION INTO HASHTABLE
        unsigned int h = hash(C, K); // index into t
        if (t[h] == NULLNEXT){ // chain does not yet exist
            t[h] = numEntries;
        }
        else { // chain does exist, follow it
            unsigned int entry = t[h];
            while (entries[entry].NEXT != NULLNEXT){ // not end of chain
                entry = entries[entry].NEXT; // "follow the chain"
            }
            entries[entry].NEXT = numEntries; // next element is the number of entries
        }
        // insertion for here on out is exactly the same for encode + decode
        insertDecode(C, K, prune);
    }
}

// insertion into string table without regard to hash table
void insertDecode(unsigned int C, char K, unsigned int prune){
    if (numEntries < SIZE){
        entries[numEntries].PREFIX = C;
        entries[numEntries].CHAR = K;
        entries[numEntries].NEXT = NULLNEXT;
        entries[numEntries].PRUNE = prune;
        if (C < NULLNEXT){
            entries[C].PRUNE = 0;
        }
        numEntries++;
    }
}

// for encode: use hashtable with chaining for faster (PREF, CHAR) lookup
unsigned int searchEncode(unsigned int C, char K){
    unsigned int h = hash(C, K);
    if (t[h] == NULLNEXT){
        return NULLNEXT;
    }
    unsigned int curr = t[h];
    while(curr != NULLNEXT){
        if (entries[curr].PREFIX == C && entries[curr].CHAR == K){
            return curr;
        }
        curr = entries[curr].NEXT;
    }
    return NULLNEXT;
}

// for encode: resize the hashtable to have newSize chains
void rehashEncode(unsigned int newSize){
    t = realloc(t, sizeof(int) * newSize); // new array
    for (int i = 0; i < newSize; i++){ // reset all chain heads in hashtable
        t[i] = NULLNEXT;
    }
    for (int i = 0; i < numEntries; i++) {
        entries[i].NEXT = NULLNEXT; // reset chains in entries
    }
    for (int i = 0; i < numEntries; i++){ // insert all elements from entries into t (update the next values)
        unsigned int h = hash(entries[i].PREFIX, entries[i].CHAR);
        if (t[h] == NULLNEXT){ // chain does not yet exist
            t[h] = i; // start of chain
        }
        else { // chain does exist, follow it
            unsigned int entry = t[h];
            while (entries[entry].NEXT != NULLNEXT){ // not end of chain
                entry = entries[entry].NEXT; // "follow the chain"
            }
            entries[entry].NEXT = i; // next element is i
        }
    }
}

// for -p: prune the string table + update the hashtable if necessary (encode)
void pruneTable(unsigned int encode){
    // reuse the next field of the struct to store offset (memory saving)
    for (int i = 0; i < numEntries; i++){
        entries[i].NEXT = 0;
    }
    unsigned int offset = 0;
    for (int i = 259; i < numEntries; i++) { // get the new count of entries (first 259 will always stay the same)
        if (entries[i].PRUNE == 1){ // not in the new table
           offset++;
        }
        entries[i].NEXT = offset; //store the new offset

        if (entries[i].PRUNE == 0) { // in the new table - shift it down by offset
            entries[i - offset].PREFIX = entries[i].PREFIX - entries[entries[i].PREFIX].NEXT; // shift the prefix down
            entries[i - offset].CHAR = entries[i].CHAR;
            entries[i - offset].PRUNE = 1; // entries must prove they deserve not to be pruned
        }
    }
    numEntries -= offset; // amount of entries in the new table
    SIZE = numEntries; // SIZE = smallest power of 2 greater than or equal to entries
    SIZE--;
    SIZE |= SIZE >> 1;
    SIZE |= SIZE >> 2;
    SIZE |= SIZE >> 4;
    SIZE |= SIZE >> 8;
    SIZE |= SIZE >> 16;
    SIZE++;
    NBITS = 0; // change NBITS accordingly
    unsigned int s = SIZE;
    while (s > 1){
        NBITS++;
        s >>= 1;
    }
    for (int i = 259; i < numEntries; i++){
        entries[entries[i].PREFIX].PRUNE = 0; // don't prune things which are prefixes of other things
    }
    // if necessary - resize the hashtable accordingly
    if (encode == 1) {
        nChains = (SIZE >> 3) - 1; // new number of chains in the hashtable
        rehashEncode(nChains);
    }
}

// hash function used by encode
unsigned int hash(unsigned int PREFIX, char CHAR){
    return (((unsigned long)(PREFIX) << CHAR_BIT) | ((unsigned)(CHAR))) % nChains;
}

// for debugging: print the tables
void dumpTable(char *nameOfFile){
    FILE *f = fopen(nameOfFile,"w");
    for (int i = 0; i < numEntries; i++){
        if (entries[i].PREFIX == -1){ // empty string
            fprintf(f, "%d - EMPTY : %d\n", i, entries[i].PRUNE);
        }
        else if (entries[i].PREFIX == -2){ // grow
            fprintf(f, "%d - GROW : %d\n", i, entries[i].PRUNE);
        }
        else if (entries[i].PREFIX == -3){ // prune
            fprintf(f, "%d - PRUNE : %d\n", i, entries[i].PRUNE);
        }
        else if (entries[i].PREFIX == 0){ // one character
            fprintf(f, "%d - EMPTY:%c : %d\n ", i, entries[i].CHAR, entries[i].PRUNE);
        }
        else { // all other entries
            fprintf(f, "%d - %d:%c : %d\n",i, entries[i].PREFIX, entries[i].CHAR, entries[i].PRUNE);
        }
    }
    fclose(f);
}
