#include <iostream>
#include <string>
#include <ctype.h>
#include <stdio.h>
#include <sstream>
#include <fstream>
#include <vector>
#include <math.h>
#include <iomanip>
#include <algorithm>
#include <stdlib.h>
#include <map>
#include <list>
#include <bitset>

using namespace std;

unsigned int ROB_Size = 1, IQ_Size = 1, Width = 1, numinstr = 0, simcycle = 1; // TAKE CARE

struct instruction {
    unsigned int PC;
    unsigned int optype;
    int originalDest;
    int dest;
    int src1;
    int src2;
    bool valid;
    unsigned int seq;
    map<string, unsigned int> beginTime;
    map<string, unsigned int> cyclesInStage;
};

struct rmtEntry {
    bool valid;
    unsigned int tag;
};

class reorderBuffer {
public:
    reorderBuffer(unsigned int, instruction I);
    vector<pair<bool, instruction> > buffer;
    unsigned int freeSlots();
    unsigned int head, tail;
    bool entryValid;
};

reorderBuffer::reorderBuffer (unsigned int Size, instruction I) {
    head = 0; tail = 0;
    pair<bool, instruction> pair;
    for (unsigned int a = 0; a < Size; a++) {
        buffer.push_back(pair);
    }
}

unsigned int reorderBuffer::freeSlots() {
    if (tail > head) {
        return buffer.size() - (tail - head);
    } else if (tail == head) {
        if (buffer[head].first) {
            return 0;
        } else {
            return ROB_Size;
        }
    } else {
        return (head - tail);
    }
}

struct IQEntry {
    bool rs1rdy;
    bool rs2rdy;
    struct instruction i;
};

vector<struct instruction> DE, RN, RR, DI, WB;
vector<pair<struct instruction, unsigned int> > execute_list;
struct rmtEntry RMT[67];
list<struct IQEntry> IQ;
float IPC = 0.0;

void printreg (vector<instruction> *reg) {
    for (vector<instruction>::iterator a = reg->begin() ; a != reg->end(); a++) {
        stringstream stream;
        stream << hex << a->PC;
        string PC( stream.str() );
        printf("PC: %s\tType:%d\tDest: %d\tSrc1: %d\tSrc2: %d\n",PC.c_str(),a->optype,a->dest,a->src1,a->src2);
    }
}

void retirePrint (struct instruction i) {
    printf("%d fu{%d} src{%d,%d} dst{%d} FE{%d,%d} DE{%d,%d} RN{%d,%d} RR{%d,%d} DI{%d,%d} IS{%d,%d} EX{%d,%d} WB{%d,%d} RT{%d,%d}\n",i.seq-1,i.optype,i.src1,i.src2,i.dest,i.beginTime["FE"],i.cyclesInStage["FE"],i.beginTime["DE"],i.cyclesInStage["DE"],i.beginTime["RN"],i.cyclesInStage["RN"],i.beginTime["RR"],i.cyclesInStage["RR"],i.beginTime["DI"],i.cyclesInStage["DI"],i.beginTime["IQ"],i.cyclesInStage["IQ"],i.beginTime["EX"],i.cyclesInStage["EX"],i.beginTime["WB"],i.cyclesInStage["WB"],i.beginTime["RT"],i.cyclesInStage["RT"]);
}

void RETIRE (reorderBuffer *ROB) {
    unsigned int count = 0;
    unsigned int *headIter = &(ROB->head);
    instruction *bufferHead = &(ROB->buffer[*headIter].second);
    while (count < Width && ROB->buffer[*headIter].first && bufferHead->valid) {
        bufferHead->cyclesInStage["RT"] = simcycle - bufferHead->beginTime["RT"];
        if (bufferHead->dest != -1 && RMT[bufferHead->dest].tag==100+(*headIter)) {
            RMT[bufferHead->dest].valid = 0;
        }
        retirePrint(*bufferHead);
        bufferHead->valid = 0; ROB->buffer[*headIter].first = false;
        count++;
        if(*headIter == ROB_Size-1) {
            *headIter = 0;
        } else {
            (*headIter)++;
        }
        bufferHead = &(ROB->buffer[*headIter].second);
    }
}

