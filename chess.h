#define STRICT
#define _CRT_SECURE_NO_WARNINGS
#define NO_KERNEL_LIST_ENTRY_CHECKS
#include "resource.h"
#include <windows.h>
#include <stdio.h>
#include <assert.h>

#define ENGINE 1		// 1=run as engine, 0=run as windowed interface
#define USE_EGTB 1		// 1
#define ALLOW_LOG 0
#define calc_pst 0		// 1=calculate in eval, 0=update incrementally. Here 0 is slightly faster - use that.
#define player_zorb 0xab42094fee35f92e
#define USE_AVX 0		// this is 5% faster. Also turn off AVX2 compiler switch.
#define SPS_CREATED_NUM_MAX 22 // max SPs created by 1 thread

// portable
#if USE_AVX
	#define USE_PEXT 1					// 1 portable. 3.9%.
	#define blsr64l(a) _blsr_u64(a)		// instruction
	#define blsr32l(a) _blsr_u32(a)		// instruction
#else
	#define USE_PEXT 0					// use magics, not PEXT
	#define blsr64l(a) ((a)&(a-1))		// legacy
	#define blsr32l(a) (unsigned int)(((unsigned int)a)&(((unsigned int)a)-1)) // legacy
#endif

//#define popcnt64l(a) __popcnt64(a)		  // instruction
#define BSF64l(a,b) BitScanForward64(a,b) // instruction
#define BSR64l(a,b) BitScanReverse64(a,b) // instruction

#define popcnt64l(a) f_popcnt64(a) // legacy: +13% run time.

//#define BSF64l(a,b) f_BSF64(a,b) // 32-bit legacy
//#define BSR64l(a,b) f_BSR64(a,b) // 32-bit legacy

#define TRAIN 0 		// 1=logic for training set - no hashing, record all positions. In training, turn-off EGBB.
#define USE_VIRT_MEM 0	// 1 portable. +3.8% run time.

#define last_move_hash_adj zorb[5][0][(b->last_move&56)+7] // only use file of last move
enum EvalType {Full,NoQueens};// Different eval types, used as template parameter

#if ENGINE
	#define get_time (int)timeGetTime // timer function definition: f_timer vs timeGetTime
#else
	#define get_time f_timer // timer function definition: f_timer vs timeGetTime
#endif
#if ALLOW_LOG
#if ENGINE
	#define LOG_FILE1 "c://xde//chess//out//log_e.csv" // main log file for the engine
#else
	#define LOG_FILE1 "c://xde//chess//out//log_i.csv" // main log file for the interface
#endif
#endif

#define EGTBP 6				// 5 or 6 piece endgame tables. Use 6 only for local internet games
#define MAX_SCORE 20000
#define MIN_SCORE -20000
#define GET_BIT(a) BSF64l(&bit,a);a=blsr64l(a);// reset lowest set bit. Works with BitScanForward, not with BitScanReverse!
#define GET_BIT2(a) BSF64l(&bit2,a);a=blsr64l(a);// reset lowest set bit. Works with BitScanForward, not with BitScanReverse!

// play data structure
typedef struct{
	short int stand_pat;
	char ch_ext;		// check extension data
	char cum_cap_val;	// cumulative capture value
	char to_square;		// "to" square
	#if NDEBUG
	#else
	char cap_val;
	unsigned char from;
	unsigned char to;
	unsigned char move_type;
	#endif
} play; // 3/4 bytes

#define MAX_MOVE_HIST 8			// how many moves to keep in PV history. 8.
#define INVALID_LAST_MOVE 64	// 0 causes false positives with cell==b->last_move logic, so use 64.

