#include <iostream>
#include <stdio.h>
#include <sstream>
#include <string>
#include <bitset>
#include <fstream>
#include <ctype.h>
#include <stdlib.h>
#include <vector>
#include <math.h>
#include <iomanip>

using namespace std;

unsigned int BLOCK_SIZE;

struct instruction{
  char rw;
  unsigned int addr;
};

struct parse_instr{
  char rw;
  unsigned int b_offset;
  unsigned int index;
  unsigned int tag;
};

struct block {
  unsigned int valid; 
  unsigned int dirty; 
  unsigned int index; 
  unsigned int tag; 
  unsigned int LRU; 
};

class cache { 
  public: 
  unsigned int sets; // Number of sets in the cache
  unsigned int assoc; // Associativity of the cache
  unsigned int blocks; // No of blocks in the cache
  unsigned int vc_blocks; // Blocks in Victim cache
  unsigned int reads;  // Read request counter
  unsigned int writes; //Write request counter
  unsigned int read_hits;  // Read hit counter
  unsigned int write_hits; //Write hit counter
  unsigned int read_misses;  // Read miss-es counter
  unsigned int write_misses; // Write misses counter
  unsigned int wbacks; // Number of write-backs to lower memory performed.
  unsigned int swaps; // Number of swaps with victim cache in case of hits
  unsigned int swap_req; // Number of swap requests forwarded to victim cache
  unsigned int mem_traffic; // Total number of memory read/writes performed (If cache is lowest in the hierarchy)
  class cache* lower;  // Pointer to lower level cache
  vector<struct block> cache_memory; // Actual cache that is constructed from struct blocks
  vector<struct block> victim;  // Actual Victim cache again constructed from struct blocks
  cache (unsigned int, unsigned int, unsigned int, unsigned int); // Cache constructor
  int read( struct instruction); // This function performs a read operation of the instruction in the operand
  int write(struct instruction); // This function performs a write operatin of the instruction in the operand
  int printset(unsigned int); // This function prints the specified single set from the cache memory 
  void printcache(); // Recursively calls the printset() function to print the contents of the whole cache
  void printvictim(); // This function prints the entire victim cache
  void insertion_sort(bool); // Sorts blocks in each set according to incr LRU (bool=1 => victim, else cache memory)
  void reconfigure_LRU(unsigned int, bool); // Makes the argument'th posn -MRU (bool=1 => victim, else cache memory)
  void swap(unsigned int, unsigned int); // Swaps arg-1'th posn in cache memory with arg2-'th posn in victim cache
  void victim_swap(unsigned int); // Calls swap function to swap LRU in cache with freespace / LRU in victim cache
  void insert_data(struct parse_instr, unsigned int); // Insert data with dirty bit set according to argument
  struct instruction blocktoinstruction(unsigned int, char); // Convert a block write back operation to a write instruction.
  struct parse_instr parser(struct instruction); // Parse instruction to read/write, block_offset, index and tag bits.
}; 
 
cache::cache (unsigned int s, unsigned int a, unsigned int b, unsigned int v) { 
  struct block block_g;
  sets = s; 
  assoc = a; 
  blocks = b; 
  vc_blocks = v; 
  swaps = 0; 
  reads = 0; 
  writes = 0; 
  wbacks = 0; 
  swap_req = 0; 
  read_hits = 0; 
  write_hits = 0; 
  read_misses = 0; 
  mem_traffic = 0; 
  write_misses = 0; 
  block_g.valid = 0; 
  block_g.dirty = 0; 
  block_g.index = 0; 
  block_g.tag = 0; 
  lower = NULL;
  cache_memory.reserve(blocks); 
  for(unsigned int i=0; i<blocks; i++) {
    cache_memory.push_back(block_g);
  } 
  victim.reserve(vc_blocks); 
  for(unsigned int i=0; i<vc_blocks; i++) {
    victim.push_back(block_g);
  } 
} 
 
