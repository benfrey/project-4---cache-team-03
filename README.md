# UST-3400 Cache Simulator

Ben Frey and Joe Lambrecht <br />
CISC340 - Professor Myre <br />
Project 4 - Cache Simulator

## Cache Simulator (Based on first generation UST-3400)
The UST-3400 Cache Simulator implements cache as an intermediary between the single-cycle processor and memory. The purpose of the cache implementation is to increase CPU performance by temporarily holding instructions and data that the CPU is likely to use. The cache in this implementation is transparent to the instruction set assembly program (ISA) in the sense that any arbitrary UST-3400 ISA program is compatible with the cache simulator.

## UST-3400 Functional Overview
First the file input lines are swept to store the program into UST-3400 memory. The cache is initialized based upon user input preferences defined as block size (in words), amount of sets, and cache associativity.

| Cache parameter   | Description |
| ----------------- | ----------- |
| Block size        | Size of the block for a single cache entry (in words) |
| Set amount        | Amount of sets in cache (1 = fully associative) |
| Set associativity | Amount of ways in the cache (1 = direct mapped) |
| Total blocks      | Set amt <i>x</i> Set associativity <= 256 |

The first address from memory is fetched into cache and the program is executed in sequential order. The program is run until a halt instruction is stored is reached. At this point, all dirty cache entries are written back to memory and all cache entries are invalidated (valid bit set to 0).

## Running a Program
Once the program .zip has been decompressed, in the cache simulator directory simply run:<br />
<br />
$ make<br />
$ ./sim -f {inputfile.mc} -b {blockSize} -s {setAmount} -a {setAssociativity}<br />
<br />
Note that each of the cache parameter inputs (-b, -s, and -a) must be positive powers of two and the total allocated blocks must not exceed 256 as stated above.

## Test Suite Descriptions
| Assembly File (Machine Code)          | Description |
| ------------- | :---------------------|
| test.4.2.1.asm (test.4.2.1.mc) | Simple example presented in the project documentation with a storeword followed by two load words and a halt. |
| testTwo.2.2.2.asm (testTwo.2.2.2.mc) | Slightly more involved example given in class as practice. Involves an R-Type instruction. The smaller (relative to test.4.2.1.asm) cache dimensions required for this example lead to a significant amount of misses. |
| lwTest.2.1.8.asm (lwTest.2.1.8.mc) | Tests the case in which an entry's LRU reaches the maximum value while unallocated entries remain. A new cache entry should be allocated instead of arbitrarily writing into any way of a certain LRU. |
| class.4.4.4.asm (class.4.4.4.mc) | Classic class.asm example that tests a myriad of instruction sequences. |
| dirty.2.2.2.asm (dirty.2.2.2.mc) | Example of a dirty entry that is written back to memory with program halt. |
| halt.1.1.1.asm (halt.1.1.1.mc) | Example of a halt that is overwritten to prevent the program from terminating prematurely. |

## Known Issues
- There are no current known issues.