// board_light data structure
typedef struct{
	UINT64 hash_key;				// save 1 (8). transposition table key=8 8
	UINT64 pawn_hash_key;			// save 2 (8). pawn transposition table key=8 16
	UINT64 colorBB[2];				// bitboard for all pieces of both players: by player only=16 32
	UINT64 piececolorBB[6][2];		// bitboard for all pieces of both players: by piece and player=96 128
	unsigned char piece[64];		// cell values are: 0=empty, 1=pawn, 2=knight, 3=bishop, 4=rook, 5=queen, 6=king. 3 bits. Plus player as 2 top bits(64 or 128). Store byte=64 192
	unsigned int castle;			// save 3 (8). castling possible: white lower(Q), white upper(K), black lower(q), black upper(k). 1=allowed, 0=not allowed. 196
	short int scorem;
	short int scoree;				// material scores. 200
	unsigned char kp[2];			// save 4 (8). king position for both players. Addressable by player-1. 202
	unsigned char player;			// player. 1/2. 203
	unsigned char last_move;		// last move made to. 204
	unsigned char halfmoveclock;	// for 50 move(100 halfmove) rule 205
	unsigned char nullmove;			// 207
	unsigned char filler[2];		// 208
	unsigned int mat_key;			// save 5 (8). 212
	unsigned int piece_value;		// total piece value, 0 to 64. 216
} board_light; // 216 bytes. Used in training only.

// board data structure
typedef struct{
	UINT64 hash_key;				// save 1 (8). transposition table key=8 8
	UINT64 pawn_hash_key;			// save 2 (8). pawn transposition table key=8 16
	UINT64 colorBB[2];				// bitboard for all pieces of both players: by player only=16 32
	UINT64 piececolorBB[6][2];		// bitboard for all pieces of both players: by piece and player=96 128
	unsigned char piece[64];		// cell values are: 0=empty, 1=pawn, 2=knight, 3=bishop, 4=rook, 5=queen, 6=king. 3 bits. Plus player as 2 top bits. Store byte=64 192
	unsigned int castle;			// save 3 (8). castling possible: white lower(Q), white upper(K), black lower(q), black upper(k). 1=allowed, 0=not allowed. 196
	#if calc_pst==0
	short int scorem;
	short int scoree;				// material scores. 200
	#endif
	unsigned char kp[2];			// save 4 (8). king position for both players. Addressable by player-1. 202
	unsigned char player;			// player. 1/2. 203
	unsigned char last_move;		// last move made to. 204
	unsigned char halfmoveclock;	// for 50 move(100 halfmove) rule 205
	unsigned char nullmove;			// 207
	unsigned char filler[2];		// 208
	unsigned int mat_key;			// save 5 (8). 212
	unsigned int piece_value;		// total piece value, 0 to 64. 216
	unsigned int em_break;			// 220 emergency break for the current thread.
	int history_count[12][64];		// history count table, for move sorting. Player*6+piece, to. 3Kb.
	unsigned short int killer[128][2];// killer move table, for move sorting. Ply, 2 moves, to/from combined into short int. 0.5Kb.
	unsigned short int countermove[12][64];// countermove table, for move sorting. OPP Player*6+piece=12, OPP to. to/from combined into short int. 1.5Kb.
	UINT64 position_hist[100+128];	// 220+1312=1540 Z values for search history. For repetition draws. Indexed by ply. Offset by 100, to capture past positions from the game. 1.3 Kb.
	UINT64 node_count;
	unsigned int max_ply;
	unsigned char move_exclude[2];

	// these should be set by master, and is needed by slaves
	unsigned char sp_level;			// number of split-points above this position
	unsigned char sps_created_num;
	unsigned char sps_created[SPS_CREATED_NUM_MAX];
	void * spp;						// pointer to split-point

	// something that i don't need to copy to slave
	play pl[128];					// offset by 1 just in case. Do not copy last 100 of it to slaves. 128*4=0.5 Kb
	unsigned int slave_index;		// master is 0, slaves are 1+. Do not copy it to slaves.
	unsigned int slave_index0;		// master is 0, slaves are 1+. Do not copy it to slaves.
	unsigned char move_hist[MAX_MOVE_HIST][MAX_MOVE_HIST][2];// move history. Do not copy it to slaves. for 8: 2*8*8=128 bytes
} board; // 216 bytes+threading=4,516 if MAX_MOVE_HIST=8.