int cache::read (struct instruction instr) { 
  reads+=1;
  struct parse_instr pinstr = parser(instr); // Parse incoming instruction
  unsigned int setno = pinstr.index;
  for(unsigned int i=setno*assoc; i<(setno+1)*assoc; i++){ // Check for Hits
    if(cache_memory[i].valid && (pinstr.tag == cache_memory[i].tag)) {
      read_hits+=1;
      reconfigure_LRU(i,0); // Reconfigure Hit Position to MRU in cache memory
      return 0; // We are done!!
    }
  }
  //There was no hit, need to bring in data
  read_misses+=1;
  //Check for empty space.
  unsigned int freespace_flag=0;
  for(unsigned int i=setno*assoc; i<(setno+1)*assoc; i++){
    if(!cache_memory[i].valid) {
      freespace_flag=1;
      break;
    }
  }
  //Perform LRU eviction in case set is full
  if(!freespace_flag) {
    unsigned int LRU_posn = 0;
    unsigned int notfull_flag = 1;
    for(unsigned int i=setno*assoc; i<(setno+1)*assoc; i++){
      if(cache_memory[i].LRU==assoc-1) { //This assumes that set is full
        LRU_posn = i;
        notfull_flag = 0;
        break;
      }
    }
    if(notfull_flag) {
      cout<<"Set not-full during eviction exception! Aborting...\n";
      return (-1);  
    }
    if(vc_blocks>0) {  
    unsigned int victim_LRU = vc_blocks;
      //Victim Cache is present
      swap_req++;
      for(unsigned int i=0; i<vc_blocks; i++) {
        if(victim[i].valid && (pinstr.tag == victim[i].tag) && pinstr.index == victim[i].index ) {// Victim HIT
          victim_LRU = i;
          swap(LRU_posn, victim_LRU); // Swap L1-LRU with Hit position
          swaps++;
          reconfigure_LRU(LRU_posn,0);  // LRU of read data needs to be updated to MRU
          reconfigure_LRU(victim_LRU,1); // Reconfigure incoming data to MRU in victim
          return 0;
        }
      }
      // Victim Miss. Swap free space/LRU in victim with LRU in cache and proceed for eviction.
      victim_swap(LRU_posn);
    }
    // LRU_posn now contains posn to read new data to, after writing back original data if its dirty.
    if(cache_memory[LRU_posn].dirty==1) {
      wbacks+=1; 
      cache_memory[LRU_posn].dirty=0;
      if(lower==NULL) {
        mem_traffic+=1;
      }
      else {
        unsigned int return_val = lower->write(blocktoinstruction(LRU_posn,'w')); // Indicates write to lower memory
        if(return_val!=0) {
          cout<<"Lower writeback exception! Aborting...\n";
          return (-1);
        }
      }
    }
    cache_memory[LRU_posn].valid=0;
  }
  //Perform read from lower memory
  if(lower==NULL) {
    mem_traffic+=1; // Indicates read from memory
  }
  else {
    unsigned int return_val = lower->read(instr); // Indicates read from lower memory
    if(return_val!=0) {
      cout<<"Lower read exception! Aborting...\n";
      return (-1);
    }
  }
  //Add data to cache (Write Allocate)
  insert_data(pinstr,0);
  return 0; 
} 

