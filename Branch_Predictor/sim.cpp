#include <iostream>
#include <string>
#include <ctype.h>
#include <stdio.h>
#include <sstream>
#include <fstream>
#include <vector>
#include <math.h>
#include <iomanip>

using namespace std;

unsigned int M1=0, M2=0, N=0, K=0, branch_history=0, predictions=0, mispredictions=0;
/*where K is the number of PC bits used to index the chooser table, 
M1 and N are the number of PC bits and global branch history register bits
used to index the gshare table (respectively), 
and M2 is the number of PC bits used to index the bimodal table.*/

struct branch_struct{
	unsigned int PC;
	bool taken;
};

/*Class implements branch prediction for Bimodal/Gshare and Hybrid predictors.*/
class predictor_table {
public:
    vector<unsigned int> counter;
    unsigned int n;
    unsigned int m;
    predictor_table(unsigned int, unsigned int, unsigned int); //Constructor Function
    unsigned int find_index(branch_struct); // Find index pointed to by trace_line PC
    void update_counter(unsigned int, bool); // Update Counter in File after prediction
    bool predictor_access(branch_struct); // Individually access Bimodal/Gshare predictors
    /*Hybrid Access accesses Bimodal/Gshare predictors and updates Chooser table*/
    bool hybrid_access(branch_struct, class predictor_table*, class predictor_table*);
    void print_table();
};

predictor_table::predictor_table (unsigned int n_in, unsigned int m_in, unsigned int counter_val) {
    n = n_in;
    m = m_in;
    for (unsigned int i=0; i<pow(2,m); i++) {
        counter.push_back(counter_val);
    }
}

unsigned int predictor_table::find_index(branch_struct b) {
    unsigned int mask   = (m==32) ? -1 : (1<<m)-1; // mask is m-bit 1 appended with zeros.
    unsigned int index  = (b.PC>>2) & mask;   // Index is bits 2 to m+1
    unsigned int indexmn= index % (1<<(m-n)); // Lower m-n bits
    unsigned int indexn = index / (1<<(m-n)); // Upper n bits.
    if (n>0) {
        indexn = indexn ^ branch_history; // Xor-ing with branch history only in Gshare case.
    }
    index  = (indexn << (m-n) ) + indexmn;
    return index;
}

void predictor_table::update_counter(unsigned int index, bool taken) {
    if (counter[index]!=3 && taken ) {
        counter[index]++; // Increment counter for taken but saturate at 3.
    }
    else if (counter[index]!=0 && !taken ) {
        counter[index]--; // Decrement counter for not taken but saturate at 0.
    }
}

bool predictor_table::predictor_access(branch_struct branch) {
    unsigned int predictor_index = find_index(branch); // Returns index to counter file
    bool predictor_result = (branch.taken==(counter[predictor_index]>=2));
    update_counter(predictor_index, branch.taken);
    return predictor_result;
}

bool predictor_table::hybrid_access(branch_struct branch, class predictor_table* bimodal, class predictor_table* gshare) {
    unsigned int h_index = find_index(branch);
    unsigned int b_index = bimodal->find_index(branch);
    unsigned int g_index =  gshare->find_index(branch);
    /*Use respective indices to figure out prediction and results*/
    bool    b_result     = (branch.taken==(bimodal->counter[b_index]>=2));
    bool    g_result     = (branch.taken==( gshare->counter[g_index]>=2));
    bool    b_prediction = b_result ? branch.taken: !branch.taken;
    bool    g_prediction = g_result ? branch.taken: !branch.taken;
    /*Hybrid_prediction is that of Gshare when its counter is >=2 and vice versa*/
    bool    h_prediction = counter[h_index]>=2 ? g_prediction : b_prediction;
    bool    h_result     = (branch.taken == h_prediction);
    /*Counter of Predictor accessed for making hybrid prediction needs to be updated*/
    counter[h_index] >=2 ?  gshare->update_counter(g_index, branch.taken)
                         : bimodal->update_counter(b_index, branch.taken);
    if (counter[h_index]!=3 && (g_result > b_result) ) {
        counter[h_index]++; // Increment counter for gshare victory over bimodal but saturate at 3.
    }
    else if (counter[h_index]!=0 && (g_result < b_result) ) {
        counter[h_index]--; // Decrement counter for gshare defeat over bimodal but saturate at 0.
    }
    return h_result;
}