void WRITEBACK (reorderBuffer *ROB) {
    for (unsigned int a = 0; a < 5*Width; a++) {
        if (WB[a].valid) {
            unsigned int ROB_posn = ROB_Size;
            for (unsigned int iter = 0; iter < ROB_Size; iter++) {
                if (ROB->buffer[iter].second.seq == WB[a].seq) {
                    ROB_posn = iter; break;
                }
            }
            if (ROB_posn==ROB_Size) {
                cout << "Error: Instruction not found in ROB! Aborting...\n" << WB[a].seq;
            }
            WB[a].cyclesInStage["WB"] = simcycle - WB[a].beginTime["WB"];
            WB[a].beginTime["RT"] = simcycle;
            ROB->buffer[ROB_posn].second = WB[a]; // Copy write back instruction to ROB.
            WB[a].valid = false;
        }
    }
}

void updateReadyInfo (struct IQEntry *entry) {
    if (entry->i.src1 >= -1 && entry->i.src1 < 67) {
        entry->rs1rdy = true;
    } else {
        entry->rs1rdy = false;
    }
    if (entry->i.src2 >= -1 && entry->i.src2 < 67) {
        entry->rs2rdy = true;
    } else {
        entry->rs2rdy = false;
    }
}

void wakeUp (instruction *I, reorderBuffer *ROB) {
    if (I->dest==-1) {
        return;
    }
    int ROB_posn = ROB_Size;
    for (unsigned int a = 0; a < ROB_Size; a++) {
        if (ROB->buffer[a].second.seq==I->seq) {
            ROB_posn = a+100;
        }
    }
    if (I->dest >= 100) {
        I->dest = I->originalDest;
    }
    for (unsigned int a = 0; a < Width; a++) {
        if (ROB_posn == RR[a].src1) {RR[a].src1 = I->dest;}
        if (ROB_posn == RR[a].src2) {RR[a].src2 = I->dest;}
        if (ROB_posn == RR[a].dest) {RR[a].dest = I->dest;}
        if (ROB_posn == DI[a].src1) {DI[a].src1 = I->dest;}
        if (ROB_posn == DI[a].src2) {DI[a].src2 = I->dest;}
        if (ROB_posn == DI[a].dest) {DI[a].dest = I->dest;}
    }
    for (list<struct IQEntry>::iterator a = IQ.begin(); a != IQ.end(); a++) {
        if (ROB_posn == a->i.src1)  {a->i.src1  = I->dest; updateReadyInfo(&*a);}
        if (ROB_posn == a->i.src2)  {a->i.src2  = I->dest; updateReadyInfo(&*a);}
        if (ROB_posn == a->i.dest)  {a->i.dest  = I->dest; updateReadyInfo(&*a);}
    }
}

void EXECUTE (reorderBuffer *ROB) {
    for (unsigned int a = 0; a < 5*Width; a++) {
        if (execute_list[a].first.valid && execute_list[a].second<6) { // This checks if the entry is valid
            execute_list[a].second--;
            if (execute_list[a].second==0) {
                WB[a] = execute_list[a].first;
                execute_list[a].second = 10; // Make entry invalid
                WB[a].cyclesInStage["EX"] = simcycle - WB[a].beginTime["EX"];
                WB[a].beginTime["WB"] = simcycle; // Direct mapping to WB, as WB processes all instructions in one cycle.
                execute_list[a].first.valid = 0;
                if (WB[a].dest != -1) {
                    wakeUp(&WB[a], ROB);
                }
            }
        }
    }
}