int cache::write (struct instruction instr) { 
  struct parse_instr pinstr = parser(instr);
  writes++;
  unsigned int setno = pinstr.index;
  for(unsigned int i=setno*assoc; i<(setno+1)*assoc; i++){
    if(cache_memory[i].valid&&pinstr.tag == cache_memory[i].tag) {
      write_hits+=1;
      //Need to rearrange LRU
      reconfigure_LRU(i,0); 
      cache_memory[i].dirty=1; 
      return 0;
    }
  }
  //There was no hit, need to bring in data
  write_misses+=1;
  //Check for empty space.
  unsigned int freespace_flag=0;
  for(unsigned int i=setno*assoc; i<(setno+1)*assoc; i++){
    if(!cache_memory[i].valid) {
      freespace_flag=1;
      break;
    }
  }
  //Perform LRU eviction in case set is full
  if(!freespace_flag) {
    unsigned int LRU_posn = 0;
    unsigned int notfull_flag = 1;
    for(unsigned int i=setno*assoc; i<(setno+1)*assoc; i++){
      if(cache_memory[i].LRU==assoc-1) { //This assumes that set is full
        LRU_posn = i;
        notfull_flag = 0;
        break;
      }
    }
    if(notfull_flag) {
      cout<<"Set not-full during eviction exception! Aborting...\n";
      return (-1);  
    }
    if(vc_blocks>0) {
    unsigned int victim_LRU = vc_blocks;
      //Victim Cache is present
      swap_req++;
      for(unsigned int i=0; i<vc_blocks; i++) {
        if(victim[i].valid && (pinstr.tag == victim[i].tag) && (pinstr.index == victim[i].index) ) {// Victim HIT
          victim_LRU = i;
          swap(LRU_posn, victim_LRU); // Swap L1-LRU with Hit position
          swaps++;
          reconfigure_LRU(LRU_posn,0);  // LRU of read data needs to be updated to MRU
          reconfigure_LRU(victim_LRU,1); // reconfigure incoming data to MRU in victim
          cache_memory[LRU_posn].dirty=1; 
          return 0;
        }
      }
      victim_swap(LRU_posn);
    }

    if(cache_memory[LRU_posn].dirty==1) {
      wbacks++;
      cache_memory[LRU_posn].dirty=0;
      if(lower==NULL) {
        mem_traffic+=1;
      }
      else {
        unsigned int return_val = lower->write(blocktoinstruction(LRU_posn,'w')); // Indicates write to lower memory NEED TO CHANGE THIS WRITE BACK TO EVICTED BLOCK
        if(return_val!=0) {
          cout<<"Lower writeback exception! Aborting...\n";
          return (-1);
        }
      }
    }
    cache_memory[LRU_posn].valid=0;
  }
  //Perform read from lower memory
  if(lower==NULL) {
    mem_traffic+=1; // Indicates read from memory
  }
  else {
    unsigned int return_val = lower->read(instr); // Indicates wirte to lower memory
    if(return_val!=0) {
      cout<<"Lower read exception! Aborting...\n";
      return (-1);
    }
  }
  //Add data to cache (Write Allocate)
  insert_data(pinstr,1);
  return 0; 
} 

int cache::printset (unsigned int setno) { 
  if(setno < sets) {
    cout<<"  set" << setw(4) << right << dec << setno << ": ";
    for(unsigned int i=setno*assoc; i<(setno+1)*assoc; i++){
      struct block block_g;
      block_g = cache_memory[i];
      cout << setw(8) << right << hex << block_g.tag<< " ";
      if(block_g.dirty) {
        cout<< "D";
      }
      else {
        cout<< " ";
      }
    }
    cout<<"\n";
    return 1; 
  }
  else {
    cout<<"Set number is invalid for this cache.\n";
    return 0; 
  }
} 

void cache::printcache () { 
  unsigned int return_val=1;
  for(unsigned int i=0; i<sets; i++) {
    return_val = printset(i);
    if(!return_val) {
      break;
    }
  }
} 

void cache::printvictim () { 
  if(vc_blocks) {
    cout<<"  set   0: ";
    for(unsigned int i=0; i<vc_blocks; i++){
    struct block block_g;
      block_g = victim[i];
      unsigned int victim_tag = block_g.tag;
      unsigned int logindex = (unsigned int)log2(sets);
      victim_tag *= (1<<logindex);
      victim_tag += block_g.index;
      cout << setw(8) << right << hex << victim_tag << " ";
      if(block_g.dirty) {
        cout<< "D";
      }
      else {
        cout<< " ";
      }
    }
    cout<<"\n";
  }
  else {
    cout<<"Victim Cache size is zero!!\n";
  }
} 

struct parse_instr cache::parser(struct instruction instr) {
  struct parse_instr pinstr;
  pinstr.rw = instr.rw;
  unsigned int logbsize = (unsigned int)log2(BLOCK_SIZE);
  unsigned int logindex = (unsigned int)log2(sets);

  pinstr.b_offset = instr.addr%(1<<logbsize); 
  instr.addr      = instr.addr/(1<<logbsize); 
  pinstr.index    = instr.addr%(1<<logindex); 
  instr.addr      = instr.addr/(1<<logindex); 
  pinstr.tag      = instr.addr; 
  return pinstr;
}

void cache::reconfigure_LRU(unsigned int LRU_posn, bool vicmem) {
  if(!vicmem) {
    unsigned int setno = LRU_posn/assoc;
    for(unsigned int i=setno*assoc; i<(setno+1)*assoc; i++) {
      if(cache_memory[i].valid) {
        if(cache_memory[i].LRU < cache_memory[LRU_posn].LRU) {
          cache_memory[i].LRU+=1;
        }
      }
    } 
    cache_memory[LRU_posn].LRU = 0;
  }
  else {
    for(unsigned int i=0; i<vc_blocks; i++) {
      if(victim[i].LRU < victim[LRU_posn].LRU) {
        victim[i].LRU+=1;
      }
    }
    victim[LRU_posn].LRU = 0;
  }
}

