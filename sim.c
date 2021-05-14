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
	cacheEntry  **cache;
	int hits;
	int misses;
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
	printf("Hits: %d\n", state->hits); // Update the state struct to include this variable
	printf("Misses: %d\n", state->misses); // Update the state struct to include this variable
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

int getBlkOffset(int addr, int blkSize){
	return(addr & (blkSize-1));
}

int getSetIndex(int addr, int setAmt, int blkSize){
	int blkBits = 0;
	while (blkSize >>= 1) ++blkBits;
	return( (addr>>blkBits) & (setAmt-1) );
}

int getTag(int addr, int setAmt, int blkSize){
        int blkBits = 0;
        while (blkSize >>= 1) ++blkBits;
	int setBits = 0;
        while (setAmt >>= 1) ++setBits;
	return  (addr>>(blkBits+setBits));
}

int* reconstructAddr(int tag, int setIndex, int setAmt, int blkSize){
        int blkBits = 0;
        while (blkSize >>= 1) ++blkBits;
        int setBits = 0;
        while (setAmt >>= 1) ++setBits;

	int blkPortion = 0; // block is our granular level for moving in and out of cache, blkOffset = 0
	int setPortion = setIndex<<blkBits;
	int tagPortion = setIndex<<(blkBits+setBits);

	return (tagPortion | setPortion | blkPortion);
}

// Check for hit (matching tag in the set)
int checkHit(int tag, int setIndex, int wayAmt, stateType* state){
        for (int i = 0; i < wayAmt; i++){
                if (tag == state->cache[setIndex][i].tag) {
                        // Hit
                        state->hits++;
			return 1;
                }
        }
	// Miss
	state->misses++;
	return 0;
}

// Update the LRU of each way in the set
void updateLRU(int targetWay, int setIndex, int wayAmt, stateType* state){
        for (int j = 0; j < wayAmt; j++){
        	if (state->cache[setIndex][j].lru < (wayAmt-1)){
                        state->cache[setIndex][j].lru++;
                }
        }
       	state->cache[setIndex][targetWay].lru = 0; // way we were dealing with
}

int signExtend(int num){
	// convert a 16-bit number into a 32-bit integer
	if (num & (1<<15) ) {
		num -= (1<<16);
	}
	return num;
}

int cacheFetch(stateType* state) {
        // Recover info about cache
        int blkSize = sizeof(state->cache[0][0].block); // get size of block
        int totBlocks = sizeof(state->cache); // total amount of blocks in cache
        int wayAmt = sizeof(state->cache[0]); // size of row, amount of ways
        int setAmt = totBlocks/wayAmt; // size of column, amount of sets

	// Manipulate address based on memory address format
	int addr = state->pc;
	int blkOffset =getBlkOffset(addr, blkSize);
	int setIndex = getSetIndex(addr, setAmt, blkSize);
	int tag = getTag(addr, setAmt, blkSize);

	// Check for hit
	int matchingWay = checkHit(tag, setIndex, wayAmt, state);
	if (matchingWay != -1){
                updateLRU(matchingWay, setIndex, wayAmt, state); //update the LRU
 		print_action(addr, blkSize, cache_to_processor); //print action to output
                return (state->cache[setIndex][matchingWay].block[blkOffset]); //return instr from mem
	} else { // miss
		for (int i = 0; i < wayAmt; i++){
			if ((wayAmt-1) == state->cache[setIndex][i].lru){
 				// Found way to populate, if dirty write to mem (write-back policy!)
				if (state->cache[setIndex][i].d = 1){
					// Reassmble address to store into memory
					int* newAddr = reconstructAddr(tag, setIndex, setAmt, blkSize); // will return start of array in mem (pointer)
					state->mem[newAddr] == state->cache[setIndex][i].block; // not sure if this is proper for block size of 4, for example
					print_action(*newAddr, blkSize, cache_to_memory); // going from cache to memory, print this action.
					state->cache[setIndex][i].d = 0; // reset dirty bit
				} else {
					// Not dirty, write to nowhere (evict)
					print_action(addr, blkSize, cache_to_nowhere);
				}

				// Pull in new data
				state->cache[setIndex][i].block = state->mem[addr]*; // not sure if this is proper for block size of 4, for example
				state->cache[setIndex][i].v = 1; // valid bit set if it was previously 0.

				// Update LRUs of set
				updateLRU(i, setIndex, wayAmt, state);

				// Finally, grab the instr from cache
				print_action(addr, blkSize, cache_to_processor);
				return (state->cache[setIndex][i].block[blkOffset]);
			}
		}
	}
}

