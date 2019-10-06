#include <immintrin.h>
#include <atomic>
#define MIN_SLAVE_DEPTH 7								// min depth where slaves are called
#define SLOG 0 											// log slave activities (19)
#define SLOG_CREATE 1
#define SLOG_BREAK 2
#define SLOG_TERMINATE 4
#define SLOG_START 8
#define SLOG_END 16
#define SLOG_MASK (SLOG_CREATE|SLOG_BREAK|SLOG_TERMINATE|SLOG_START|SLOG_END)
#define SLOG_FILE1 "c://xde//chess//out//slog_i.csv"	// slave log file
#define MAX_SP_NUM 63									// maximum number of split points. Do not exceed 64!.

// spin-lock
class Spinlock {
  std::atomic_int lock; 
public: 
  Spinlock() { lock = 1000; } // Init here to workaround a bug with MSVC 2013
  void acquire() { 
	assert(lock>10);
	assert(lock<1001);
    while (lock.fetch_sub(1, std::memory_order_acquire) != 1000) 
        while (lock.load(std::memory_order_relaxed) < 1000) {_mm_pause();} 
	assert(lock>10);
	assert(lock<1001);
  } 
  void release() { 
	  lock.store(1000, std::memory_order_release);
  }
};

// split-point structure
typedef struct{
	UINT64 slave_bits;		// bits are set for threads working on this sp. Including master (bit 0)
	board b;				// board
	move_list* mlp;			// pointer to move list object
	CONDITION_VARIABLE CVsp;// master is allowed to run on this split point (last slave is done with it)
	unsigned int c_0;		// count of items not analyzed
	unsigned int c_1;		// count of slaves working on this split-point (master is always working on it)
	int be;					// beta
	int depth;				// depth
	unsigned int ply;		// ply
	unsigned int node_type;	// node type
	unsigned int sp_index;	// index of this SP in "sp_all/open_mask" variables. 0 to 63.
	unsigned int sp_created_by_thread;	// index of thread that created this SP; 0+
	best_m bm;
	int in_check;
	int ext_ch;
	Spinlock lock;			// spinlock
	int master_sleeping;	// indicator for when mastert is sleeping on this point waiting for slaves to finish. Then last slave wakes up the master.
	int beta_break;
	#if SLOG
	unsigned int id;		// unique ID of this point
	unsigned int i0;		// i of SP creation
	int t1;					// time SP is created
	#endif
	unsigned char move_hist[MAX_MOVE_HIST][MAX_MOVE_HIST][2];// move history.
} split_point_type;

extern board *b_s;
extern split_point_type *sp_all;
extern UINT64 sp_all_mask;	
extern SRWLOCK L1;
extern CONDITION_VARIABLE CV1;
extern int slave_count;
extern UINT64 thread_running_mask;
extern unsigned int Threads;
extern UINT64 sp_open_mask;

DWORD WINAPI SlaveThreadProc(PVOID);