void cache::swap(unsigned int LRU_posn, unsigned int victim_LRU) {
  struct block temp_block;
  temp_block = victim[victim_LRU];
  victim[victim_LRU] = cache_memory[LRU_posn];
  victim[victim_LRU].LRU = temp_block.LRU; // Keep LRU value in victim unchanged
  temp_block.LRU = cache_memory[LRU_posn].LRU; // Keep LRU value in cache unchanged.
  cache_memory[LRU_posn] = temp_block;
}

void cache::victim_swap(unsigned int LRU_posn) {
  for(unsigned int i=0; i<vc_blocks; i++) {
    // Victim Miss - Check for empty space
    if(!victim[i].valid || (victim[i].LRU==vc_blocks-1)) { // Found empty space
      victim_LRU = i;
      swap(LRU_posn, victim_LRU); // Bring L1-LRU to free space in victim
      reconfigure_LRU(victim_LRU,1); // reconfigure incoming data to MRU in victim
      break; //Victim is updated. Proceed for lower->read and insertion. 
    }
  }
}

void cache::insert_data(struct parse_instr pinstr, unsigned int dirty) {
  unsigned int insert_flag=0;
  unsigned int setno = pinstr.index;
  unsigned int valid_blocks = 0;
  unsigned int LRU_posn = 0;
  for(unsigned int i=setno*assoc; i<(setno+1)*assoc; i++){
    if(cache_memory[i].valid) {
      valid_blocks+=1;
    }
  }
  for(unsigned int i=setno*assoc; i<(setno+1)*assoc; i++){
    if(!cache_memory[i].valid) {
      insert_flag=1;
      cache_memory[i].valid=1;
      cache_memory[i].dirty=dirty;
      cache_memory[i].LRU=valid_blocks;
      cache_memory[i].index = pinstr.index;
      cache_memory[i].tag = pinstr.tag;
      LRU_posn = i;
      break;
    }
  }
  if(!insert_flag) {
    cout<<"Error in Insert Data\n";
  }
  reconfigure_LRU(LRU_posn,0);
}

struct instruction cache::blocktoinstruction(unsigned int LRU_posn, char rw) {
  unsigned int addr = 0;
  unsigned int logbsize = (unsigned int)log2(BLOCK_SIZE);
  unsigned int logindex = (unsigned int)log2(sets);
  addr += cache_memory[LRU_posn].tag;
  addr = addr<<logindex;
  addr += cache_memory[LRU_posn].index;
  addr = addr<<logbsize;
  struct instruction instr;
  instr.addr = addr;
  instr.rw = rw;
  return instr;
}

void cache::insertion_sort (bool vicmem) {
  if (!vicmem) {  
    unsigned int valid_blocks;
    for(unsigned int setno=0; setno < sets; setno++) {
      valid_blocks =0;
      for(unsigned int i=setno*assoc; i<(setno+1)*assoc; i++){
        if(cache_memory[i].valid) {
          valid_blocks+=1;
        }
        else {
          break;
        }
      }
      for (unsigned int i = setno*assoc; i < setno*assoc+valid_blocks; i++){ //Assumes valid blocks are all grouped to left
        unsigned int j = i;
        while (j > setno*assoc && cache_memory[j].LRU < cache_memory[j-1].LRU) {
          struct block temp_block = cache_memory[j];
          cache_memory[j] = cache_memory[j-1];
          cache_memory[j-1] = temp_block;
          j--;
        }
      }
    }
  }
  else {
    unsigned int valid_blocks = 0;
    for(unsigned int i=0; i<vc_blocks; i++){
      if(victim[i].valid) {
        valid_blocks+=1;
      }
      else {
        break;
      }
    }
    for (unsigned int i=0; i < valid_blocks; i++){ //Assumes valid blocks are all grouped to left
      unsigned int j = i;
      while (j > 0 && victim[j].LRU < victim[j-1].LRU) {
        struct block temp_block = victim[j];
        victim[j] = victim[j-1];
        victim[j-1] = temp_block;
        j--;
      }
    }
  }
}