// data structure for move un-make function
typedef struct{
	UINT64 hash_key;				// 8 transposition table key
	UINT64 pawn_hash_key;			// 16 pawn transposition table key
	unsigned int castle;			// 20 castling possible: white lower(Q), white upper(K), black lower(q), black upper(k). 1=allowed, 0=not allowed.
	short int scorem;
	short int scoree;				// 24 material scores.
	unsigned char kp[2];			// 26 king position for both players. Addressable by player-1.	
	unsigned char player;			// 27 player. 1/2.
	unsigned char last_move;		// 28 last move made to.
	unsigned char halfmoveclock;	// 29 for 50 move(100 halfmove) rule
	unsigned int move_type;			// type of move: 0=quiet; 1=castling; 2=capture, including ep; 4=promotion; 8=ep capture. Combinations are allowed.
	unsigned char w;				// piece captured
	unsigned char cc;				// square where piece was captured (to/to1)
	unsigned char from;				// from
	unsigned char to;				// to
	unsigned int mat_key;			// material key
	unsigned int piece_value;		// total piece value, 0 to 64.
	unsigned char promotion;		// 0-Q,1-R,2-B,3-N
	unsigned char dummy[4];
} unmake; // 32+8+4+align=48 bytes

// main transposition table data structure
typedef struct{
	unsigned short int lock2;		// 2 bytes of lock = 16
	unsigned char lock1;			// 1 more byte of lock =24
	unsigned char lock1a:2,			// 2 more bits of lock, for total lock size of 2*8+2=26 bits. = 26
				  depth:6;			// 6 bits of depth: 0 to 63 = 32
	short int score;				// 16 bits, score, +-10K. Need 15 bits to cover +-16K. = 48
	unsigned char from:6,			// "from". Need 6 bits. Has to be immediately after "score" = 54
				  type:2;			// score type: 0/1/2=exact,lower,upper. Need 2 bits. = 56
	unsigned char to:6,				// "to". Need 6 bits. = 62
				  age:2;			// age: 0 to 3. Need 2 bits. = 64
} hash; // 8 bytes.

// index into the main hash table
#define get_hash_index ((b->hash_key&hash_index_mask)<<2) // always start at the beginning of block of 4 - 4-way set-associative structure - corrected

// index into the eval hash table
#define get_eval_hash_index (b->hash_key%EHSIZE)

// size of pawn hash table
#define PHSIZE (1024*512) // 1/2 Mill entries * 8 bytes= 4 Mb. Fits in L3 cache.
//#define PHSIZE (1024*1024*4) // 4 Mill entries * 8 bytes= 32 Mb. For internet games and TCEC.

// size of eval hash table
#define EHSIZE (1024*512) // 1/2 Mill entries * 8 bytes= 4 Mb. Fits in L3 cache. Increasing this significantly improves runtime!
//#define EHSIZE (1024*1024*32) // 32 Mill entries * 8 bytes=256 Mb. For internet games and TCEC.

// transposition table return data structure
typedef struct{
	int alp;
	int be;
	int tt_score;
	unsigned char move[2];		// from, to
	unsigned char depth;
	unsigned char bound_type;	// score type: 0/1/2=exact,lower,upper.
} hash_data; // 16 bytes

// move data structure
typedef struct{
	int score;					// move score. TT move is 1e6. Capture is 200+. Countermove is 195. Killers are 0-100. Quiet moves are negative.
	unsigned char from,to;
} move; // 8 bytes (with alignment)

// best move structure
typedef struct{
	int legal_moves;  // 0/1
	int best_score;
	int alp;
	unsigned char best_move[2];
} best_m; // 16 bytes (with alignment)

// move list data structure
typedef struct{
	UINT64 pinBB;
	UINT64 opp_attacks;			// mask of opp attacks, for selecting losing moves
	unsigned int moves_generated;// 1=captures, 2=non-captures, 32=all
	unsigned int status;		// 0-9=uninitialized; 10-14=initialized(move type identified); 15=TT move is next; 20-29=captures are sorted; 30-39=killers are sorted; 40-49=quiet moves are sorted; 50-59=all moves are processed
	unsigned int next_move;		// next move to be returnd. Start at 0, end at mc
	unsigned int moves_avalaible;// sorted
	unsigned int previos_stages_mc;
	unsigned int mc;			// unsorted
	unsigned int MCP_depth1;
	move sorted_moves[128];
	int score[128];				// move score. TT move is 1e6. Capture is 1000+. Killers are 0-100. Quiet moves are negative.
	unsigned char list[256];	// list of unsorted moves, up to 128 of them
	short int TTmove;
} move_list;