bool renameReg (int *reg, reorderBuffer *ROB) {
    if (*reg == -1) {
        return false;
    }
    /* The following piece of code checks if the register to be renamed is already available in WB
     but since the execute stage of the dependency is already over before executing rename of the 
     dependant instruction, the wakeUp call was missed. No rename required in this case.*/
    for (unsigned int a = 0; a < 5*Width; a++) {
        unsigned int ROB_posn = ROB_Size;
        for (unsigned int iter = 0; iter < ROB_Size; iter++) {
            if (ROB->buffer[iter].second.seq == WB[a].seq) {
                ROB_posn = iter+100; break;
            }
        }
        if (WB[a].valid && WB[a].dest==*reg && (RMT[*reg].tag==ROB_posn)) {
            return false;
        }
    }
    /* The following piece of code checks if the register to be renamed is already available in ROB
       but just not checked in into the ARF/RMT as the retire stage is not over.*/
    for (unsigned int a = 0; a < ROB_Size; a++) {
        instruction robEntry = ROB->buffer[a].second;
        if (robEntry.valid == 1 && robEntry.dest == *reg && (RMT[*reg].tag==a+100)) {
            return false;
        }
    }
    if(*reg>=0 && *reg<67) {
        struct rmtEntry rmt = RMT[*reg];
        if (rmt.valid) { // Rename register after checking that tag is valid.
            if (rmt.tag < 100) {
                return true;
            } else {
                *reg = rmt.tag;
                return false;
            }
        } else { // No need to rename
            return false;
        }
    }
    return true;
}

bool resolveReg (int *reg, reorderBuffer *ROB) {
    if (*reg >= -1 && *reg < 67) {
        return false;
    } else if (*reg>=100 && *reg < 100+ROB_Size) {
        if (ROB->buffer[*reg-100].second.dest<67 && !RMT[ROB->buffer[*reg-100].second.dest].valid) {
            *reg = ROB->buffer[*reg-100].second.dest;
        }
        return false;
    }
    return true;
}

void rename (instruction* i, bool resolve, reorderBuffer *ROB) {
    if (resolve) {
        if (resolveReg(&(i->src1), ROB)
            || resolveReg(&(i->src2), ROB)
            || resolveReg(&(i->dest), ROB)
            ) {
            cout << "Error: Invalid values reached for Register Names! Aborting...\n";
        }
    } else {
        if (renameReg(&(i->src1), ROB)
            || renameReg(&(i->src2), ROB)
            || renameReg(&(i->dest), ROB)
            ) {
            cout << "Error: Invalid Register Names detected! Aborting...\n";
        }
    }
}

void processIssue (reorderBuffer *ROB, list<struct IQEntry>::iterator iter) {
    if (iter->i.seq>=90) {
        for (unsigned int a = 0; a < Width; a++) {
        }
    }
    iter->i.cyclesInStage["IQ"] = simcycle - iter->i.beginTime["IQ"];
    iter->i.beginTime["EX"] = simcycle;
    pair<instruction, unsigned int> exEntry;
    exEntry.first= iter->i;
    exEntry.second = iter->i.optype == 0 ? 1 : iter->i.optype == 1 ? 2 : 5;
    for (unsigned int a =0; a < 5*Width; a++) {
        if (!execute_list[a].first.valid) {
            execute_list[a] = exEntry;
            break;
        }
    }
}

void ISSUE (reorderBuffer *ROB) {
    unsigned int count =0;
    for (list<struct IQEntry>::iterator a = IQ.begin(); a != IQ.end() && count < Width;) {
        rename(&(a->i), 1, ROB);
        updateReadyInfo(&*a);
        if (a->rs1rdy && a->rs2rdy) {
            processIssue(ROB, a);
            count++;
            a = IQ.erase(a);
            if (a != IQ.end()) {
            }
        } else {
            a++;
        }
    }
}

unsigned int validInstructions (vector<instruction> *reg) {
    unsigned int valids = 0;
    for (vector<instruction>::iterator a = reg->begin(); a != reg->end(); a++) {
        if (a->valid) {
            valids++;
        } else {
            break;
        }
    }
    return valids;
}

void DISPATCH (reorderBuffer *ROB) { // Check This
    unsigned int valids = validInstructions(&DI);
    if (valids && (IQ_Size - IQ.size()) >= valids) {
        for (unsigned int a = 0; a < valids; a++) {
            /* Check if any operands are ready before assigning to dispatch*/
            rename(&DI[a], 1, ROB);
            DI[a].cyclesInStage["DI"] = simcycle - DI[a].beginTime["DI"];
            DI[a].beginTime["IQ"] = simcycle;
            struct IQEntry *entry = new struct IQEntry;
            entry->i = DI[a];
            updateReadyInfo(entry);
            IQ.push_back(*entry);
            DI[a].valid = 0;
        }
    }
    return;
}