void cacheLoadStore(stateType* state, int aluResult, int instr){
	// Recover info about cache
        int blkSize = sizeof(state->cache[0][0].block); // get size of block
        int totBlocks = sizeof(state->cache); // total amount of blocks in cache
        int wayAmt = sizeof(state->cache[0]); // size of row, amount of ways
        int setAmt = totBlocks/wayAmt; // size of column, amount of sets

        // Manipulate address based on memory address format
        int addr = aluResult;
        int blkOffset =getBlkOffset(addr, blkSize);
        int setIndex = getSetIndex(addr, setAmt, blkSize);
        int tag = getTag(addr, setAmt, blkSize);

        // Check for hit
        for (int i = 0; i < wayAmt; i++){
                if (tag == state->cache[setIndex][i].tag) {
                        // Hit
                        state->hits++;

			if(opcode(instr) == LW){
                		// Load
				print_action(addr, blkSize, cache_to_processor);
				state->reg[field0(instr)] = state->cache[setIndex][i].block[blkOffset];
                	        return;
			} else if(opcode(instr) == SW){
		                // Store, already in cache, it is okay here.
                		//state->mem[aluResult] = regA;
        		}
                }
        }

        // Check for hit
        int matchingWay = checkHit(tag, setIndex, wayAmt, state);
        if (matchingWay != -1){
                updateLRU(matchingWay, setIndex, wayAmt, state); //update the LRU

        	if(opcode(instr) == LW){ // Load Word
                        print_action(addr, blkSize, cache_to_processor); //print out action to output
                        state->reg[field0(instr)] = state->cache[setIndex][matchingWay].block[blkOffset]; //populate reg with particular word
                        return;
                } else if(opcode(instr) == SW){
                        print_action(addr, blkSize, processor_to_cache); //print out action to output
			state->cache[setIndex][matchingWay].block[blkOffset] = state->reg[field0(instr)]; //populate cache with instr from reg
                        state->cache[setIndex][matchingWay].d = 1; // set dirty bit
			return;
                }
		return;
        } else { // miss
                for (int i = 0; i < wayAmt; i++){ // identify LRU way
                        if ((wayAmt-1) == state->cache[setIndex][i].lru){
				// Found way to populate, if dirty write to mem (write-back policy!)
                                if (state->cache[setIndex][i].d = 1){
                                        // Reassmble address to store into memory
                                        int* newAddr = reconstructAddr(tag, setIndex, setAmt, blkSize); // will return start of array in mem (pointer)
                                        state->mem[newAddr] == state->cache[setIndex][i].block; // not sure if this is proper for block size of 4, for example
                                        print_action(*newAddr, blkSize, cache_to_memory); // going from cache to memory, print this action.
                                        state->cache[setIndex][i].d = 0; // reset dirty bit
                        	} else {
                                	// Not dirty, write to nowhere (evict)
                                	print_action(addr, blkSize, cache_to_nowhere);
                        	}

				if(opcode(instr) == LW){ // Load Word (mem to cache, then cache to processor)
                        		// Pull in new data
		                        print_action(*newAddr, blkSize, cache_to_processor);
		                        state->cache[setIndex][i].block = state->mem[addr]*; // not sure if this is proper for block size of 4, for example
                		        state->cache[setIndex][i].v = 1; // valid bit set if it was previously 0.

                        		// Update LRUs of set
                        		updateLRU(i, setIndex, wayAmt, state);

                        		// Finally, grab the instr from cache
                        		print_action(*newAddr, blkSize, cache_to_processor);
                                        state->reg[field0(instr)] = state->cache[setIndex][i].block[blkOffset];
					return;
			        } else if(opcode(instr) == SW){ // Store word (processor to cache, but not cache to mem (handled on eviction)
		                        print_action(addr, blkSize, processor_to_cache); //print out action to output
        		                state->cache[setIndex][matchingWay].block[blkOffset] = state->reg[field0(instr)]; //populate cache with instr from reg
                        		state->cache[setIndex][matchingWay].d = 1; // set dirty bit

                                        // Update LRUs of set
                                        updateLRU(i, setIndex, wayAmt, state);

                        		return;
                		}
				return;
			}
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
		instr = cacheFetch(state);

		/* check for halt */
		if (opcode(instr) == HALT) {
			printf("machine halted\n");
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
			cacheLoadStore(state, aluResult, instr);
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
				printf("FILE: %s\n", fname);
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

        // Try to extract integer values from flags
        if (blockSizeChar != NULL && numSetsChar != NULL && setAssocChar != NULL) {
                // Update state's cache with proper dimensions of array
		int numSets = atoi(numSetsChar); // number of sets in cache
		int setAssoc = atoi(setAssocChar); // 1 = direct mapped, 256 = full assoc (assuming block size 1 word)
		int blkSize = atoi(blockSizeChar); // amount of words in block

		// Ensure that amount of blocks does not exceed 256
		if ((numSets*setAssoc) > 256) {
			printf("Max amount of blocks in cache exceeds spec of 256");
		}

                // Create 2D cache array
		cacheEntry **cache = (cacheEntry**)malloc(numSets*sizeof(cacheEntry*));
		for (int i = 0; i < numSets; ++i) {	// through sets
			cache[i] = (cacheEntry*)malloc(setAssoc*sizeof(cacheEntry)); // numSets of size setAssoc (ways)

			for (int j = 0; j < setAssoc; j++) { // through ways
	                        // Initialize cache entries
				cache[i][j].lru = setAssoc - 1;
				cache[i][j].v = 0;
				cache[i][j].d = 0;
				cache[i][j].tag = -1;
				cache[i][j].block = (int*)malloc(blkSize*sizeof(int));
			}
		}

		// Define cache in state
		state->cache = cache;
        } else {
                printf("Error interpreting flags");
                return 0;
        }

	/** Run the simulation **/
	run(state);

	free(state);
	free(fname);
}