void predictor_table::print_table() {
    for (unsigned int i=0; i<pow(2, m); i++) {
        cout << " " << i << "\t" << counter[i] << "\n";
    }
}

int main (int argc, char* argv[]) {
	string predictor = string(argv[1]);
	// Convert predictor string to lowercase and check for correctness
    for(string::size_type i=0; i<predictor.length(); i++) {
    	predictor[i] = tolower(predictor[i]);
    }
    if(predictor!= "bimodal" && predictor != "gshare" && predictor != "hybrid") {
    	cout << "Error: Invalid predictor specified! Aborting...\n";
		return -1;
    }
    if( (predictor =="bimodal" && argc != 4)
     || (predictor == "gshare" && argc != 5)
     || (predictor == "hybrid" && argc != 7) ) {
		cout << "Error: Invalid number of arguments for ";
        cout << predictor << " predictor! Aborting...\n";
		return -1;
	}
    if (predictor=="bimodal") {
    	M2 = atoi(argv[2]);
    }
    else if (predictor=="gshare") {
    	M1 = atoi(argv[2]);
    	N  = atoi(argv[3]);
    }
    else {
    	K  = atoi(argv[2]);
    	M1 = atoi(argv[3]);
    	N  = atoi(argv[4]);
    	M2 = atoi(argv[5]);
    }
    if (N>M1) {
        cout << "Error: Global Branch History larger than prediction table! Aborting...\n";
        return -1;
    }
    if (M1>32 || M2>32) {
        cout << "Error: Prediction table size is capped at 32! Aborting...\n";
        return -1;
    }
    predictor_table gshare (N, M1, 2);
    predictor_table bimodal(0, M2, 2);
    predictor_table hybrid (0, K , 1);
    struct branch_struct branch;
    char actual_branch;
    string trace_line, trace = string(argv[argc-1]);
    ifstream ftrace;
    ftrace.open(trace.c_str(),ios::in);
    if(!ftrace.is_open()) {
        cout << "Error: Could not open trace_file! Aborting...\n";
        return -1;
    }
    while(ftrace >> trace_line >> actual_branch) {
        actual_branch = tolower(actual_branch);
        if (actual_branch != 't' && actual_branch !='n') {
            cout << "Error: Trace file is corrupt! Aborting...\n";
            return -1;
        }
        branch.taken = (actual_branch=='t');
        //Convert trace_line string from hex to binary and store in branch.PC.
  		stringstream ss;
  		ss << hex << trace_line;
  		ss >> branch.PC;
  		bitset<32> b(branch.PC);
        /*Access predictors and increment mispredictions for an incorrect prediction.*/
        if (predictor == "bimodal") {
            mispredictions += !bimodal.predictor_access(branch);
        } else if (predictor == "gshare") {
            mispredictions += !gshare.predictor_access(branch);
        } else if (predictor == "hybrid") {
            mispredictions += !hybrid.hybrid_access(branch, &bimodal, &gshare);
        }
        if (N>0) {
            branch_history>>= 1;
            branch_history += (branch.taken)<<(N-1); // Latest branch info added to upper bit
            unsigned int mask;
            mask = (N==32) ? -1 : (1<<N)-1;
            branch_history &= mask; // Mask used to counter any overflows from N bit history.
        }
        predictions++;
    }
    cout << "COMMAND\n";
    cout << " ./sim " << argv[1] << " " << argv[2] << " " << argv[3];
    if (predictor != "bimodal") {
        cout << " " << argv[4];
    }
    if (predictor == "hybrid" ) {
        cout << " " << argv[5] << " " << argv[6];
    }
    cout << "\nOUTPUT";
    cout << "\n number of predictions:    " << predictions;
    cout << "\n number of mispredictions: " << mispredictions;
    cout << "\n misprediction rate:       ";
    cout << fixed << setprecision(2) << float(mispredictions)/float(predictions)*100 << "%\n";
    if (predictor == "hybrid") {
        cout << "FINAL CHOOSER CONTENTS\n";
        hybrid.print_table();
    }
    if (predictor != "bimodal") {
        cout << "FINAL GSHARE CONTENTS\n";
        gshare.print_table();
    }
    if (predictor != "gshare") {
        cout << "FINAL BIMODAL CONTENTS\n";
        bimodal.print_table();
    }
    return 0;
}