// function prototypes
unsigned int f_popcnt64(UINT64);
unsigned char f_BSF64(unsigned long*,unsigned __int64);
unsigned char f_BSR64(unsigned long*,unsigned __int64);
void init_material(void);
unsigned int get_all_moves(board*,unsigned char*);
unsigned int get_all_attack_moves(board*,unsigned char*);
unsigned int get_out_of_check_moves(board*,unsigned char*,unsigned int,unsigned int);
unsigned int find_all_get_out_of_check_moves_slow(board*,unsigned char*);
void make_null_move(board*);
void unmake_null_move(board*);
void make_move(board*,unsigned char,unsigned char,unmake*);
void unmake_move(board*,unmake*);
unsigned int boards_are_the_same(board*,board*,unsigned char,unsigned char);
int Msearch(board*,const int,const unsigned int,int,int,unsigned int);
unsigned int get_piece_moves(board*,unsigned char*,unsigned char,unsigned int);
void init_hash(void);
void init_moves(void);
void init_piece_square(void);
void clear_hash(unsigned int);
unsigned int lookup_hash(unsigned int,board*,hash_data*,unsigned int);
void add_hash(int,int,int,unsigned char*,unsigned int,board*,unsigned int);
unsigned int cell_under_attack(board*,unsigned int,unsigned char);
unsigned int player_moved_into_check(board*,unsigned int,unsigned char);
unsigned int print_position(char*,board *);
UINT64 perft(board*,int);
unsigned int move_is_legal(board*,unsigned char,unsigned char);
UINT64 get_TT_hash_key(board*);
UINT64 get_pawn_hash_key(board*);
unsigned int get_mat_key(board *);
int get_piece_value(board *);
void get_scores(board*);
int pawn_score(board*);
unsigned int player_is_in_check(board*,unsigned int);
unsigned int move_list_is_good(board*,unsigned char*,unsigned int);
unsigned int checkmate(board*);
unsigned int bitboards_are_good(board*);
void init_board_FEN(char*,board*);
void set_bitboards(board*);
void solve_prep(board*);
void init_all(unsigned int);
void init_board(unsigned int,board*);
void decode_move(unsigned char*,char*,board*);
unsigned int get_legal_moves(board*,unsigned char*);
void train(void);
void check_move(unsigned char*,unsigned char*,unsigned int);
int eval(board*);
int Qsearch(board*,unsigned int,int,int,int);
UINT64 attacks_bb_R(int,UINT64);
UINT64 attacks_bb_B(int,UINT64);
UINT64 flip_color(UINT64);
void init_threads(unsigned int);
unsigned int move_gives_check(board *,unsigned int,unsigned int);
unsigned int moving_piece_is_pinned(board *,unsigned int,unsigned int,unsigned int);
int f_timer(void);
void pass_message_to_GUI(char*);
unsigned int hashfull(void);

// vars
extern FILE *f_log;
extern UINT64 hash_index_mask;
extern unsigned int HBITS;
extern unsigned int TTage;
extern int endgame_weight_all_i[];
extern unsigned char dist[64][64];
extern short int piece_square[6][2][64][2];
extern unsigned int depth0;
extern UINT64 zorb[6][2][64];
extern int timeout;
extern int timeout_complete;
extern int time_start;
extern UINT64 *ph;
extern board b_m;
extern UINT64 ray_segment[64][64];
extern UINT64 knight_masks[];
extern UINT64 bishop_masks[];
extern UINT64 rook_masks[];
extern UINT64 king_masks[];
extern UINT64 dir_mask[5][64];
extern HWND hWnd_global;
extern const unsigned char flips[64][8];
extern const UINT64 passed_mask[];
extern const UINT64 blocked_mask[];
extern unsigned char dir_norm[64][64];
extern const unsigned int mat_key_mult[];
extern hash *h;
extern const UINT64 pawn_attacks[2][64];
extern unsigned int tb_loaded,UseEGTBInsideSearch,EGTBProbeLimit;