void REG_READ (reorderBuffer *ROB) {
    if (RR.begin()->valid && !DI.begin()->valid) {
        unsigned int valids = validInstructions(&RR);
        for (unsigned int a = 0; a < valids; a++) {
            rename(&RR[a], 1, ROB);
            RR[a].cyclesInStage["RR"] = simcycle - RR[a].beginTime["RR"];
            DI[a] = RR[a];
            DI[a].beginTime["DI"] = simcycle;
            RR[a].valid = 0;
        }
    }
}

void robInsert (struct instruction i, reorderBuffer *ROB) {
    ROB->buffer[ROB->tail].second = i; // Add renamed instruction to ROB
    ROB->buffer[ROB->tail].second.valid = 0; // Valid field in ROB acts as ready flag.
    ROB->buffer[ROB->tail].first = true;
    ROB->tail++;
    if (ROB->tail==ROB_Size) {
        ROB->tail = 0;
    }
}

void process_rename (class reorderBuffer *ROB, unsigned int valids) {
    //allocate space in ROB, rename sources, rename destination in program order
    for (unsigned int a = 0; a < valids; a++) {
        RN[a].cyclesInStage["RN"] = simcycle - RN[a].beginTime["RN"];
        int tempDest = RN[a].dest;
        rename(&RN[a], 0, ROB);
        if ( RN[a].dest != -1) {
            RMT[tempDest].valid = 1; // Make instruction destination entry in RMT point to -
            RMT[tempDest].tag = ROB->tail+100; // ROB position which will hold the instruction
        }
        robInsert(RN[a], ROB);
        RR[a] = RN[a];
        RN[a].valid = 0;
        RR[a].beginTime["RR"] = simcycle;
    }
}

void RENAME (class reorderBuffer *ROB) {
    unsigned int valids = validInstructions(&RN);
    if (valids && !RR.begin()->valid) {
        if (ROB->freeSlots() >= valids) {
            process_rename(ROB, valids);
        }
    }
}

void DECODE () {
    if (DE.begin()->valid && !RN.begin()->valid) {
        for (unsigned int a = 0; a < Width; a++) {
            DE[a].cyclesInStage["DE"] = simcycle - DE[a].beginTime["DE"];
            RN[a] = DE[a];
            RN[a].beginTime["RN"] = simcycle;
            DE[a].valid = 0;
        }
    }
}

void FETCH (ifstream *ftrace) {
    if (DE.begin()->valid) {
        return;
    }
    for (unsigned int a = 0; a < Width; a++) {
        string trace_line;
        struct instruction I;
        if (*ftrace >> trace_line >> I.optype >> I.dest >> I.src1 >> I.src2) {
            numinstr++;
            //Convert trace_line string from hex to binary and store in I.PC.
            stringstream ss;
            ss << hex << trace_line;
            ss >> I.PC;
            bitset<32> b(I.PC);
            I.valid = 1; I.seq = numinstr; I.originalDest = I.dest;
            I.beginTime["FE"]=simcycle-1;I.beginTime["DE"]=simcycle;I.beginTime["RN"]=0;
            I.beginTime["RR"]  =  0 ; I.beginTime["DI"]  =  0 ; I.beginTime["IQ"]  =  0;
            I.beginTime["EX"]  =  0 ; I.beginTime["WB"]  =  0 ; I.beginTime["RT"]  =  0;
            I.cyclesInStage["FE"]=1 ; I.cyclesInStage["DE"]=0 ; I.cyclesInStage["RN"]=0;
            I.cyclesInStage["RR"]=0 ; I.cyclesInStage["DI"]=0 ; I.cyclesInStage["IQ"]=0;
            I.cyclesInStage["EX"]=0 ; I.cyclesInStage["WB"]=0 ; I.cyclesInStage["RT"]=0;
            DE[a] = I;
        } else {
            break;
        }
    }
}

