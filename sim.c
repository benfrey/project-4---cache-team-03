#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define NUMMEMORY 65536 /* maximum number of data words in memory */
#define NUMREGS 8 /* number of machine registers */

#define ADD 0
#define NAND 1
#define LW 2
#define SW 3
#define BEQ 4
#define JALR 5
#define HALT 6
#define NOOP 7

#define NOOPINSTRUCTION 0x1c00000

typedef struct entryStruct {
        int v; // valid bit
        int d; //dirty bit
        int tag;
        int lru;
        int *block;
} cacheEntry;

typedef struct stateStruct {
	int pc;
	int mem[NUMMEMORY];
	int reg[NUMREGS];
	int numMemory;
	cacheEntry **cache;
	int hits;
	int misses;
	int blkSize;
	int setAmt;
	int setAssoc;
} stateType;

/*
* Log the specifics of each cache action. *
* address is the starting word address of the range of data being transferred.
* size is the size of the range of data being transferred.
* type specifies the source and destination of the data being transferred. *
* cache_to_processor: reading data from the cache to the processor
* processor_to_cache: writing data from the processor to the cache
* memory_to_cache: reading data from the memory to the cache
* cache_to_memory: evicting cache data by writing it to the memory
* cache_to_nowhere: evicting cache data by throwing it away
*/
enum action_type {cache_to_processor, processor_to_cache, memory_to_cache, cache_to_memory, cache_to_nowhere};

void print_action(int address, int size, enum action_type type) {
	printf("transferring word [%i-%i] ", address, address + size - 1);
	if (type == cache_to_processor) {
		printf("from the cache to the processor\n");
	} else if (type == processor_to_cache) {
		printf("from the processor to the cache\n");
	} else if (type == memory_to_cache) {
		printf("from the memory to the cache\n");
	} else if (type == cache_to_memory) {
		printf("from the cache to the memory\n");
	} else if (type == cache_to_nowhere) {
		printf("from the cache to nowhere\n");
	}
}

void print_stats(stateType* state) {
	//printf("Cycles: %d\n", state->cycles);
	printf("Hits: %d\n", state->hits); // Update the state struct to include this variable
	printf("Misses: %d\n", state->misses); // Update the state struct to include this variable
}

// Print out cache (debug)
void print_cache(stateType* state) {
	int setAmt = state->setAmt;
	int setAssoc = state->setAssoc;

	for (int i=0; i<setAmt; i++){
		for (int j=0; j<setAssoc; j++){
			printf("v=%d,d=%d,t=%d,l=%d,b[0]=%d", state->cache[i][j].v, state->cache[i][j].d, state->cache[i][j].tag, state->cache[i][j].lru, state->cache[i][j].block[0]);
		}
		printf("\n");
	}
}

int field0(int instruction){
	return( (instruction>>19) & 0x7);
}

int field1(int instruction){
	return( (instruction>>16) & 0x7);
}

int field2(int instruction){
	return(instruction & 0xFFFF);
}

int opcode(int instruction){
	return(instruction>>22);
}

// Get block offset from an address
int getBlkOffset(int addr, stateType* state){
	int blkSize = state->blkSize;
	return(addr & (blkSize-1));
}

// Get set index from an address
int getSetIndex(int addr, stateType* state){
	int blkSize = state->blkSize;
	int setAmt = state->setAmt;
	int blkBits = 0;
	while (blkSize >>= 1) ++blkBits;
	return( (addr>>blkBits) & (setAmt-1) );
}

// Get tag from an address
int getTag(int addr, stateType* state){
	int blkSize = state->blkSize;
	int setAmt = state->setAmt;
        int blkBits = 0;
        while (blkSize >>= 1) ++blkBits;
	int setBits = 0;
        while (setAmt >>= 1) ++setBits;
	return  (addr>>(blkBits+setBits));
}

// Function to check if is power of 2.
int powerOfTwo(int n){
	if (n == 0){
    		return 0;
 	}

	while (n != 1){
		if (n%2 != 0){
         		return 0;
		}
      		n = n/2;
  	}
  	return 1;
}