int main(int argc, char* argv[]) {

if (argc!=8) {
	cout << "Error: Please enter exactly 7 arguments\n";
	return -1;	
}

             BLOCK_SIZE   = atoi(argv[1]);
unsigned int L1_SIZE      = atoi(argv[2]);
unsigned int L1_ASSOC     = atoi(argv[3]);
unsigned int VC_BLOCKS    = atoi(argv[4]);
unsigned int L2_SIZE      = atoi(argv[5]);
unsigned int L2_ASSOC     = atoi(argv[6]);
unsigned int L1_SETS=0, L2_SETS=0, L1_BLOCKS=0, L2_BLOCKS=0;
unsigned int mem_traffic;
struct instruction instr;
float        swap_rate =0.0,  L1_miss_rate = 0.0, L2_miss_rate = 0.0;
string       trace_file   = string(argv[7]);
string       trace_line;

// Instantiate necessary caches
if (L1_SIZE==0) {
  cout << "Error: Cannot have a L1 Cache size of 0\n";	
  return -1;
}
  L1_SETS = L1_SIZE/BLOCK_SIZE/L1_ASSOC;		//atoi(argv[3]);
  L1_BLOCKS = L1_SIZE/BLOCK_SIZE;		//atoi(argv[3]);
if (L2_SIZE>0) {
  L2_SETS = L2_SIZE/BLOCK_SIZE/L2_ASSOC;		//atoi(argv[3]);
  L2_BLOCKS = L2_SIZE/BLOCK_SIZE;		//atoi(argv[3]);
}	
else {
  L2_SETS = 0;
  L2_BLOCKS = 0;
  L2_ASSOC = 0;
}	
  cache L1_CACHE(L1_SETS, L1_ASSOC, L1_BLOCKS, VC_BLOCKS);
  cache L2_CACHE(L2_SETS, L2_ASSOC, L2_BLOCKS, 0);
if (L2_SIZE>0) {
  L1_CACHE.lower = &L2_CACHE;
}	


// Check if BLOCK_SIZE, Li_SETS is a power of 2
if(BLOCK_SIZE==1) {
  cout<<"Error: Block_size of cache cannot be one\n";
  return (-1);
}
if(L1_SIZE!=L1_SETS*L1_ASSOC*BLOCK_SIZE) {
  cout<<"Error: L1_SIZE/L1_ASSOC is not a power of 2\n";
  return (-1);
}
if(L2_SIZE) {
  if(L2_SIZE!=L2_SETS*L2_ASSOC*BLOCK_SIZE) {
    cout<<"Error: L2_SIZE/L2_ASSOC is not a power of 2\n";
    return (-1);
  }
}
vector<unsigned int> array;
array.push_back(BLOCK_SIZE);
array.push_back(L1_SETS);
if (L2_SIZE) {
  array.push_back(L2_SETS);
}
for (unsigned int i=0; i<array.size(); i++) {
  unsigned int remainder=0;
  while(array[i]>1) {
    remainder += array[i]%2;
    array[i]/= 2;
  }
  if(remainder) {
    cout<<"Error: Block_Size/Sets in a Cache need to be a power of 2\n";
    return (-1);
  }
}

//Open traces file for reading
ifstream ftrace;

ftrace.open(trace_file.c_str(),ios::in);
if(!ftrace.is_open()) {
  cout << "Error: Could not open trace_file\n";
  return -1;
}


//File opened for parsing
while (ftrace >> instr.rw >> trace_line) {
  instr.rw = tolower(instr.rw);
  if (instr.rw != 'r' && instr.rw !='R' && instr.rw != 'w' && instr.rw != 'W') {
    cout << "Error: Trace line r/w bit is invalid\n";
    break;
  }
  
  //Convert trace_line string from hex to binary and store in instr.addr.
  stringstream ss;
  ss << hex << trace_line;
  ss >> instr.addr;
  bitset<32> b(instr.addr);

  //Instruction is Parsed. Invoke L1 cache:
  if(instr.rw=='r') {
    L1_CACHE.read(instr);
}
  else {
    L1_CACHE.write(instr);
}
}
ftrace.close();

L1_CACHE.insertion_sort(0);
L2_CACHE.insertion_sort(0);
L1_miss_rate = ( float(L1_CACHE.read_misses) + float(L1_CACHE.write_misses) - float(L1_CACHE.swaps) ) / ( float(L1_CACHE.reads) + float(L1_CACHE.writes) );

cout << "===== Simulator configuration =====\n";
cout << left<< setw(18)<< "  BLOCKSIZE:    " <<right <<setw(14) <<BLOCK_SIZE <<"\n"; 
cout << left<< setw(18)<< "  L1_SIZE:      " <<right <<setw(14) <<L1_SIZE    <<"\n"; 
cout << left<< setw(18)<< "  L1_ASSOC:     " <<right <<setw(14) <<L1_ASSOC   <<"\n"; 
cout << left<< setw(18)<< "  VC_NUM_BLOCKS:" <<right <<setw(14) <<VC_BLOCKS  <<"\n"; 
cout << left<< setw(18)<< "  L2_SIZE:      " <<right <<setw(14) <<L2_SIZE    <<"\n"; 
cout << left<< setw(18)<< "  L2_ASSOC:     " <<right <<setw(14) <<L2_ASSOC   <<"\n"; 
cout << left<< setw(18)<< "  trace_file:   " <<right <<setw(14) <<trace_file <<"\n"; 
cout << "\n";
cout << "===== L1 contents =====\n";
L1_CACHE.printcache();
cout << "\n";
if (VC_BLOCKS>0) {
  L1_CACHE.insertion_sort(1); // Sort Victim Cache in L1-Cache
  swap_rate = float(L1_CACHE.swap_req)/float(L1_CACHE.reads+L1_CACHE.writes);
  cout << "===== VC contents =====\n";
  L1_CACHE.printvictim();
  cout << "\n";
}	
if (L2_SIZE>0) {
  mem_traffic = L2_CACHE.mem_traffic;
  L2_miss_rate = ( float(L2_CACHE.read_misses) ) / ( float(L2_CACHE.reads) );
  cout << "===== L2 contents =====\n";
  L2_CACHE.printcache();
  cout << "\n";
}	
else {
  mem_traffic = L1_CACHE.mem_traffic;
}
cout << fixed << showpoint;
cout << dec << "===== Simulation results =====\n";
cout << setw(35) << left << "  a. number of L1 reads:" << right << setw(17) << L1_CACHE.reads << "\n"; 
cout << setw(35) << left << "  b. number of L1 read misses:" << right << setw(17) << L1_CACHE.read_misses << "\n"; 
cout << setw(35) << left << "  c. number of L1 writes:" << right << setw(17) << L1_CACHE.writes << "\n"; 
cout << setw(35) << left << "  d. number of L1 write misses:" << right << setw(17) << L1_CACHE.write_misses << "\n"; 
cout << setw(35) << left << "  e. number of swap requests:" << right << setw(17) << L1_CACHE.swap_req << "\n"; 
cout << setw(35) << left << "  f. swap request rate:" << right << setw(17) << fixed << setprecision(4) << swap_rate << "\n"; 
cout << setw(35) << left << "  g. number of swaps:" << right << setw(17) << L1_CACHE.swaps << "\n"; 
cout << setw(35) << left << "  h. combined L1+VC miss rate:" << right << setw(17) << fixed << setprecision(4) << L1_miss_rate << "\n"; 
cout << setw(35) << left << "  i. number writebacks from L1/VC:" << right << setw(17) << L1_CACHE.wbacks << "\n"; 
cout << setw(35) << left << "  j. number of L2 reads:" << right << setw(17) << L2_CACHE.reads << "\n"; 
cout << setw(35) << left << "  k. number of L2 read misses:" << right << setw(17) << L2_CACHE.read_misses << "\n"; 
cout << setw(35) << left << "  l. number of L2 writes:" << right << setw(17) << L2_CACHE.writes << "\n"; 
cout << setw(35) << left << "  m. number of L2 write misses:" << right << setw(17) << L2_CACHE.write_misses << "\n"; 
cout << setw(35) << left << "  n. L2 miss rate:" << right << setw(17) << fixed << setprecision(4) << L2_miss_rate << "\n"; 
cout << setw(35) << left << "  o. number of writebacks from L2:" << right << setw(17) << L2_CACHE.wbacks << "\n"; 
cout << setw(35) << left << "  p. total memory traffic:" << right << setw(17) << mem_traffic << "\n"; 

return 0;
}