bool advanceCycle (reorderBuffer *ROB, ifstream *ftrace) {
    simcycle++;
    bool robEmpty = (ROB->freeSlots()==ROB_Size);
    bool pipeEmpty = true;
    for (unsigned int iter = 0; (iter < Width) && (pipeEmpty==true); iter++) {
        if (DE[iter].valid || RN[iter].valid || RR[iter].valid) {
            pipeEmpty = false;
        }
    }
    bool traceFileEmpty = ftrace->eof() ? true : false;
    return !(traceFileEmpty && pipeEmpty && robEmpty);
}

void printLog (char* argvZero, char* traceFile) {
    cout << "# === Simulator Command =========\n";
    printf( "# %s %d %d %d %s\n",argvZero, ROB_Size, IQ_Size, Width, traceFile);
    cout << "# === Processor Configuration ===\n";
    printf( "# ROB_SIZE = %d\n# IQ_SIZE  = %d\n# WIDTH    = %d\n",ROB_Size, IQ_Size, Width);
    cout << "# === Simulation Results ========\n";
    printf( "# Dynamic Instruction Count    = %d\n",numinstr);
    cout << "# Cycles                       = " << simcycle-1 << "\n";
    printf( "# Instructions Per Cycle (IPC) = %.2f\n",IPC);
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        cout << "Error: Program requires exactly four arguments! Aborting...\n";
        return -1;
    }
    ROB_Size = atoi(argv[1]);
    IQ_Size = atoi(argv[2]);
    Width = atoi(argv[3]);
    for (unsigned int a = 0; a < 67; a++) {
        RMT[a].valid = 0;
        RMT[a].tag   = 99;
    }
    if (!ROB_Size || !IQ_Size || !Width) {
        cout << "Error: Pipeline registers needs minimum width of 1! Aborting...\n";
        return -1;
    }
    struct instruction I;
    I.PC=0; I.optype=0; I.originalDest=0; I.dest=0; I.src1=0; I.src2=0; I.valid=0; I.seq=0;
    I.beginTime["FE"]  = 0 ; I.beginTime["DE"]  = 0 ; I.beginTime["RN"]  = 0 ;
    I.beginTime["RR"]  = 0 ; I.beginTime["DI"]  = 0 ; I.beginTime["IQ"]  = 0 ;
    I.beginTime["EX"]  = 0 ; I.beginTime["WB"]  = 0 ; I.beginTime["RT"]  = 0 ;
    I.cyclesInStage["FE"]=0; I.cyclesInStage["DE"]=0; I.cyclesInStage["RN"]=0;
    I.cyclesInStage["RR"]=0; I.cyclesInStage["DI"]=0; I.cyclesInStage["IQ"]=0;
    I.cyclesInStage["EX"]=0; I.cyclesInStage["WB"]=0; I.cyclesInStage["RT"]=0;
    for (unsigned int a = 0; a < Width; a++) {
        DE.push_back(I);
        RN.push_back(I);
        RR.push_back(I);
        DI.push_back(I);
    }
    for (unsigned int a = 0; a < 5*Width; a++) {
        pair<struct instruction, unsigned int> execute (I,10);
        execute_list.push_back(execute);
        WB.push_back(I);
    }
    reorderBuffer ROB(ROB_Size, I);
    string trace = string(argv[4]);
    ifstream ftrace;
    ftrace.open(trace.c_str(),ios::in);
    if(!ftrace.is_open()) {
        cout << "Error: Could not open trace_file! Aborting...\n";
        return -1;
    }
    do {
        RETIRE(&ROB);
        WRITEBACK(&ROB);
        EXECUTE(&ROB);
        ISSUE(&ROB);
        DISPATCH(&ROB);
        REG_READ(&ROB);
        RENAME(&ROB);
        DECODE();
        FETCH(&ftrace);
    } while (advanceCycle(&ROB, &ftrace));
    IPC = float(numinstr)/float(simcycle-1);
    printLog(argv[0], argv[4]);
	return 0;
}