// Reconstruct an address
int reconstructAddr(int tag, int setIndex, stateType* state){
	int blkSize = state->blkSize;
	int setAmt = state->setAmt;

	int blkBits = 0;
        while (blkSize >>= 1) ++blkBits;
        int setBits = 0;
        while (setAmt >>= 1) ++setBits;

	int blkPortion = 0; // block is our granular level for moving in and out of cache, blkOffset = 0
	int setPortion = setIndex<<blkBits;
	int tagPortion = tag<<(blkBits+setBits);

	//printf("Reconstructed Address: %d \n", (tagPortion | setPortion | blkPortion));
	return (tagPortion | setPortion | blkPortion);
}

// Check for hit (matching tag in the set)
int checkHit(int tag, int setIndex, stateType* state){
	int setAssoc = state->setAssoc;
        for (int i = 0; i < setAssoc; i++){
                if (tag == state->cache[setIndex][i].tag && state->cache[setIndex][i].v == 1) {
                        // Hit
			//printf("!HIT!\n");
                        state->hits++;
			return i;
                }
        }
	// Miss
	//printf("!MISS!\n");
	state->misses++;
	return -1;
}

// Update the LRU of each way in the set
void updateLRU(int targetWay, int setIndex, stateType* state){
	int setAssoc = state->setAssoc;

        for (int j = 0; j < setAssoc; j++){
        	if (state->cache[setIndex][j].lru < (setAssoc-1)){
                        state->cache[setIndex][j].lru++;
                }
        }
       	state->cache[setIndex][targetWay].lru = 0; // way we were dealing with
}

// Write back if any entry is dirty on halt
void dirtyWB(stateType* state){
	int blkSize = state->blkSize;
        int setAmt = state->setAmt;
        int setAssoc = state->setAssoc;

        for (int i=0; i<setAmt; i++){
                for (int j=0; j<setAssoc; j++){
                        if (state->cache[i][j].d == 1){
				//printf("DIRTY!\n");
				// Write back

				// Reassmble address to store into memory
                                int newTag = state->cache[i][j].tag;
                                int newAddr = reconstructAddr(newTag, i, state);

                                // Drop entire block into memory
                                //print_cache(state);
                                print_action(newAddr, blkSize, cache_to_memory); // going from cache to me$
                                for (int k = 0; k < blkSize; k++){
        	                        state->mem[newAddr] = state->cache[i][j].block[k];
                                        newAddr++;
                                }
                                state->cache[i][j].d = 0; // reset dirty bit
			}
		}
        }
}

// Invalidate bits
void invalidateBits(stateType* state){
        int setAmt = state->setAmt;
        int setAssoc = state->setAssoc;

        for (int i=0; i<setAmt; i++){
                for (int j=0; j<setAssoc; j++){
			state->cache[i][j].v = 0;
                }
        }
}

int signExtend(int num){
	// convert a 16-bit number into a 32-bit integer
	if (num & (1<<15) ) {
		num -= (1<<16);
	}
	return num;
}

// Function to determine if IF, LW, or SW. Returns -1 for LW or SW (no return necessary) or an instr int for IF.
int cacheOperation(stateType* state, int addrIn, int instr){
        // Recover info about cache
        int blkSize = state->blkSize; // get size of block
        int wayAmt = state->setAssoc; // size of "row", amount of ways
        int setAmt = state->setAmt; // size of "column", amount of sets

        // Manipulate address based on memory address format
        int addr = addrIn;
        int blkOffset =getBlkOffset(addr, state);
        int setIndex = getSetIndex(addr, state);
        int tag = getTag(addr, state);

        // Check for hit
        int matchingWay = checkHit(tag, setIndex, state);
        if (matchingWay != -1){ //hit
                updateLRU(matchingWay, setIndex, state); //update the LRU
        	if(opcode(instr) == LW){ // Load Word
			//print_cache(state);
                        print_action(addr, 1, cache_to_processor); //print out action to output
                        state->reg[field0(instr)] = state->cache[setIndex][matchingWay].block[blkOffset]; //populate reg with particular word
                        return -1;
                } else if(opcode(instr) == SW){ // Store word
			//print_cache(state);
                        print_action(addr, 1, processor_to_cache); //print out action to output
			state->cache[setIndex][matchingWay].block[blkOffset] = state->reg[field0(instr)]; //populate cache with instr from reg
                        state->cache[setIndex][matchingWay].d = 1; // set dirty bit
			return -1;
                } else { // Instruction fetch
			//print_cache(state)
	                print_action(addr, 1, cache_to_processor); //print action to output
        	        return (state->cache[setIndex][matchingWay].block[blkOffset]); //return instr from mem
		}
        } else { // miss
                // Determine best way to populate
                int bestWay;
                for (int i = 0; i < wayAmt; i++){ // identify LRU way
                        if ((wayAmt-1) == state->cache[setIndex][i].lru){
                                bestWay = i;
                                break;
                        }
                }
		for (int i = 0; i < wayAmt; i++){ // identify LRU way
                       	if ((wayAmt-1) == state->cache[setIndex][i].lru && state->cache[setIndex][i].v == 0){
				bestWay = i;
				break;
			}
		}

                // Reassmble address to store into memory
                int newTag = state->cache[setIndex][bestWay].tag;
                int newAddr = reconstructAddr(newTag, setIndex, state);

                //printf("WAY: %d\n", bestWay);
		//printf("LRU: %d\n", state->cache[setIndex][bestWay].lru);
		//printf("V BIT: %d\n", state->cache[setIndex][bestWay].v);

                // Found way to populate, if dirty write to mem (write-back policy!)
                if (state->cache[setIndex][bestWay].d == 1){
                	//printf("DIRTY!\n");

                        // Drop entire block into memory
                        //print_cache(state);
                        print_action(newAddr, blkSize, cache_to_memory); // going from cache to memory, print this action.
                        for (int j = 0; j < blkSize; j++){
	                        state->mem[newAddr] = state->cache[setIndex][bestWay].block[j];
                                newAddr++;
                        }
                        state->cache[setIndex][bestWay].d = 0; // reset dirty bit
                } else if (state->cache[setIndex][bestWay].v == 1) { // When v == 1, the entry is valid and we need to throw it away.
                        // Not dirty, write to nowhere (evict)
                        //print_cache(state);
                        print_action(newAddr, blkSize, cache_to_nowhere);
                }

		if(opcode(instr) == LW){ // Load Word (mem to cache, then cache to processor)
                	// Pull data from mem into cache
                      	int newAddr = reconstructAddr(tag, setIndex, state);
                      	//print_cache(state);
                      	print_action(newAddr, blkSize, memory_to_cache);
                        for (int j = 0; j < blkSize; j++){
                              	state->cache[setIndex][bestWay].block[j] = state->mem[newAddr];
                              	newAddr++;
                      	}

                        // Finally, grab the instr from cache
                        //print_cache(state);
                        print_action(addr, 1,  cache_to_processor);
                        state->reg[field0(instr)] = state->cache[setIndex][bestWay].block[blkOffset];

              		// Update LRUs of set, tag, valid bit
               		updateLRU(bestWay, setIndex, state);
	                state->cache[setIndex][bestWay].tag = tag;
			state->cache[setIndex][bestWay].v = 1;

			return -1;
		} else if(opcode(instr) == SW){ // Store word (processor to cache, but not cache to mem (handled on eviction)
			// Pull data from mem into cache
                        int newAddr = reconstructAddr(tag, setIndex, state);
                        //print_cache(state);
                        print_action(newAddr, blkSize, memory_to_cache);
                        for (int j = 0; j < blkSize; j++){
	                        state->cache[setIndex][bestWay].block[j] = state->mem[newAddr];
                                newAddr++;
                        }

			//print_cache(state);
			print_action(addr, 1, processor_to_cache); //print out action to output
			state->cache[setIndex][bestWay].block[blkOffset] = state->reg[field0(instr)]; //populate cache with instr from reg
                        state->cache[setIndex][bestWay].d = 1; // set dirty bit

                        // Update LRUs of set, tag, valid bit
                        updateLRU(bestWay, setIndex, state);
	                state->cache[setIndex][bestWay].tag = tag;
	                state->cache[setIndex][bestWay].v = 1;

                        return -1;
                } else { // IF
			// Pull data from mem into cache
                        newAddr = addr;
                        //print_cache(state);
			//printf("blkSize: %d", blkSize);
	                print_action(newAddr, blkSize, memory_to_cache);
        	        for (int j = 0; j < blkSize; j++){
                        	state->cache[setIndex][bestWay].block[j] = state->mem[newAddr];
                                newAddr++;
                      	}

                        // Finally, grab the instr from cache
                        //print_cache(state);
                        print_action(addr, 1, cache_to_processor);

	                // Update LRUs of set, tag, valid bit
     	                updateLRU(bestWay, setIndex, state);
           	        state->cache[setIndex][bestWay].tag = tag;
               	        state->cache[setIndex][bestWay].v = 1;

                	return (state->cache[setIndex][bestWay].block[blkOffset]);
		}
        }
}

void run(stateType* state){

	// Reused variables;
	int instr = 0;
	int regA = 0;
	int regB = 0;
	int offset = 0;
	int branchTarget = 0;
	int aluResult = 0;

	int total_instrs = 0;

	// Primary loop
	while(1){
		total_instrs++;

		//printState(state);
		// Stuff here

		// Instruction Fetch
		instr = cacheOperation(state, state->pc, -1);

		/* check for halt */
		if (opcode(instr) == HALT) {
			dirtyWB(state); // write back any dirty cache entries to mem
			invalidateBits(state); // invalidate all cache entries
			//printf("machine halted\n");
			break;
		}

		// Increment the PC
		state->pc = state->pc+1;

		// Set reg A and B
		regA = state->reg[field0(instr)];
		regB = state->reg[field1(instr)];

		// Set sign extended offset
		offset = signExtend(field2(instr));

		// Branch target gets set regardless of instruction
		branchTarget = state->pc + offset;

		// ADD
		if(opcode(instr) == ADD){
			// Add
			aluResult = regA + regB;
			// Save result
			state->reg[field2(instr)] = aluResult;
		}
		// NAND
		else if(opcode(instr) == NAND){
			// NAND
			aluResult = ~(regA & regB);
			// Save result
			state->reg[field2(instr)] = aluResult;
		}
		// LW or SW
		else if(opcode(instr) == LW || opcode(instr) == SW){
			// Calculate memory address
			aluResult = regB + offset;

			// Tap into cache and determine what to do
			cacheOperation(state, aluResult, instr);
		}
		// JALR
		else if(opcode(instr) == JALR){
			// rA != rB for JALR to work
			// Save pc+1 in regA
			state->reg[field0(instr)] = state->pc;
			//Jump to the address in regB;
			state->pc = state->reg[field1(instr)];
		}
		// BEQ
		else if(opcode(instr) == BEQ){
			// Calculate condition
			aluResult = (regA - regB);

			// ZD
			if(aluResult==0){
				// branch
				state->pc = branchTarget;
			}
		}
	} // While
	print_stats(state);
}

int main(int argc, char** argv){

	/** Get command line arguments **/
	char* fname;
	char* blockSizeChar = NULL;
	char* numSetsChar = NULL;
	char* setAssocChar = NULL;

	opterr = 0;

	int cin = 0;

	while((cin = getopt(argc, argv, "f:b:s:a:")) != -1){
		switch(cin)
		{
			case 'f':
				fname=(char*)malloc(strlen(optarg));
				fname[0] = '\0';

				strncpy(fname, optarg, strlen(optarg)+1);
				//printf("FILE: %s\n", fname);
				break;
			case 'b':
                                blockSizeChar=(char*)malloc(strlen(optarg));
                                blockSizeChar[0] = '\0';

                                strncpy(blockSizeChar, optarg, strlen(optarg)+1);
				break;
			case 's':
                                numSetsChar=(char*)malloc(strlen(optarg));
                                numSetsChar[0] = '\0';

                                strncpy(numSetsChar, optarg, strlen(optarg)+1);
                                break;
			case 'a':
                                setAssocChar=(char*)malloc(strlen(optarg));
                                setAssocChar[0] = '\0';

                                strncpy(setAssocChar, optarg, strlen(optarg)+1);
                                break;
			case '?':
				if(optopt == 'i'){
					printf("Option -%c requires an argument.\n", optopt);
				}
				else if(isprint(optopt)){
					printf("Unknown option `-%c'.\n", optopt);
				}
				else{
					printf("Unknown option character `\\x%x'.\n", optopt);
					return 1;
				}
				break;
			default:
				abort();
		}
	}

	FILE *fp = fopen(fname, "r");
	if (fp == NULL) {
		printf("Cannot open file '%s' : %s\n", fname, strerror(errno));
		return -1;
	}

	/* count the number of lines by counting newline characters */
	int line_count = 0;
	int c;
	while (EOF != (c=getc(fp))) {
		if ( c == '\n' ){
			line_count++;
		}
	}
	// reset fp to the beginning of the file
	rewind(fp);

	// Malloc state and initialize
	stateType* state = (stateType*)malloc(sizeof(stateType));
	state->pc = 0;
	memset(state->mem, 0, NUMMEMORY*sizeof(int));
	memset(state->reg, 0, NUMREGS*sizeof(int));
	state->numMemory = line_count;

	char line[256];

	int memLine = 0;
	while (fgets(line, sizeof(line), fp)) {
		/* note that fgets doesn't strip the terminating \n, checking its
		   presence would allow to handle lines longer that sizeof(line) */
		state->mem[memLine] = atoi(line);
		memLine++;
	}
	fclose(fp);

	int numSets; // number of sets in cache
        int setAssoc; // 1 = direct mapped, 256 = full assoc (assuming block size 1 $
        int blkSize; // amount of words in block

      	// Update state's cache with proper dimensions of array
	if(strspn(numSetsChar, "0123456789") == strlen(numSetsChar) && strspn(setAssocChar, "0123456789") == strlen(setAssocChar) && strspn(blockSizeChar, "0123456789") == strlen(blockSizeChar)){
		numSets = atoi(numSetsChar);
                setAssoc = atoi(setAssocChar);
		blkSize = atoi(blockSizeChar);
	} else {
		printf("Invalid cache parameter \n");
		exit(-1);
	}

	// Check if each param is a power of two
	if (powerOfTwo(numSets) != 1 || powerOfTwo(setAssoc) != 1 || powerOfTwo(blkSize) != 1){
		printf("Error, one of the input params is not a power of two \n");
                exit(-1);
	}

	//printf("-----\nInputs: \n");
	//printf("blkSize: %d \n", blkSize);
        //printf("numSets: %d \n", numSets);
        //printf("setAssoc: %d \n-----\n", setAssoc);

	// Ensure that amount of blocks does not exceed 256
	if ((numSets*setAssoc) > 256) {
		printf("Max amount of blocks in cache exceeds spec of 256 \n");
		exit(-1);
	}

	// Set dimensions of cache
	state->blkSize = blkSize;
	state->setAssoc = setAssoc;
	state->setAmt = numSets;

        // Create 2D cache array
	cacheEntry **cache = (cacheEntry**)malloc(numSets*sizeof(cacheEntry*));
	for (int i = 0; i < numSets; ++i) {	// through sets
		cache[i] = (cacheEntry*)malloc(setAssoc*sizeof(cacheEntry)); // numSets of size setAssoc (ways)

		for (int j = 0; j < setAssoc; j++) { // through ways
                        // Initialize cache entries
			cache[i][j].lru = setAssoc - 1;
			cache[i][j].v = 0;
			cache[i][j].d = 0;
			cache[i][j].tag = 0;
			cache[i][j].block = (int*)malloc((blkSize)*sizeof(int));
		}
	}

	// Define cache in state
	state->cache = cache;

	/** Run the simulation **/
	run(state);

	free(state);
	free(fname);
}
