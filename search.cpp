// main search functionality
#include "chess.h"
#include <intrin.h>
#include <math.h>
#include "threads.h"

void pass_message_to_GUI(char *);

static const unsigned int tto[]={0,3,3,2};	// node type of other children: PV->CUT, ALL->CUT, CUT->ALL
static const int piece_value_search[]={0,106,406,449,653,1327,1625,100}; // +69 on 8/1/2017

// globals
UINT64 tbhits;
unsigned int pv10[]={0,0,3,3,5,10,100}; // used for almost everything. Need king value!
int timer_depth;						// depth at which timer is checked

int see_move(board *,unsigned int,unsigned int);
static void Qsort(board*,unsigned char*,unsigned int);

static void reduce_history(unsigned int bits,board *b){for(unsigned int i=0;i<sizeof(b->history_count)/sizeof(int);++i) b->history_count[0][i]=b->history_count[0][i]>>bits;}

static UINT64 get_opp_attacks(board *b){
	static const unsigned int dirs1[2][2]={{7,9},{9,7}};
	UINT64 opp_attacks,o,a;
	unsigned long bit;
	unsigned int pll=b->player-1,opp=pll^1;

	a=b->piececolorBB[0][opp];
	opp_attacks=(a<<dirs1[0][pll])|(a>>dirs1[1][pll]);						// all pawn attacks
	opp_attacks|=king_masks[b->kp[opp]];									// all king attacks
	a=b->piececolorBB[1][opp];												// all knights
	o=b->colorBB[opp];														// all opponent pieces - cannot X-ray through them.
	while( a ){
		GET_BIT(a)
		opp_attacks|=knight_masks[bit];										// all knight attacks
	}
	a=b->piececolorBB[2][opp]|b->piececolorBB[4][opp];						// all bishops*
	while( a ){
		GET_BIT(a)
		opp_attacks|=attacks_bb_B(bit,o);									// all bishop* attacks, including X-ray
	}
	a=b->piececolorBB[3][opp]|b->piececolorBB[4][opp];						// all rooks*
	while( a ){
		GET_BIT(a)
		opp_attacks|=attacks_bb_R(bit,o);									// all rook* attacks, including X-ray
	}
	return(opp_attacks);
}

extern int param1,param2;
extern unsigned char g_promotion;

int probe_wdl(board*,int*);// Probe EGTB.
int probe_dtz(board*,int*);
static UINT64 root_hash;
static unsigned int pc_root;

static const int FU[6]={0,80,150,250,450,910};						// move futility margin
static const int futility_margin[8]={0,0,80,150,250,450,910};		// position futility margin, for depth 1-6. d=0 is never used.
static const unsigned int MCP[10]={0,11,12,10,20,27,29,58,53,69};	// move count based pruning, for depth 1-9.
static const float lmr_a[]={0.f,-0.60f,-0.50f,1.33f};				// additive LMR
static const float lmr_m[]={0.f, 0.52f, 0.46f,0.44444f};			// multiplicative LMR

unsigned int get_all_moves_new_part1(board*,unsigned char*,UINT64*);// get list of all available moves - captures and promotions only. Return count. Put moves on the list.
unsigned int get_all_moves_new_part2(board*,unsigned char*,UINT64);// get list of all available moves - excluding captures and promotions. Return count. Put moves on the list.

static inline int return_move(move_list *m,move *m1){*m1=m->sorted_moves[m->next_move++];return(1);}// return next move

typedef struct {
	float out_1_nn[64];					// outputs for 1st layer
	float out_2_nn[8];					// outputs for 2nd layer
	float out_lasta_nn;					// outputs for last layer
	unsigned int inp_nn2[64];			// index of up to 64 pieces on the board. First one is bias - always 1. Terminated by 1000. Max length=32+bias+terminator=34.
} data_nn;

//float pass_forward_2_float(board *b,data_nn *d_nnp,unsigned int from,unsigned int to,unsigned int node_type,int SEE,int hist);
static int get_next_move(board *b,unsigned int ply,move_list *m,move *m1,unsigned short int cm,int depth,int node_type){
	unsigned int i,j;
	int curr_score;

	if( m->moves_generated>1 && m->next_move>=m->mc )
		return(0); //all moves generated and processed - finish

	if( m->status==0 ){		// select TT move.
		m->status=1;		// 1=TT move picked
		if( m->TTmove ){
			((short int*)&m1->from)[0]=m->TTmove;m1->score=1000000;	// copy TT move into return
			m->sorted_moves[0]=*m1;									// copy TT move into first sorted slot. Then skip it in all sorts.
			m->next_move=m->moves_avalaible=1;
			return(1);
		}
	}

	if( m->status<10 && (m->moves_generated&32) ){// initialize the list - all moves have been generated
		assert(cell_under_attack(b,b->kp[b->player-1],b->player)); // this should only be called when in check
		unsigned int r_h=0,ply2=ply;
		if( ply>=2 ) ply2=ply-2;
		unsigned short int k1=b->killer[ply][0],k2=b->killer[ply][1];		// killer moves
		unsigned short int k1a=b->killer[ply2][0],k2a=b->killer[ply2][1];	// killer moves a
		for(i=0;i<m->mc;++i){// init score
			if( ((short int*)&m->list[2*i])[0]==m->TTmove ){ // skip TT move - it has already been processed, so exclude it from sorted move list
				m->score[i]=1000000;
				continue;
			}
			unsigned char from=m->list[2*i],to=m->list[2*i+1];
			if( b->piece[to] || ((b->piece[from]&7)==1 && (to==b->last_move || (to&7)==0 || (to&7)==7)) ){ // capture or promotion or ep capture
				int v1=piece_value_search[b->piece[to]&7];
				int v2=piece_value_search[b->piece[from]&7];
				curr_score=300+(v1<<4)-v2;	// most valuable victim/least valuable attacker
			}else if( cm==((short int*)&m->list[2*i])[0] )// countermove
				curr_score=195;
			else if( k1==((short int*)&m->list[2*i])[0] )// killer(s).
				curr_score=90;
			else if( k2==((short int*)&m->list[2*i])[0] )
				curr_score=80;
			else if( k1a==((short int*)&m->list[2*i])[0] )
				curr_score=70;
			else if( k2a==((short int*)&m->list[2*i])[0] )
				curr_score=60;
			else{// no captures, no killers - use history.
				int hh=b->history_count[(b->player-1)*6+(b->piece[from]&7)-1][to];
				if( b->slave_index0==0 && abs(hh)>(1<<28) ) r_h=1;// master only. Max bits are 31. Reduce if>28
				curr_score=-1000000000+hh;// use history. -1 Bil
			}

			// insertion sort
			m->score[i]=curr_score;
			j=m->moves_avalaible;
			while( j && curr_score > m->sorted_moves[j-1].score ){	// value to insert doesn't belong where the hole currently is, so shift 
				m->sorted_moves[j]=m->sorted_moves[j-1];		// shift the larger value down
				j--;											// move the hole position down
			}
			m->sorted_moves[j].score=curr_score;
			((short int*)&m->sorted_moves[j].from)[0]=((short int*)&m->list[2*i])[0];
			m->moves_avalaible++;
		} // end of "i" loop
		if( r_h ) reduce_history(1,b);
		m->status=40;// fully sorted
	} // end of "initialize the list"

	// pick next best move - captures
	if( m->status<20 ){// sort captures. Only for partial move list (empty right now).
		// get mask of squares protected by pawns
		/*UINT64 bb;
		if( b->player==1){// white move, black pawns are protecting
			UINT64 a=b->piececolorBB[0][1];
			bb=(a<<7)&(a>>9);
		}else{// black move, white pawns are protecting
			UINT64 a=b->piececolorBB[0][0];
			bb=(a<<9)&(a>>7);
		}*/
		m->moves_generated|=1;									// captures generated
		m->mc=get_all_moves_new_part1(b,&m->list[0],&m->pinBB);	// captures generated
		UINT64 one=1;
		for(i=0;i<m->mc;++i){
			if( ((short int*)&m->list[2*i])[0]==m->TTmove ){ // skip TT move
				m->score[i]=1000000;
				continue;
			}
			int v1=piece_value_search[b->piece[m->list[2*i+1]]&7];// victim, times 16
			int v2=piece_value_search[b->piece[m->list[2*i]]&7];// attacker
			if( !v1 && v2==piece_value_search[1] ){// P moving to empty square
				if( ((one<<m->list[2*i+1])&0x8181818181818181) ){// pawn moves to empty cell on rank 1/8 - promotion. Make it Q-P captured by Q
					v1=piece_value_search[5]-piece_value_search[1];
					v2=piece_value_search[5];
				}else if( m->list[2*i+1]==b->last_move)// EP capture: P vs P
					v1=piece_value_search[1];
			}

			assert(v1); // captures should always have a victom
			curr_score=3000+300+(v1<<4)-v2;	// most valuable victim/least valuable attacker. Here range is 3,700-19,200.
			assert(curr_score>=200); // capture should always be >=200

			// move losing captures to the end
			if( ((m->opp_attacks>>m->list[2*i+1])&1) && v1<v2 ){// only if "to" is attacked by opp. And losing capture.
				int s=see_move(b,m->list[2*i],m->list[2*i+1]);
				if( s<-99 )// only for material loss, not PST loss
					curr_score+=s;// here range is 16,600-2,355
			}

			// insertion sort
			j=m->moves_avalaible;
			m->score[i]=curr_score;		// so that later i can sort all of them together
			while( j && curr_score > m->sorted_moves[j-1].score ){	// value to insert doesn't belong where the hole currently is, so shift 
				m->sorted_moves[j]=m->sorted_moves[j-1];		// shift the larger value down
				j--;											// move the hole position down
			}
			m->sorted_moves[j].score=curr_score;
			((short int*)&m->sorted_moves[j].from)[0]=((short int*)&m->list[2*i])[0];
			m->moves_avalaible++;
		}
		m->status=20; // captures have been sorted
	}// end of "sort captures"

	if( m->status<30 && m->next_move < m->moves_avalaible )// select next capture move
		return(return_move(m,m1));
	
	if( m->status<30 ){// sort killers
		m->moves_generated|=2;								// non-captures generated
		i=m->mc;
		m->previos_stages_mc=i; //save, so that i can skip them later
		m->mc+=get_all_moves_new_part2(b,&m->list[2*i],m->pinBB);	// non-captures generated
		unsigned int ply2=ply;
		if( ply>=2 ) ply2=ply-2;
		unsigned short int k1=b->killer[ply][0],k2=b->killer[ply][1];		// killer moves
		unsigned short int k1a=b->killer[ply2][0],k2a=b->killer[ply2][1];	// killer moves a
		for(;i<m->mc;++i){
			if( ((short int*)&m->list[2*i])[0]==m->TTmove ){ // skip TT move
				m->score[i]=1000000;
				continue;
			}
			if( cm==((short int*)&m->list[2*i])[0] )// countermove.
				curr_score=195;
			else if( k1==((short int*)&m->list[2*i])[0] )// killer(s).
				curr_score=90;
			else if( k2==((short int*)&m->list[2*i])[0] )
				curr_score=80;
			else if( k1a==((short int*)&m->list[2*i])[0] )
				curr_score=70;
			else if( k2a==((short int*)&m->list[2*i])[0] )
				curr_score=60;
			else{
				m->score[i]=-1000000000; // so that i can identify these moves later, and process them
				continue;
			}

			// insertion sort
			m->score[i]=curr_score;
			j=m->moves_avalaible;
			while( j && curr_score > m->sorted_moves[j-1].score ){	// value to insert doesn't belong where the hole currently is, so shift 
				m->sorted_moves[j]=m->sorted_moves[j-1];		// shift the larger value down
				j--;											// move the hole position down
			}
			m->sorted_moves[j].score=curr_score;
			((short int*)&m->sorted_moves[j].from)[0]=((short int*)&m->list[2*i])[0];
			m->moves_avalaible++;
		}// end of "i" loop
		m->status=30; // killers have been sorted
	}// end of "sort killers"

	if( m->status<40 && m->next_move < m->moves_avalaible )// select next killer
		return(return_move(m,m1));

	if( m->status<40 ){// sort quiet moves
		// prep check search data structures
		UINT64 to_mask[7],from_mask,o;
		if( m->MCP_depth1<128 ){
			unsigned int kpo=b->kp[2-b->player]; // opp king
			to_mask[1]=UINT64(1)<<kpo;
			if( b->player==1 ) to_mask[1]=(to_mask[1]>>9)|(to_mask[1]<<7);// white P checking black K: K-9, K+7
			else to_mask[1]=(to_mask[1]>>7)|(to_mask[1]<<9);// black P checking white K: K+9, K-7
			to_mask[2]=knight_masks[kpo];// N
			o=b->colorBB[0]|b->colorBB[1];
			to_mask[3]=attacks_bb_B(kpo,o);// B
			to_mask[4]=attacks_bb_R(kpo,o);// R
			to_mask[5]=to_mask[3]|to_mask[4];// Q
			to_mask[6]=0;// K
			from_mask=0;
			if( bishop_masks[kpo]&(b->piececolorBB[2][b->player-1]|b->piececolorBB[4][b->player-1]) ) from_mask=to_mask[3];
			if( rook_masks[kpo]&(b->piececolorBB[3][b->player-1]|b->piececolorBB[4][b->player-1]) ) from_mask|=to_mask[4];
		}
		for(i=m->previos_stages_mc;i<m->mc;++i){// skip all the previous stages (captures)
			if( ((short int*)&m->list[2*i])[0]==m->TTmove || m->score[i]>=0 ) // skip TT move, or capture, or killer
				continue;

			// no captures, no killers - use history.
			unsigned char from=m->list[2*i],to=m->list[2*i+1];
			curr_score=-1000000000+b->history_count[(b->player-1)*6+(b->piece[from]&7)-1][to];// use history. -1 Bil
			j=m->moves_avalaible;

			// MCP cut. This changes node count: late checking moves that were kept and cause cuts will impact history on all prevoius moves. But this removes those moves from the list!
			if( 
				( j>=m->MCP_depth1+10 || ( j && j>=m->MCP_depth1-10 && curr_score<-1000000000 ) )	// +4 ELO vs cs<=0
				&& curr_score <= m->sorted_moves[min(j,m->MCP_depth1)-1].score					// make sure to use defined score!
				&& (
					!(to_mask[b->piece[from]&7]&(UINT64(1)<<to)) // no regular checks - estimated (=complete)
					&& ( !(from_mask&(UINT64(1)<<from)) || !moving_piece_is_pinned(b,from,to,b->player) ) // no discovered checks - estimated or complete
				)
			)
				continue;

			// insertion sort
			while( j && curr_score > m->sorted_moves[j-1].score ){	// value to insert doesn't belong where the hole currently is, so shift 
				m->sorted_moves[j]=m->sorted_moves[j-1];		// shift the larger value down
				j--;											// move the hole position down
			}
			m->sorted_moves[j].score=curr_score;
			((short int*)&m->sorted_moves[j].from)[0]=((short int*)&m->list[2*i])[0];
			m->moves_avalaible++;
		}
		m->mc=m->moves_avalaible; // update move count
		m->status=40; // quiet moves have been sorted

		// drop all unneeded moves now
		if( m->mc > m->MCP_depth1 ){
			for(j=i=m->MCP_depth1;i<m->mc;++i)
				if( to_mask[b->piece[m->sorted_moves[i].from]&7]&(UINT64(1)<<m->sorted_moves[i].to) // regular check - estimated (=complete)
					|| moving_piece_is_pinned(b,m->sorted_moves[i].from,m->sorted_moves[i].to,b->player) // discovered check - complete
				)
					m->sorted_moves[j++]=m->sorted_moves[i];
			m->mc=j; // new move count
		}

		if( m->next_move>=m->mc )
			return(0); //all moves processed
	}// end of "sort quiet moves"

	// select next quiet move
	return(return_move(m,m1));
}

extern unsigned int MaxCardinality;
#define RET(type,score) return(ret_ms(m_excl_l,b,depth,ply,alp_in,be,node_type,((short int*)&hd.move[0])[0],type,score))
static inline int ret_ms(unsigned char *m_excl_l,board *b, const int depth, const unsigned int ply, int alp_in, int be, unsigned int node_type,short int TTmove,int type,int score){
	// write log
	/*static FILE *f=NULL;
	static UINT64 cc=0;
	if( f==NULL ){
		f=fopen("c://xde//chess//out//l.csv","w");
		fprintf(f,"node_type,cut,exit_type,depth,NMs,cc_notNM,cc_NM,m_excl,nullmove,in_check,stand_pat-be,stand_pat,TTmove_from,TTmove_to,FEN\n");
	}

	if( depth>7 && depth<11 ){
		char sss[100];
		print_position(sss,b);
		int x;
		if( abs(stand_pat-be)<300 ) x=5+10*int((stand_pat-be+10000)/10)-10000;
		else if( abs(stand_pat-be)<1000 ) x=50+100*int((stand_pat-be+10000)/100)-10000;
		else x=250+500*int((stand_pat-be+10000)/500)-10000;
		fprintf(f,"%d,%d,%d,%d,%d,%u,%u,%d,%d,%d,%d,%d,%d,%d,%s",node_type,(score>=be?1:0),type,depth,NMs,b->node_count-nc1,nc1-nc0,(((short int*)&m_excl_l[0])[0]?1:0),b->nullmove,in_check,x,stand_pat,(TTmove&255),(TTmove>>8),sss );
		cc++;
		if( cc>66000 ){
			fclose(f);
			exit(0);
		}
	}*/

	return(score);
}

#define bbp(bit) if( bit ) b=b_s+bit-1;else b=&b_m;


#if SLOG
typedef struct{
	int cc;
	char ss[100];
} bbd;

static void beta_break_threads(unsigned int sp_index,bbd* d){
#else
static void beta_break_threads(unsigned int sp_index){
#endif
	board *b;
	UINT64 a=sp_all[sp_index].slave_bits&~(UINT64(1)<<sp_all[sp_index].sp_created_by_thread); // list of all threads working on this SP
	unsigned long bit=sp_all[sp_index].sp_created_by_thread;
	unsigned int i;

	a&=~(UINT64(1)<<bit);				// excluding the owner
	sp_all[sp_index].c_0=0;				// no more moves left at this SP
	InterlockedAnd64((LONG64*)&sp_open_mask,~(UINT64(1)<<sp_index));// count this as closed SP
	while( a ){// here "bit" is slave thread index.
		GET_BIT(a)
		bbp(bit);b->em_break+=2;			// beta break this thread
		#if SLOG
		d->ss[d->cc++]=char('A'+bit);
		#endif
		for(i=0;i<b->sps_created_num;++i)	// and all its children
			#if SLOG
			beta_break_threads(b->sps_created[i],d);
			#else
			beta_break_threads(b->sps_created[i]);
			#endif
	}
}


#if SLOG
int f_timer2(void);
extern FILE *f_slog;
extern int t0;
extern int c_s;
unsigned int id0=0; // SP counter
#endif

#define LOG_SEARCH 0
#if LOG_SEARCH
FILE *fls;
#endif
extern unsigned int cc[15][2010];
extern short int adj[];
static const float ln[64]={0.693147181f,0.693147181f,0.693147181f,1.098612289f,1.386294361f,1.609437912f,1.791759469f,1.945910149f,2.079441542f,2.197224577f,2.302585093f,2.397895273f,2.48490665f,2.564949357f,2.63905733f,2.708050201f,2.772588722f,2.833213344f,2.890371758f,2.944438979f,2.995732274f,3.044522438f,3.091042453f,3.135494216f,3.17805383f,3.218875825f,3.258096538f,3.295836866f,3.33220451f,3.36729583f,3.401197382f,3.433987204f,3.465735903f,3.496507561f,3.526360525f,3.555348061f,3.583518938f,3.610917913f,3.63758616f,3.663561646f,3.688879454f,3.713572067f,3.737669618f,3.761200116f,3.784189634f,3.80666249f,3.828641396f,3.850147602f,3.871201011f,3.891820298f,3.912023005f,3.931825633f,3.951243719f,3.970291914f,3.988984047f,4.007333185f,4.025351691f,4.043051268f,4.060443011f,4.077537444f,4.094344562f,4.110873864f,4.127134385f,4.143134726f};
int Msearch(board *b, const int depth, const unsigned int ply, int alp_in, int be, unsigned int node_type){// main search function. Node type: 1=PV, 2=ALL, 3=CUT
	split_point_type *spl;
	hash_data hd;
	UINT64 KBB;
	unmake d;
	move_list ml;
	best_m *bm,bm_l;
	move mo;
	unsigned int i,j,sp_index;
	int split_point,in_check,stand_pat,ext_ch,new_depth,score,s0,SEE_limit,futility_limit;
	short unsigned int cm;
	unsigned char player,kp0,m_excl_l[2];
	#if ENGINE==0
	assert(!player_is_in_check(b,3-b->player)); // make sure there is no king capture.
	#endif
	assert(ply<128);// check that ply is good.
	if( ply>b->max_ply ) b->max_ply=ply; // if this writes to shared memory every time, multi-threaded process gets delayed a lot!
	assert(b->max_ply<128);
	
	if( b->slave_index ){// called by slave from a split-point - skip most top logic
		assert(b->slave_index0<100);
		split_point=1;					// this is a split-point - slave thread.
		spl=(split_point_type *)b->spp;	// pointer to the split-point object
		sp_index=spl->sp_index;
		b->slave_index=0;				// reset slave index
		
		// copy some items from sp to locals
		in_check=spl->in_check;
		ext_ch=spl->ext_ch;
		SEE_limit=futility_limit=0; // invalid

		// calc some items
		player=b->player;
		kp0=b->kp[player-1];

		bm=&spl->bm;// use SP version
		m_excl_l[0]=m_excl_l[1]=0;
		ml.opp_attacks=spl->mlp->opp_attacks; // save it locally
		goto top_of_the_move_loop;
	}

	// master code*******************************************
	if( ((short int*)b->move_exclude)[0] ){
		((short int*)m_excl_l)[0]=((short int*)b->move_exclude)[0]; // save excluded move locally and reset it
		((short int*)b->move_exclude)[0]=0;
		split_point=0;
	}else
		((short int*)m_excl_l)[0]=0;

	if( ply<MAX_MOVE_HIST ){
		((short int*)&b->move_hist[ply][ply][0])[0]=0; // invalid PV terminator
		if( ply<MAX_MOVE_HIST-1 ) ((short int*)&b->move_hist[ply][ply+1][0])[0]=0; // terminator
	}
	// check for board repetition and 50 move draw, before calling Q search.
	if( b->halfmoveclock>=4 && ply ){// For repeaters, halfmoveclock 4 or more (repeats 0 and 4=current - Pmove,Omove,Punmove,Ounmove). Do not stop ply=0 - that produces no valid move.
		// 50 move draw
		if( b->halfmoveclock>=100 ){
			if( checkmate(b)==2 )// checkmate. Return of 1 means stalemate - do not count it here!
				RET(1,ply-10000); // i have been mated - low (negative) score. 100-ply.
			else
				//RET(2,0);// 50 move rule - draw.
				RET(3,1-2*(ply&1));// Give it +1 cp for top level player to pick 50 move draws over waisting time.
		}

		// loop over previous positions from this search and earlier game
		UINT64 n1=b->hash_key,n2=n1^player_zorb;
		for(j=max(96+ply,99);j>ply+99-b->halfmoveclock && b->position_hist[j];--j){// last: 99+ply. Total: halfmoveclock. Also stop on first empty.
			if( b->position_hist[j]==n1 || b->position_hist[j]==n2 )// found a match - return.
				RET(3,1-2*(ply&1));// Give it +1 cp for top level player to pick repetition draws over waisting time.
		}
	}

	#if USE_EGTB
	// probe syzygy EG tablebases: before Qs, but after repetition
	if( pc_root<=EGTBProbeLimit && pc_root<=MaxCardinality && ply ){
		score=checkmate(b);	
		if( score==2 ) RET(4,ply-10000);// checkmate - return
		else if( score==1 ) RET(5,0);// stalemate - return
		score=probe_dtz(b,&new_depth);
		if( new_depth) tbhits++; // record a hit
		if( new_depth && score==0 ) RET(6,0);// always use draw score "as is"
		if( new_depth && abs(score)<=100 ){// good DTZ access
			if( abs(score)+b->halfmoveclock>100 ) RET(7,0);// draw by 50 move rule - return
			if( score>0 ) RET(8,9000-ply-(b->halfmoveclock>0?score:0)); // zero out score if move was zeroing
			else RET(9,-9000+ply-(b->halfmoveclock>0?score:0)); // zero out score if move was zeroing
		}
	}
	#endif

	// mate-distance pruning
	alp_in=max(alp_in,-10000+(int)ply);
	be=min(be,10000-1-(int)ply);
	if( alp_in>=be ) RET(10,alp_in);

	// put current position (clean hash) on history list, for repetition draw analysis
	b->position_hist[ply+100]=b->hash_key;// record current position on the list of repeaters

	// call Q search on last move*********************************************************
	ext_ch=b->pl[ply].ch_ext; // extend if getting out of check and correct SEE	
	if( depth+ext_ch<=0 ) RET(11,Qsearch(b,ply,alp_in,be,node_type));

	// look in the TT.
	((short int*)hd.move)[0]=0;// mark as invalid move.
	if( !((short int*)m_excl_l)[0] && lookup_hash(depth,b,&hd,ply) && node_type>1 ){// hash found - use the score. No cuts on PV.
		if( hd.alp>=be ){
			if( ply<MAX_MOVE_HIST ) ((short int*)&b->move_hist[ply][ply][0])[0]=((short int*)hd.move)[0]; // record current move.
			RET(12,hd.alp); // fail high
		}else if( hd.be<=alp_in ){
			if( ply<MAX_MOVE_HIST ) ((short int*)&b->move_hist[ply][ply][0])[0]=((short int*)hd.move)[0]; // record current move.
			RET(13,hd.be); // fail low
		}
		alp_in=max(alp_in,hd.alp);
		be=min(be,hd.be);
		assert(alp_in<be);
	}

	#if USE_EGTB
	// use syzygy EG bitbases
	if( UseEGTBInsideSearch			// i'm alowed to use them
		&& ply						// skip ply 0 - need valid move
		&& (i=(unsigned int)popcnt64l(b->colorBB[0]|b->colorBB[1]))<=min(EGTBProbeLimit,MaxCardinality)	// piece count is in the EGBB range
	){
		// Assume good results (win with +8 material advantage) cannot get better. This reduces disc reads greatly, without impacting results
		if( node_type==2 && alp_in>=7300 ) RET(14,alp_in);
		if( node_type==3 && be<=-7300 ) RET(15,be);

		// Do the EGBB probe
		score=probe_wdl(b,&new_depth);
		if( new_depth ){
			tbhits++; // record a hit
			int s_r=0;
			if( score==-1 || score==1 ) score=0; // call these draws.
			#if calc_pst==1
			if( score>0 ) s_r=get_scoree(b)+6500-ply;
			else if( score<0 ) s_r=get_scoree(b)-6500+ply;
			#else
			if( score>0 ) s_r=b->scoree+6500-ply;
			else if( score<0 )  s_r=b->scoree-6500+ply;
			#endif
			RET(16,s_r);
		}
	}
	#endif

	// See if i'm in check.
	in_check=cell_under_attack(b,b->kp[b->player-1],b->player); // from this point on in_check is defined.

	// futility, for d=1-6: if Qscore>be+margin, skip. This stops "previous move is bad" situations.
	if( 
		!((short int*)&m_excl_l[0])[0]	// +6
		&& depth<=6						// here 5 is -11 ELO, 4 is -12 ELO
		&& node_type>1					// +9
		&& !in_check					// +18
		&& abs(be)<3000					// a wash
	){
		s0=futility_margin[depth];
		if( Qsearch(b,ply,be+s0-1,be+s0,4)>=be+s0 ) // null-window around be+s0. This score is never below stand pat. Comparing stand_pat to bounds is done inside this, so no need to do it explicitely.
			RET(17,be); // fail-soft is a wash
			
		// razoring: if Qscore<alp-margin, skip. +1 ELO
		stand_pat=eval(b);// static eval - after futility
		if( depth<4 && !((short int*)&hd.move[0])[0] ){// d=1,2,3. Here excluding TT moves is +1 ELO.
			s0=365; // around 5% below -365
			if( stand_pat<=alp_in-s0 && Qsearch(b,ply,alp_in-s0,alp_in-s0+1,5)<=alp_in-s0 )// null-window around alp-s0. This score is never below stand pat.
				RET(18,alp_in);
		}
		if( b->em_break ) RET(0,alp_in);// timeout or beta cut-off break: return
	}else stand_pat=eval(b);// static eval - after futility

	
	// null-move pruning.
	if( depth>=2							// d=2+: +10 ELO vs >=3, +5 ELO vs >=1
		&& !((short int*)m_excl_l)[0]		// not singular ext: +4 ELO
		&& node_type>1						// not PV node: +1 ELO
		&& !in_check						// not in check: +10 ELO
		&& !b->nullmove						// not second NM in a row: +3 ELO
		&& abs(be)<3000						// not mate score: +5 ELO
		&& (b->piececolorBB[2][b->player-1]|b->piececolorBB[3][b->player-1]|b->piececolorBB[4][b->player-1]) // palyer has>0 sliders: +1 ELO
		&& stand_pat>=be					// PST>=be: +12 ELO
	){
		unsigned int R=2;
		if( depth>7 && stand_pat>=be+100 ) R++; // increase R to 3 for d=8+ and good (100+) score. +6 ELO
		unsigned char lm_l=b->last_move,hm_l=b->halfmoveclock;// save
		if( b->last_move!=INVALID_LAST_MOVE ){
			b->hash_key^=last_move_hash_adj;
			b->last_move=INVALID_LAST_MOVE;			// illegal last move
		}
		make_null_move(b);b->nullmove=1;
		b->halfmoveclock=0;// reset, to avoid repeated positions after 2 moves. Need this. Otherwise opp can always force a draw.
		b->pl[ply+1].ch_ext=0; //no check extension on next move after NM
		b->pl[ply+1].cum_cap_val=-b->pl[ply].cum_cap_val;// cumulative value of pieces captured. Excludes pawns. Used for recaptue extension.
		b->pl[ply+1].to_square=64; // invalid
		score=-Msearch(b,depth-1-R,ply+1,-be,-be+1,tto[node_type]);// Cut search d by R. Zero window. Node type is opposite to current. ply+1.
		b->nullmove=0;unmake_null_move(b);
		b->last_move=lm_l;b->halfmoveclock=hm_l;// restore
		if( b->last_move!=INVALID_LAST_MOVE ) b->hash_key^=last_move_hash_adj;
		if( b->em_break ) RET(0,alp_in);// timeout or beta cut-off break: return
		if( score>=be ){// here storing result in TT is -2 ELO
			if( depth>=10 ){// +6 ELO
				b->nullmove=1; // +6 ELO. Why?
				s0=Msearch(b,depth-1-R,ply,alp_in,be,node_type);// reduce by R+1.
				b->nullmove=0;
				if( b->em_break ) RET(0,alp_in);// timeout or beta cut-off break: return
				if( s0>=be )
					RET(19,score);
			}else
				RET(20,score);// fail-soft. +4 ELO vs returning beta. Returning be for ALL nodes does not help.
		}
	}// end of NM
	b->nullmove=0; // reset


	// check for time out. Both master and slaves get stopped here.
	if( depth>3 && depth>timer_depth ){
		int now=get_time();
		if( now>timeout ){
			// set break for all slaves and master.
			b_m.em_break=1;
			for(i=0;i<(unsigned int)slave_count;++i) b_s[i].em_break=1;
			timer_depth=3;	// reset
			RET(21,MIN_SCORE);
		}
		now=timeout-now;
		if( now<=30 ){ if( timer_depth!=3 ) timer_depth=3; // only write it to memory if it changed
		}else if( now<120 ){ if( timer_depth!=5 ) timer_depth=5;
		}else if( now<350 ){ if( timer_depth!=7 ) timer_depth=7;
		}else if( now<1000 ){ if( timer_depth!=9 ) timer_depth=9;
		}else{ if( timer_depth!=11 ) timer_depth=11;}
	}


	// init for first call to search
	if( ply==0 ){// top level search
		#if LOG_SEARCH
		if( fls==NULL ) fls=fopen("c://xde//chess//out//search_log.csv","w");
		char sss[100];print_position(sss,b);
		fprintf(fls,",depth,%d,FEN,%s",depth,sss);
		#endif
		InterlockedAnd(&sp_open_mask,0); // init
		b->pl[0].ch_ext=b->pl[0].cum_cap_val=0; // reset starting pl values
		for(i=0;i<(unsigned int)slave_count;++i) b_s[i].em_break=0;
		b_m.em_break=0;
		timer_depth=3; // init to something small
		b->sp_level=0; // reset sp level
		b->sps_created_num=0;
		if( depth0==1 || root_hash!=b->hash_key ){// new position.
			reduce_history(4,b); // this is +5 ELO vs resetting history and killers.
			b->pl[0].to_square=64; // init to invalid
		}else// same position, searched to deeper d - reduce by 3
			reduce_history(3,b);// here 2 is -1 ELO
		g_promotion=0;	//init
		root_hash=b->hash_key;
		pc_root=(unsigned int)popcnt64l(b->colorBB[0]|b->colorBB[1]);// save root position piece count
	}


	// Get move list. Here i always have do use "get out of check" logic! This executes 0.073 times per node
	if( in_check ){
		ml.mc=get_out_of_check_moves(b,&ml.list[0],b->kp[b->player-1],in_check-64);
		ml.moves_generated=32; // all moves have been generated
	}else
		ml.mc=ml.moves_generated=0; // moves have NOT been generated. Used for all positions not in check.
	

	player=b->player;
	kp0=b->kp[player-1];

	// see if TT move is legit. I get bad TT move around 2 times an hour per core, including second part, so i need it too!
	if( ((short int*)&hd.move[0])[0] && (ml.moves_generated&32) ){// have move list - look at it
		unsigned int reset=30;
		for(i=0;i<ml.mc;++i)// insert TT move in the first slot.
			if( ((short int*)&hd.move[0])[0]==((short int*)&ml.list[2*i])[0] ){// TT move found
				reset=0;
				break;
			}
		if( reset )
			((short int*)&hd.move[0])[0]=0;
	}else if( ((short int*)&hd.move[0])[0] ){
		unsigned int reset=0;
		if( (b->piece[hd.move[0]]>>6)!=player || (b->piece[hd.move[1]]>>6)==player ) // wrong player moving, or trying to capture my own piece
			reset=1;
		else if( (b->piece[hd.move[1]]&7)==6 ) // king capture
			reset=2;
		else if( dir_norm[kp0][hd.move[0]] && moving_piece_is_pinned(b,hd.move[0],hd.move[1],3-player) ) // move of pinned piece
			reset=3;
		else{
			switch(b->piece[hd.move[0]]&7){
			case 1: if( player==1 ){// white
				if( !(
					(hd.move[1]==hd.move[0]+1 && b->piece[hd.move[1]]==0 )
					|| ((hd.move[0]&7)==1 && hd.move[1]==hd.move[0]+2 && b->piece[hd.move[1]]==0  && b->piece[hd.move[0]+1]==0 )
					|| (hd.move[1]==hd.move[0]+9 && (b->piece[hd.move[1]] || hd.move[1]==b->last_move) )
					|| (hd.move[1]==hd.move[0]-7 && (b->piece[hd.move[1]] || hd.move[1]==b->last_move) )
					)
				)
					reset=10;
			}else{// black
				if( !(
					(hd.move[1]==hd.move[0]-1 && b->piece[hd.move[1]]==0 )
					|| ((hd.move[0]&7)==6 && hd.move[1]==hd.move[0]-2 && b->piece[hd.move[1]]==0  && b->piece[hd.move[0]-1]==0 )
					|| (hd.move[1]==hd.move[0]-9 && (b->piece[hd.move[1]] || hd.move[1]==b->last_move) )
					|| (hd.move[1]==hd.move[0]+7 && (b->piece[hd.move[1]] || hd.move[1]==b->last_move) )
					)
				)
					reset=10;
			}
			break;
			case 2: if( !(knight_masks[hd.move[0]]&(UINT64(1)<<hd.move[1])) ) reset=11; // Complete
				break;
			case 3: if( dir_norm[hd.move[0]][hd.move[1]]!=7 && dir_norm[hd.move[0]][hd.move[1]]!=9 ) reset=12; // drop if not on diagonal. Incomplete
				else if( ray_segment[hd.move[0]][hd.move[1]]&(b->colorBB[0]|b->colorBB[1]) ) reset=13; // drop if someting is in the way. Complete.
				break;
			case 4: if( dir_norm[hd.move[0]][hd.move[1]]!=1 && dir_norm[hd.move[0]][hd.move[1]]!=8 ) reset=14; // drop if not on a line. Incomplete
				else if( ray_segment[hd.move[0]][hd.move[1]]&(b->colorBB[0]|b->colorBB[1]) ) reset=15; // drop if someting is in the way. Complete.
				break;
			case 5: if( dir_norm[hd.move[0]][hd.move[1]]==0 ) reset=16; // drop if not on a line or diagonal. Incomplete
				else if( ray_segment[hd.move[0]][hd.move[1]]&(b->colorBB[0]|b->colorBB[1]) ) reset=17; // drop if someting is in the way. Complete.
				break;
			case 6:if( dist[hd.move[0]][hd.move[1]]!=1 ) reset=18; // disallow castling - too complicated to check all the conditions. Otherwise, complete
				else if( dist[hd.move[1]][b->kp[2-player]]==1 ) reset=19; // make sure kings do not touch!
				else if( (pawn_attacks[2-player][hd.move[1]]&b->piececolorBB[0][2-player]) ) reset=20; // make sure king is not attacked by opp pawns 
				else{// See if i'm in check after the move
					b->colorBB[player-1]^=UINT64(1)<<kp0;					// update occupied BB of player. here i only need to remove the king from its current position on colorBB board.
					if( player_moved_into_check(b,hd.move[1],player)) reset=21;
					b->colorBB[player-1]^=UINT64(1)<<kp0;					// update occupied BB of player.
				}
				break;
			}
		}
		if( !reset && in_check ){// make sure i am out of check after TT move
			i=kp0; 
			if( i==hd.move[0] ) i=hd.move[1];
			d.promotion=0;//init to Q
			make_move(b,hd.move[0],hd.move[1],&d);
			if( cell_under_attack(b,i,player) ) reset=4;
			unmake_move(b,&d);
		}
		if( reset )
			((short int*)hd.move)[0]=0;
	}
	ml.TTmove=((short int*)hd.move)[0]; // put TT move into move list object
	

	// singular extension
	if( depth>6							// d>X
		&& ply							// skip top position
		&& ((short int*)&hd.move[0])[0] // have hash move: 93%
		&& hd.depth>=depth/2			// hash position is deep enough: 91%
		&& hd.bound_type<2				// TT contains low or exact score
		&& !ext_ch						// not extended already: 98%
		&& !in_check					// not in check: 93%
		&& abs(hd.tt_score)<1000		// score is reasonable: 100%. Cum=45%
		&& !((short int*)m_excl_l)[0]	// not in SE search
		&& ply+depth<115				// limit extreme depths
	){
		*((short int*)b->move_exclude)=((short int*)hd.move)[0];	// set move to exclude
		int be1=hd.tt_score-(50+(depth-10)*6);
		score=Msearch(b,depth/2,ply,be1-1,be1,node_type); // call search**********************************************************************
		if( b->em_break ) RET(0,alp_in);// timeout or beta cut-off break: return
		if( score<=be1-1 ) ext_ch=2; // fail low - extend. Happens 24% for margin of 25. 18% for margin of 50.
	}

	// shallow cut
	spl=NULL;				// not a split-point
	sp_index=split_point=0;	// this is not a split-point - master start. Change it later if needed.
	SEE_limit=futility_limit=ml.moves_avalaible=ml.status=ml.next_move=0; // init
	ml.MCP_depth1=128;
	if( node_type>1 && !in_check && depth<10 ){
		if( depth<=6 ) SEE_limit=1; // SEE cuts for depth 6- only. This is +12 ELO vs 9-. Effect is about the same for 3-6. Cuts over 6 hurt because sometimes opp piece that captures me was protecting some other opp piece, which i can now capture=SEE is incomplete.
		ml.MCP_depth1=MCP[depth];
		if( popcnt64l(b->colorBB[2-b->player]^b->piececolorBB[0][2-b->player])>1+1 ) futility_limit=1;// Opp has more than 1 piece (+king).
	}
	bm_l.alp=alp_in; // save input alpha
	((short int*)bm_l.best_move)[0]=0; // this impacts count. Why?
	bm_l.best_score=MIN_SCORE; // to avoid extreme results when all moves are cut (it happens)
	bm_l.legal_moves=0;
	bm=&bm_l;// use local version

top_of_the_move_loop:// end of top logic, excluded by slave ********************************************************************************************************************************	
	KBB=UINT64(1)<<kp0;
	i=-1; // decrement here, since it is incremented at the top of the loop
	ml.opp_attacks=get_opp_attacks(b);// get a list of all potential attacks by opponent - for SEE logic. Do this always, for recapture
	if( b->pl[ply].to_square<64 ) cm=b->countermove[(2-player)*6+(b->piece[b->pl[ply].to_square]&7)-1][b->pl[ply].to_square]; // only if valid
	else cm=0;
	while( 1 ){// loop over all moves***********************************************************************************
		i++; // this is the only place to do this

		// create a split-point?
		if( depth>=MIN_SLAVE_DEPTH-1 && Threads>1 && !split_point									// there are slaves available. Not a split-point already
			&& ( (node_type==1 && i>0) || node_type==2 || (node_type==3 && i>3) )					// PV after first move, or ALL node. Or CUT node after 3 moves.
			&& ( ml.moves_generated<2 || ml.mc>5+i )												// number of remaining moves is >5
			&& !((short int*)m_excl_l)[0]	     													// not in SE search
			&& depth>=MIN_SLAVE_DEPTH-(popcnt64l(sp_open_mask)<=2?1:0)+(popcnt64l(sp_open_mask)>min(2*Threads+4,MAX_SP_NUM-20)?3:0)+(popcnt64l(sp_open_mask)>min(3*Threads+6,MAX_SP_NUM-10)?3:0) // depth>min
			&& popcnt64l(sp_all_mask)<MAX_SP_NUM													// there are available split-points
			&& b->sps_created_num<SPS_CREATED_NUM_MAX
		){	// save some current info into split-point
			if( !get_next_move(b,ply,&ml,&mo,cm,depth,node_type) ) // simple logic - just take next move.
				break;

			int c1=int(popcnt64l(sp_open_mask)); // i have so many open
			c1-=(Threads-int(popcnt64l(thread_running_mask))); // i need so many for full utilization
			// now c1 is excess capacity
			int dd=min(int(depth0)-3,MIN_SLAVE_DEPTH+max(-1,(c1-2)/2)); // 1/2 reduction in depth
			int i_min=0;
			unsigned int node_type_max=3,node_type_min=1;
			if( c1>0 ){ // have at least 0 open* SPs - now only open new SP on ALL node if i>0
				i_min=1;
				node_type_max=2; // excl 3
				node_type_min=2; // excl 1
				dd=max(dd,MIN_SLAVE_DEPTH+1);
			}
			if( depth>=dd && int(i)>=i_min ){
			AcquireSRWLockExclusive(&L1); // lock the split-point
			if( !b->em_break && popcnt64l(sp_all_mask)<MAX_SP_NUM && node_type<=node_type_max && node_type>=node_type_min ){// check again - this does come into play! And do not create SP if in break.
				// select next available sp data structure.
				DWORD bit;
				BSF64l(&bit,~sp_all_mask);
				assert(bit<MAX_SP_NUM);
				sp_index=bit;
				spl=sp_all+sp_index;
				b->sps_created[b->sps_created_num++]=sp_index; // record and increment
				// save some curent info into spl
				spl->in_check=in_check;
				spl->ext_ch=ext_ch;
				spl->mlp=&ml;
				b->sp_level++;									// increase sp level. Do it before board is copied to spl.
				size_t sizeofboard=sizeof(board)-sizeof(b->move_hist)-100*sizeof(play)-sizeof(b->slave_index)-sizeof(b->slave_index0);// this copies history and countermoves
				memcpy(&(spl->b),b,sizeofboard);

				spl->be=be;
				spl->bm=bm_l;									// copy all local BM into SP
				if( ply<MAX_MOVE_HIST ) for(j=ply;j<MAX_MOVE_HIST;++j){spl->move_hist[ply][j][0]=b->move_hist[ply][j][0];spl->move_hist[ply][j][1]=b->move_hist[ply][j][1];}// copy move history into SP
				bm=&spl->bm;									// use SP version of bm
				spl->depth=depth;
				spl->ply=ply;
				spl->node_type=node_type;
				spl->c_0=1;										// calc remaining moves
				InterlockedOr64((LONG64*)&sp_open_mask,(UINT64(1)<<sp_index));// count this as open SP
				spl->c_1=0;										// no slaves here so far
				spl->sp_index=sp_index;
				spl->master_sleeping=0;							// master is not sleeping
				sp_all_mask=sp_all_mask|(UINT64(1)<<sp_index);	// mark this split point "used"
				spl->slave_bits=(UINT64(1)<<b->slave_index0);	// mark it as being worked on by "master"
				spl->beta_break=0;								// mark this SP as "unbroken
				spl->sp_created_by_thread=b->slave_index0;


				// log the activity
				#if SLOG
				spl->id=id0++;
				if( depth0>=SLOG ){
					if( t0==0 ){
				 		t0=f_timer2();
						f_slog=fopen(SLOG_FILE1,"w");
						fprintf(f_slog,"id,time,threads running,sp_all,sp_open,cat,SP,threadID,c_1,depth,depth0,ply,node_type,i,sp_level,elapsed,beta_cut,breaks\n");
					}
					spl->t1=f_timer2();
					spl->i0=i;
					if( (SLOG_MASK&SLOG_CREATE) ){
						fprintf(f_slog,"%d,%d,%d,%d,%d,",spl->id,spl->t1-t0,int(popcnt64l(thread_running_mask)),int(popcnt64l(sp_all_mask)),int(popcnt64l(sp_open_mask)));
						fprintf(f_slog,"creating,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
							sp_index,b->slave_index0,0,depth,depth0,ply,node_type,i,b->sp_level);	
						c_s++;
					}
					if( c_s>65500 ){
						fclose(f_slog);
						exit(777);
					}
				}
				#endif
				


				ReleaseSRWLockExclusive(&L1);	// release the split-point. It is usually better to release the lock before waking other threads to reduce the number of context switches.
				if( popcnt64l(thread_running_mask)<Threads ) WakeAllConditionVariable(&CV1);	// start the slaves, only if there are idle slave slots.
				split_point=2; // now this is a split-point - "master" thread
			}else
				ReleaseSRWLockExclusive(&L1);	// release the split-point.	
			}
		}
		// select the move*********************************************
		else if( split_point ){
			spl->lock.acquire();				// lock the split-point. This takes some time with many threads.
			
			// get next available move.
			assert(spl->mlp);
			if( spl->c_0==0 || spl->bm.alp>=be || !get_next_move(b,ply,spl->mlp,&mo,cm,depth,node_type) ){// end of move list or beta cut-off - break. No need to pass best move/score here - it is always passed in alpha logic.
				if( spl->c_0 ){
					spl->c_0=0;					// no more moves
					InterlockedAnd64((LONG64*)&sp_open_mask,~(UINT64(1)<<sp_index));// count this as closed SP
				}
				spl->lock.release();			// release the split-point
				break;							// break out of the move loop
			}
			i=spl->mlp->next_move-1;			// i is used to adjust history on cuts
			spl->lock.release();				// release the split-point
		}else if( !get_next_move(b,ply,&ml,&mo,cm,depth,node_type) ) // simple logic for single threaded search - just take next move
			break;

		if( ((short int*)m_excl_l)[0]==((short int*)&mo.from)[0] ) // skip excluded move
			continue;
		
		// pass current move to GUI
		#if ENGINE
		if( ply==0 && depth>14 && !b->slave_index && get_time()-time_start>3000 ){// at least 3 secs and depth 15
			char sss[200];
			sprintf(sss,"info depth %u currmove %c%c%c%c currmovenumber %u\n",depth,mo.from/8+'a',mo.from%8+'1',mo.to/8+'a',mo.to%8+'1',i+1);
			pass_message_to_GUI(sss);
		}
		#endif
		
		// See if i'm in check after the move
		if( mo.from==kp0 ){
			b->colorBB[player-1]^=KBB;					// update occupied BB of player. here i only need to remove the king from its current position on colorBB board.
			j=player_moved_into_check(b,mo.to,player);
			b->colorBB[player-1]^=KBB;					// update occupied BB of player.
			if( j ) continue;
		}

		// skip moves with low SEE=+11 ELO. This does very little: all those bad moves get cut immediately by futility logic. Max speed-up is around 10%, for cutoff of -200.
		else if( // not a king move - king can never be captured
			SEE_limit
			&& ((ml.opp_attacks>>mo.to)&1)							// "to" is attacked by opponent
			&& abs(piece_square[(b->piece[mo.from]&7)-1][player-1][mo.from][1])>200 // moving piece is valuable enough to miss it
			&& piece_value_search[b->piece[mo.to]&7]<piece_value_search[b->piece[mo.from]&7] // bad capture
			&& !moving_piece_is_pinned(b,mo.from,mo.to,player)		// the move does not give discovered check. +3 ELO vs excluding all checks
			&& see_move(b,mo.from,mo.to)<-200						// call SEE. Returns score for current move.
		){
			if( !bm->legal_moves ) bm->legal_moves=1;				// count legal move.
			continue;
		}


		// futility cuts. // FU is only +5 ELO
		unsigned int mgc=move_gives_check(b,mo.from,mo.to);
		if( 
			futility_limit							// not PV, not in check, low depth
			&& mo.score<100							// skip captures/TT. Not excluding killers is a wash.
			&& !mgc									// exclude checking moves. This is +5 ELO vs only excluding discovered checks. +11 ELO vs not excluding any checks
		){
			int LMR1=int(ln[depth]*ln[min(63,i)]*lmr_m[1]+lmr_a[1]);// ln+ln tabulated: baseline
			new_depth=max(0,depth-LMR1);
			if( new_depth<6 ){
				j=(((b->piece[mo.from]&7)-1)<<1)+b->player-1; // index
				int sm=piece_square[0][j][mo.to][0]-piece_square[0][j][mo.from][0]; // mid
				int se=piece_square[0][j][mo.to][1]-piece_square[0][j][mo.from][1]; // end
				sm+=(((se-sm)*endgame_weight_all_i[b->piece_value])>>10); // blended
				if( sm<alp_in-FU[new_depth-1]-stand_pat-40 ){
					if( !bm->legal_moves ) bm->legal_moves=1;	// count legal move.
					continue;
				}
			}
		}

		// check extension - assume promo to Q
		if( mgc && see_move(b,mo.from,mo.to)>-200 )// only if losing less then 2 pawns
			b->pl[ply+1].ch_ext=1;
		else
			b->pl[ply+1].ch_ext=0;

		d.promotion=0;//init to Q
		do{// beginning of the promotion loop
		if( d.promotion ) b->pl[ply+1].ch_ext=0;			// no check ext after under-promo
		assert(b->piece[mo.from]);							// piece to move is not empty
		make_move(b,mo.from,mo.to,&d);						// make the move. This executes 0.38 times per node.***
		assert(get_pawn_hash_key(b)==b->pawn_hash_key);		// make sure pawn hash key is still good
		assert(get_mat_key(b)==b->mat_key);					// make sure material key is still good
		assert(get_TT_hash_key(b)==b->hash_key);			// make sure TT hash key is still good
		assert(bitboards_are_good(b));						// bitboards are good
		assert(get_piece_value(b)==b->piece_value);			// make sure total piece value is still good

		// Extensions
		new_depth=depth-1;
		s0=pv10[d.w&7];										// value of current piece capture
		b->pl[ply+1].cum_cap_val=s0-b->pl[ply].cum_cap_val;	// cumulative value of pieces captured. Excludes pawns. Used for recaptue extension.
		b->pl[ply+1].to_square=mo.to;						// move - to
		b->pl[ply+1].stand_pat=(short int)stand_pat;
		#if NDEBUG
		#else // for debugging, record stuff
		b->pl[ply+1].cap_val=s0;
		b->pl[ply+1].from=mo.from;
		b->pl[ply+1].to=mo.to;
		b->pl[ply+1].move_type=d.move_type;
		#endif
		#if LOG_SEARCH
		for(unsigned int lc1=1;lc1<=ply+1;++lc1)
			fprintf(fls,"move %d=%d/%d/%d,",lc1,b->pl[lc1].from,b->pl[lc1].to,b->pl[lc1].move_type);
		fprintf(fls,"\n");
		#endif

		if(	(
				ext_ch														// Check extension. Exclude checks losing more than something.
			|| ( s0															// capture of piece: +5 ELO vs excluding Q
				&& ply														// not on the first move
				&& ((ml.opp_attacks>>mo.to)&1)==0							// "to" is not attacked by opponent, so clean recapture only
				&& abs(b->pl[ply].cum_cap_val-b->pl[ply-1].cum_cap_val)==s0 // prev move was capture with the same value
				) // recapture ext is +12 ELO
			) && ply+depth<115		// limit extreme depths
		)
			new_depth++;

		//LMR logic
		int LMR=0;
		if( depth>3													// 2 vs 3 is -0 ELO. 4 vs 3 is -6 ELO. 5 vs 3 is -15 ELO.
			&& i>0 													// skip first move. For PV and ALL nodes it would get LMR=0 anyway.
			&& piece_value_search[b->piece[mo.to]&7]>piece_value_search[d.w&7]// skip good captures: +2 ELO
			&& (mo.score<=70 || mo.score>=100 )						// skip killers: +10 ELO. Here including scores of +60 and +70 is +1 ELO. Excluding cm=195 is -6 ELO. 2/2017
			&& !in_check											// do not cut check evasions: +2 ELO. 3/2017
			&& !(d.move_type&5)										// no cuts on promotions or castlings(!&5): +4 ELO. 3/2017
		){
			LMR=max(0,int(ln[min(63,depth)]*ln[min(63,i)]*lmr_m[node_type]+lmr_a[node_type]));// ln+ln tabulated: baseline
			 
			// Increase reduction for non-PV nodes when eval is not improving
			if( LMR && node_type==2 && ply>1 && stand_pat<=b->pl[ply-1].stand_pat )
				LMR++;
		}
		
		b->node_count++; // increase node count
		if( node_type==1 && bm->legal_moves==0 )// first move on PV - use full window and type PV. Here type=1 is important, so that only children of PV are PV.
			score=-Msearch(b,new_depth,ply+1,-be,-bm->alp,1); // call search**********************************************************************
		else{ // all other moves - use type of ALL/CUT
			score=-Msearch(b,new_depth-LMR,ply+1,-bm->alp-1,-bm->alp,tto[node_type]); // call search**************************************************
			if( LMR && score>bm->alp && !b->em_break )// research without LMR. Not doing the re-search is -100 ELO
				score=-Msearch(b,new_depth,ply+1,-bm->alp-1,-bm->alp,tto[node_type]); // call search**************************************************. Here score<=alp 30%. Less for early moves.
			if( score>bm->alp && score<be && !b->em_break ){ // better move - call as PV with full window. Only happens on PV nodes. Because all other nodes always have be=alp+1.
				if( score==bm->alp+1 ) score=bm->alp; // per Enhanced Forward Pruning. +37 ELO
				score=-Msearch(b,new_depth,ply+1,-be,-score,1); // call search**************************************************. Here score<=alp 40% for d=8,50% for d=16, 60% for d=20. Otherwise more likely score>be
			}
		}
		if( !bm->legal_moves ) bm->legal_moves=1;	// count legal move.
		unmake_move(b,&d);
	
		if( b->em_break ) goto end_of_loop_over_moves;// timeout or beta cut-off break: exit move list.
		
		// restore full time, after first move has been processed.
		if( ply==0 && timeout_complete>timeout )
			timeout=timeout_complete;

		if( score>bm->best_score ){// record better move/score
			if( split_point ) spl->lock.acquire(); // lock the split-point
			if( score>bm->best_score ){// second time, in case better move is found by another thread (it happens!)
				bm->best_score=score;bm->best_move[0]=mo.from;bm->best_move[1]=mo.to;bm->alp=max(bm->alp,score);// record current move/score/alpha
				if( ply<MAX_MOVE_HIST ){
					b->move_hist[ply][ply][0]=mo.from;b->move_hist[ply][ply][1]=mo.to;
					for(j=ply+1;j<MAX_MOVE_HIST;++j){
						b->move_hist[ply][j][0]=b->move_hist[ply+1][j][0];b->move_hist[ply][j][1]=b->move_hist[ply+1][j][1];// globalize local move history
					}

					// copy move history into SP
					if( split_point ) for(j=ply;j<MAX_MOVE_HIST;++j){spl->move_hist[ply][j][0]=b->move_hist[ply][j][0];spl->move_hist[ply][j][1]=b->move_hist[ply][j][1];}
				}

				// there are helpers - stop them. Also, mark this SP not open anymore.
				if( split_point && bm->alp>=be ){// SP and beta cut-off
					if( spl->c_0 ){// beta cut-off. This is already under SP lock
						spl->c_0=0;		// no more moves.
						InterlockedAnd64((LONG64*)&sp_open_mask,~(UINT64(1)<<sp_index));// count this as closed SP
					}
					if( spl->c_1 ){ // only break on beta, not alpha. And only if there are helpers.
						AcquireSRWLockExclusive(&L1);		// lock split-points
						if( spl->c_1 && !spl->beta_break ){// do not double-break the same SP! That causes major problems, where beta-break propagates to the root!
							spl->beta_break=1; // mark this  SP as "broken"

							// break all remaining points on SP owner thread
							#if SLOG
							bbd d;
							#endif
							d.cc=0;
							if( split_point==1 ){// this is slave, break all down master SPs
								board *b;		// index of owner thread of this SP
								bbp(spl->sp_created_by_thread)
								b->em_break+=2; // beta break creator of this SP
								#if SLOG
								d.ss[d.cc++]=char('A'+spl->sp_created_by_thread);
								#endif
								for(int ii=b->sps_created_num-1;b->sps_created[ii]!=spl->sp_index;--ii)
									#if SLOG
									beta_break_threads(b->sps_created[ii],&d);
									#else
									beta_break_threads(b->sps_created[ii]);
									#endif
							}else{// this is master - mark it broken
								b->em_break+=2;
								#if SLOG
								d.ss[d.cc++]=char('A'+spl->sp_created_by_thread);
								#endif
							}
							#if SLOG
							beta_break_threads(spl->sp_index,&d);
							d.ss[d.cc]=0;
							#else
							beta_break_threads(spl->sp_index);
							#endif

							// log the activity
							#if SLOG
							if( depth0>=SLOG  && (SLOG_MASK&SLOG_BREAK)){
								static const char s1[2][40]={"beta-breaking helpers(S)","beta-breaking helpers(M)"};
								fprintf(f_slog,"%u,%d,%d,%d,%d,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s\n",
									spl->id,f_timer2()-t0,int(popcnt64l(thread_running_mask)),int(popcnt64l(sp_all_mask)),int(popcnt64l(sp_open_mask)),&s1[split_point-1][0],
									sp_index,b->slave_index0,spl->c_1,depth,depth0,ply,node_type,i,b->sp_level,f_timer2()-spl->t1,(spl->bm.best_score>alp_in?1:0),d.ss);
								c_s++;
							}
							#endif		
							
						}
						ReleaseSRWLockExclusive(&L1);	// release the lock
					}
				}

				// display it
				if( ply==0 ){// only at top level search
					g_promotion=d.promotion;// record global promotion for top level search
					// get total node count
					UINT64 ncl=b_m.node_count;
					for(j=0;j<unsigned int(slave_count);++j) ncl+=b_s[j].node_count;
					#if ENGINE==0
					#if TRAIN==0
					#if ALLOW_LOG
					fprintf(f_log,"Score,%.2f,Time,%i,Depth,%d x %d,Count,%I64u, Move",score/100.,get_time()-time_start,depth,b->max_ply,ncl);
					fprintf(f_log,",%c%c%c%c",bm->best_move[0]/8+'a',bm->best_move[0]%8+'1',bm->best_move[1]/8+'a',bm->best_move[1]%8+'1');// always
					for(j=1;j<min(depth0,MAX_MOVE_HIST) && b->move_hist[0][j-1][0]!=b->move_hist[0][j-1][1] && b->move_hist[0][j][0]+b->move_hist[0][j][1]>0;++j)// get first invalid, unless it is 00.
						fprintf(f_log,",%c%c%c%c",b->move_hist[0][j][0]/8+'a',b->move_hist[0][j][0]%8+'1',b->move_hist[0][j][1]/8+'a',b->move_hist[0][j][1]%8+'1');
					fprintf(f_log,"\n");
					#endif
					#endif
					#else
					// pass resulting move to the GUI
					if( depth>3 ){// skip d 1,2,3 - too much noise
						int te=get_time()-time_start; // elapsed time
						char sss[300];
						unsigned int c=sprintf(sss,"info depth %u seldepth %u score cp %i",depth,b->max_ply,score);
						if( score<=alp_in ) c+=sprintf(sss+c," upperbound");
						else if( score>=be ) c+=sprintf(sss+c," lowerbound");
						c+=sprintf(sss+c," time %u nodes %I64u nps %I64u tbhits %I64u",te,ncl,(ncl*1000)/max(1,te),tbhits);
						if( te>1000 ) c+=sprintf(sss+c," hashfull %d",hashfull());
						c+=sprintf(sss+c," pv %c%c%c%c",bm->best_move[0]/8+'a',bm->best_move[0]%8+'1',bm->best_move[1]/8+'a',bm->best_move[1]%8+'1');
						for(unsigned int lc1=1;lc1<min(depth0,MAX_MOVE_HIST) && b->move_hist[0][lc1][0]!=b->move_hist[0][lc1][1];++lc1)
							c+=sprintf(sss+c," %c%c%c%c",b->move_hist[0][lc1][0]/8+'a',b->move_hist[0][lc1][0]%8+'1',b->move_hist[0][lc1][1]/8+'a',b->move_hist[0][lc1][1]%8+'1');
						sprintf(sss+c,"\n");
						pass_message_to_GUI(sss);
					}
					#endif
				}
			}
			if( split_point ) spl->lock.release(); // unlock the split-point
		}// end of "score>best_score"


		// Beta cut-off
		if( bm->alp>=be ){
			// handle killer move list. Indexed by ply. 2 moves.
			if( d.move_type<2 ){
				// new->1, 1->2.
				if( b->killer[ply][0]!=((short int*)&mo.from)[0] ){// the move is new
					b->killer[ply][1]=b->killer[ply][0];// 1->2
					b->killer[ply][0]=((short int*)&mo.from)[0]; // new->1
				}

				// countermove - always replace. Skip invalids
				if( ply>3 && b->pl[ply].to_square<64 )// only for ply>X? Here 3 is better than 0
					b->countermove[(2-player)*6+(b->piece[b->pl[ply].to_square]&7)-1][b->pl[ply].to_square]=((short int*)&mo.from)[0];
			}
			// update history
			s0=depth; // power=1
			if( d.move_type<2 )// move is quiet
				b->history_count[(player-1)*6+(b->piece[mo.from]&7)-1][mo.to]+=s0; // cut
			// reduce history for prior moves that did not lead to cut.
			if( split_point ){
				for(j=0;j<i;++j)
					if( spl->mlp->sorted_moves[j].score<100 || spl->mlp->sorted_moves[j].score==1000000 )// only if NOT a capture and not a killer: quiet and TT moves only
						b->history_count[(player-1)*6+(b->piece[spl->mlp->sorted_moves[j].from]&7)-1][spl->mlp->sorted_moves[j].to]-=s0; // not a cut - reduce it
			}else{
				for(j=0;j<i;++j)
					if( ml.sorted_moves[j].score<100 || ml.sorted_moves[j].score==1000000 )// only if NOT a capture and not a killer: quiet and TT moves only
						b->history_count[(player-1)*6+(b->piece[ml.sorted_moves[j].from]&7)-1][ml.sorted_moves[j].to]-=s0; // not a cut - reduce it
			}
			i++; // to reuse logic in top sort
			goto end_of_loop_over_moves; // have to use goto instead of break - need to break out of 2 loops!
		}// end of "alp>=be"
		assert(be>bm_l.alp);
		}while( (d.move_type&4) && (++d.promotion)<4 );// end of the promotion loop
	}// end of loop over moves. No beta cut-offs.
	end_of_loop_over_moves:
	
	if( split_point==1 ){ // slave
		b->em_break=0; // reset
		if( b_m.max_ply<b->max_ply ) b_m.max_ply=b->max_ply;					// mass max ply to global master
		InterlockedExchangeAdd64((LONG64*)&b_m.node_count,b->node_count);		// pass node count from slave to global master
		UINT64 m1=~(UINT64(1)<<b->slave_index0);
		// add history change to master. Decreases run time around 1%. 12*64=786 int32 elements. 192 128-bit elements.
		for(unsigned int lc1=0;lc1<sizeof(b_m.history_count)/sizeof(int);lc1+=4) ((__m128i*)&b_m.history_count[0][lc1])[0]=_mm_add_epi32(((__m128i*)&b_m.history_count[0][lc1])[0],_mm_sub_epi32(((__m128i*)&b->history_count[0][lc1])[0],((__m128i*)&spl->b.history_count[0][lc1])[0]));
		AcquireSRWLockExclusive(&L1);		// lock split-points. Need to lock L1 because global thread_running_mask is modified
		assert(spl->c_1>0);
		assert(spl->c_0==0);
		spl->c_1--;							// reduce number of slaves working on this sp
		spl->slave_bits&=m1;				// unassign this thread from this SP
		thread_running_mask&=m1;			// mark this thread as not running. Has to be under lock.
		if( spl->c_1==0 && spl->master_sleeping ){
			WakeConditionVariable(&spl->CVsp);	// all slaves are done with this sp - start the master. If it is sleeping.
			RET(24,1);							// then send slaves to sleep. This helps.
		}
		RET(25,0);// slave - skip the hash add/checkmate logic. Returned value is not used. Keep L1 locked
	}else if( split_point==2 ){// "master": clear the split-point.
		AcquireSRWLockExclusive(&L1);									// lock split-points
		if( spl->c_1 ){// slave(s) are still working on this SP - wait for them to finish.
			UINT64 m1;
			if( b->em_break>=2 )// this is beta cut-off: do not start another helper, this will come to an end soon, just wait for it.
				m1=0;
			else{// no beta cut: other threads may take a while to complete. So start another thread, to keep CPU busy.
				m1=(UINT64(1)<<b->slave_index0);
				thread_running_mask&=~m1;								// mark this thread as not running. This allows other slave threads to wake up. Has to be under lock.
				if( sp_open_mask ) WakeConditionVariable(&CV1);			// wake up 1 slave only. And only if there are open split points.
			}
			while( spl->c_1 ){
				spl->master_sleeping=1;
				if( SleepConditionVariableSRW(&spl->CVsp,&L1,500,0)==FALSE && GetLastError()==ERROR_TIMEOUT ){	// wait for CV and release the lock. When i wake up i have the lock. Limit sleep to 500 msec.
					/*#if ALLOW_LOG
					UINT64 ncl=b_m.node_count;
					for(j=0;j<unsigned int(slave_count);++j) ncl+=b_s[j].node_count;
					fprintf(f_log,"timeout during sleep; thread,%d,time,%d,node_count,%I64d,threads running,%d\n",spl->sp_created_by_thread,get_time()-time_start,ncl,int(popcnt64l(thread_running_mask)));
					#endif*/
					WakeAllConditionVariable(&CV1);	// timeout - wake all slaves. JIC.
				}
			}
			thread_running_mask|=m1;									// mark this thread as running
		}
		assert(spl->c_1==0);

		// move results (bm and move history) from SP into b
		bm_l=spl->bm;
		for(j=ply;j<MAX_MOVE_HIST;++j){b->move_hist[ply][j][0]=spl->move_hist[ply][j][0];b->move_hist[ply][j][1]=spl->move_hist[ply][j][1];}// take move history from split point into board		

		sp_all_mask&=~(UINT64(1)<<sp_index);					// mark this split point "unused". Has to be under lock.
		InterlockedAnd64((LONG64*)&sp_open_mask,sp_all_mask);	// count this as closed SP
		b->sps_created_num--; // decrement
	
		// log the activity
		#if SLOG
		if( depth0>=SLOG && (SLOG_MASK&SLOG_TERMINATE) ){
			fprintf(f_slog,"%d,%d,%d,%d,%d,",spl->id,f_timer2()-t0,int(popcnt64l(thread_running_mask)),int(popcnt64l(sp_all_mask)),int(popcnt64l(sp_open_mask)));
			fprintf(f_slog,"terminating,%d,%d,%d,%d,%d,%d,%d,%dx%d,%d,%d,%d,%dx%d\n",sp_index,b->slave_index0,0,depth,depth0,ply,node_type,spl->i0,i,b->sp_level,f_timer2()-spl->t1,(spl->bm.best_score>alp_in?1:0),b->em_break,b_m.em_break);
			c_s++;
			if( b->em_break>1 )
				fprintf(f_log,"terminating broken thread,%d,em break,%d,beta_break,%d\n",b->slave_index0,b->em_break,spl->beta_break);
		}
		#endif	

		if( spl->beta_break && b->em_break>=2 ) b->em_break-=2;// reset beta break, only if this is real beta cut. Otherwise, let it stay broken.
		ReleaseSRWLockExclusive(&L1);	// release the lock
		b->sp_level--;					// decrease sp level
	}
	#if ALLOW_LOG
	if( ply==0 && b->em_break>1 ){
		fprintf(f_log,"beta break,%d\n",b->em_break);
		fprintf(f_log,"\n");
		#if SLOG
			fclose(f_log);
			fclose(f_slog);
			exit(13124);
		#endif	
	}
	#endif	

	// skip hash/cm logic for timeout
	if( !b->em_break && !((short int*)&m_excl_l[0])[0] ){// need exclusion of incomplete search - sometimes this TT entry gets overwritten, and things go really bad (fine 70)
		if( !bm_l.legal_moves ){// no legal moves. Either checkmate or stalemate.
			if( in_check ) score=ply-10000; // checkmate.
			else score=0; // stalemate.
			add_hash(MIN_SCORE,MAX_SCORE,score,(unsigned char*)piece_value_search,63,b,ply);// empty move. But real score.
			RET(26,score);
		}
		if( bm_l.best_score==MIN_SCORE ) bm_l.best_score=alp_in-50; // do not return MIN_SCORE - it blows up aspiration window. Give it 50 margin
		add_hash(alp_in,be,bm_l.best_score,bm_l.best_move,depth,b,ply);// here it is better to use best move than nothing.
	}
	RET(27,bm_l.best_score);// fail-soft
}

int see_move(board *b,unsigned int from,unsigned int cell){// static exchange evaluator version 3 - return score for the player. The move has not been played. This is called 0.63 times per node.
	UINT64 attackerBB,a,past_attacks,o=b->colorBB[0]|b->colorBB[1],aB,aR,bb,no_from=(~(UINT64(1)<<from));
	unsigned long int froml;
	unsigned int i,i0,j,d,wi,vi,kpl,pll=(b->player-1)^1; // pll is player after the move is made.
	int list[32],va,egw=endgame_weight_all_i[b->piece_value];// blend using initial material
	short int s2[2];
	unsigned char w;

	// calculate move value based on PST
	i0=(b->piece[from]&7)-1;
	w=b->piece[cell];
	vi=(i0<<1)+pll^1;					// index of "from" piece
	if( i0==0 ){// pawn moves - promotions and near promotions.
		((int*)s2)[0]=((int*)&piece_square[0][vi][cell][0])[0];
		((int*)s2)[0]-=((int*)&piece_square[0][vi][from][0])[0];
	}
	else
		((int*)s2)[0]=0;
	if( cell==b->last_move && i0==0 ) // EP capture - make captured piece pawn.
		w=1;
	if( w ){// capture
		w=(w&7);
		wi=(w<<1)+pll-2;								// index of "to" piece
		((int*)s2)[0]-=((int*)&piece_square[0][wi][cell][0])[0];
	}
	va=s2[0]+(((s2[1]-s2[0])*egw)>>10);	// blend
	if( !pll ) va=-va;			// Make it positive.

	// find all attacks on "cell", for both players, excluding currently blocked sliders.
	attackerBB=pawn_attacks[0][cell]&b->piececolorBB[0][0];						// white pawn attacks
	attackerBB|=pawn_attacks[1][cell]&b->piececolorBB[0][1];					// black pawn attacks
	attackerBB|=knight_masks[cell]&(b->piececolorBB[1][0]|b->piececolorBB[1][1]);// knight attacks, both colors
	a=b->piececolorBB[4][0]|b->piececolorBB[4][1];								// queens
	aR=b->piececolorBB[3][0]|b->piececolorBB[3][1]|a;							// Q+R
	o&=no_from;																	// play the move: remove moving piece from o
	attackerBB|=attacks_bb_R(cell,o)&aR;										// rook (and queen) attacks, both colors.
	aB=b->piececolorBB[2][0]|b->piececolorBB[2][1]|a;							// Q+B
	attackerBB|=attacks_bb_B(cell,o)&aB;										// bishop (and queen) attacks, both colors.
	attackerBB|=king_masks[cell]&(b->piececolorBB[5][0]|b->piececolorBB[5][1]); // king attacks, both colors
	attackerBB&=no_from;														// play the move: remove moving piece from list of attackers
	if( !(attackerBB&b->colorBB[pll]) ) return(va);								// cannot capture anything - return "va". This condition reduces execution from 0.63 to 0.45 times per node, or by 29%.
	
	// prep
	list[0]=va;j=1;past_attacks=UINT64(1)<<from;
	do{ // this loop has 1.59 iterations at the top.
		// find the least valuable attacker.
		if( (a=attackerBB&b->piececolorBB[0][pll]) ) i=0;
		else if( (a=attackerBB&b->piececolorBB[1][pll]) ) i=1;
		else if( (a=attackerBB&b->piececolorBB[2][pll]) ) i=2;
		else if( (a=attackerBB&b->piececolorBB[3][pll]) ) i=3;
		else if( (a=attackerBB&b->piececolorBB[4][pll]) ) i=4;
		else{// king, by exclusion.
			if( attackerBB&b->colorBB[pll^1] )// stop if king is captured.
				break;
			a=attackerBB&b->piececolorBB[5][pll];
			i=5; // there is only 1 possible king attacker - no need to pick it!
		}	

		a=a&~(a-1);			// the single attacker.
		o^=a;				// remove the attacker from "o".
		BSF64l(&froml,a);	// get "from"

		// is it pinned?
		kpl=b->kp[pll];
		if( (d=dir_norm[kpl][froml])			// direction to the king is defined - it is on a straight line
			&& d!=dir_norm[cell][froml]		// move is not on the line to the king
			&& !(ray_segment[kpl][froml]&o)	// no blockers in between king and moving piece
		){
			if( d==1 || d==8 ){
				bb=attacks_bb_R(kpl,o)&((b->piececolorBB[3][pll^1]|b->piececolorBB[4][pll^1])&no_from); // rook (and queen) attacks, opp only.
				if( d==1 ) bb&=dir_mask[1][kpl];
				else bb&=dir_mask[3][kpl];
			}else{
				bb=attacks_bb_B(kpl,o)&((b->piececolorBB[2][pll^1]|b->piececolorBB[4][pll^1])&no_from); // bishop (and queen) attacks, opp only.
				if( d==7 ) bb&=dir_mask[2][kpl];
				else bb&=dir_mask[4][kpl];
			}
			if( bb ){// absolute pin - this move is not allowed.
				aR&=~a;			// remove the current attacker from potential future attacks R
				aB&=~a;			// remove the current attacker from potential future attacks B
				attackerBB&=~a;	// remove the current attacker from "attackerBB".
				o^=a;			// put the attacker back
				continue;		// skip to next move
			}
		}

		// calculate move value based on PST
		wi=(i0<<1)+pll^1;// index of "to" piece=prior "from" piece
		((int*)s2)[0]=((int*)&piece_square[0][wi][cell][0])[0]; // remove current "to"
		if( i==0 ){// for pawn, add PST move change: to get promotions and almost promotions.
			vi=(i<<1)+pll;// index of "from" piece
			((int*)s2)[0]-=((int*)&piece_square[0][vi][cell][0])[0]; // add new "to"
			((int*)s2)[0]+=((int*)&piece_square[0][vi][cell][0])[0]; // remove current "from"
		}
		va=s2[0]+(((s2[1]-s2[0])*egw)>>10);	// blend, using starting material.
		if( !pll ) va=-va;// Make it positive.
		list[j++]=va;		// add it to the list

		// add new attacked from now open directions
		if( a&rook_masks[cell] ) attackerBB|=attacks_bb_R(cell,o)&aR;			// rook (and queen) attacks, both colors.
		else if( a&bishop_masks[cell] ) attackerBB|=attacks_bb_B(cell,o)&aB;	// bishop (and queen) attacks, both colors.

		past_attacks|=a;			// add current attack to past attacks
		pll=pll^1;					// switch player.
		attackerBB&=~past_attacks;	// remove the past attackers from "attackerBB".
		i0=i;						// save current attacker
	}while( attackerBB&b->colorBB[pll] );// loop while there are attacks
	if( j==1 )
		return(list[0]);
	for(;j>2;--j) // go over the exchange list. First entry is at position 0, last one is at position j-1. If there is only 1 move (original one), j=1.
		list[j-2]=max(0,list[j-2]-list[j-1]);
	return(list[0]-list[1]);
}

#if TRAIN
extern _declspec(thread) unsigned int eval_counter; // count of eval calls
#endif
int Qsearch(board *b, unsigned int ply, int alp, int be,int node_type){// Q search. It is responsible for 74% of nodes.
	unmake d;
	hash_data hd;
	UINT64 KBB;
	unsigned int mc,i,legal_moves,in_check;
	int score,stand_pat,alp0;
	unsigned char list[256],player,kp0,from,to;
	#if ENGINE==0
	assert(!player_is_in_check(b,3-b->player)); // make sure there is no king capture.
	#endif
	assert(ply<128);// check that ply is good.
	if( ply>b->max_ply ) b->max_ply=ply; // if this writes to shared memory every time, multi-threaded process gets delayed a lot!

										 // look in the TT. Hardcode depth as 0.
	#if TRAIN==0
	if( node_type>1 && lookup_hash(0,b,&hd,ply) ){// hash found - use the score.
		if( be>hd.be ){
			be=hd.be;
			if(be<=alp) return(be);
		}
		if( alp<hd.alp ){
			alp=hd.alp;
			if(alp>=be) return(alp);
		}
	}
	#endif
	alp0=alp; // save original, for hash update. After TT bounds are applied.

	// get "in_check"
	player=b->player;kp0=b->kp[player-1];
	in_check=cell_under_attack(b,kp0,player); // from this point on in_check is defined.
	// get move list. Attack only, if not in check. Here i always have to use "get out of check" logic!
	if( in_check ){// this happens 0.15 times per call.***
		mc=get_out_of_check_moves(b,list,kp0,in_check-64);
		unsigned int j,i1=0;
		int scores[128],temp,temp2;

		scores[0]=2000000000;// max value. Need this to avoid going into negative j.
		for(i=0;i<mc;++i){ // get scores
			// get sort order
			temp=piece_value_search[b->piece[list[2*i+1]]&7]<<4;	// most valuable victim
			temp-=piece_value_search[b->piece[list[2*i]]&7];		// least valuable attacker

			// insertion sort
			j=i1;
			temp2=((short int*)&list[2*i])[0];// save A[i], the value that will be inserted into the array on this iteration
			while( temp>scores[j] ){ //value to insert doesn't belong where the hole currently is, so shift 
				scores[j+1]=scores[j];										//shift the larger value down
				((short int*)&list[2*j])[0]=((short int*)&list[2*(j-1)])[0];//shift the larger value down
				j--;														//move the hole position down
			}
			scores[j+1]=temp;
			((short int*)&list[2*j])[0]=temp2;
			i1++;
		}
	}else{// this happens 0.85 times per call.***
		//if( !stand_pat ) // only if no TT value
			stand_pat=eval(b);// static eval. This executes 0.63 times per node.***
		#if TRAIN
		if( be==alp+1 && eval_counter<1024*1024*4 && eval_counter ) // not a PV node - do not save it.
			eval_counter--; // decrease eval counter
		#endif
		if( stand_pat>=be )// here adding this to TT is -1 ELO
			return(stand_pat); //  here returning stand pat is +3 ELO. 34% of the time=0.22 times per node.***
		// this executes 0.41 times per node/0.56 times per call.***
		alp=max(alp,stand_pat);// improve alpha.
		mc=get_all_attack_moves(b,list);// get all attack moves. This executes 0.40 times per node.*** 2.9 moves on average.
		if( mc>1 ) Qsort(b,list,mc);
		//score=0;// for testing only
		stand_pat+=100; // add margin. Here + makes cuts less likely - is conservative
	}
	
	// main loop. This executes 0.50 times per node/0.69 times per call.***
	unsigned char opp=3-player,best_move[2]={0,0};
	KBB=UINT64(1)<<kp0;
	//int cut=0,s2;
	for(legal_moves=i=0;i<mc;++i){// loop over all moves****************************************
		from=list[2*i];to=list[2*i+1];

		// See if king moves into check
		if( from==kp0 ){
			b->colorBB[player-1]^=KBB;					// update occupied BB of player. here i only need to remove the king from its current position on colorBB board.
			score=player_moved_into_check(b,to,player);
			b->colorBB[player-1]^=KBB;					// update occupied BB of player.
			if( score ) continue;						// bad move - skip it
		}else if( dir_norm[kp0][from] && moving_piece_is_pinned(b,from,to,opp) ) continue;// check for disallowed moves of pinned pieces
		legal_moves=1; // move is legal - count it as such.

		// SEE* cut
		//cut=0;
		if( !in_check ){													// only if not in check. Here i cannot restrict to opp attackes only, since here i cut even some good captures (when alp is high).
			unsigned char v=b->piece[from],w=b->piece[to];
			v=v&7; // attacker
			w=w&7; // victim
			if( piece_value_search[w]+stand_pat<=alp+piece_value_search[v]	// This is a necessary condition for cut(but not a sufficient one, so need to check SEE)
				&& !moving_piece_is_pinned(b,from,to,player)				// the move does not give check
				&& see_move(b,from,to)+stand_pat<alp
			)
				//cut=1;
				continue;
		}//else if( mc>2 && alp>-1000 && !b->piece[to] && ((short int*)best_move)[0] && see_move(b,from,to)<0 ) continue;// in check, not a capture. A wash - do not use.
			
		
		d.promotion=0;//init to Q
		do{// start of the promotion loop
			make_move(b,from,to,&d);							// make the move. This executes 0.70 times per node.***
			assert(get_TT_hash_key(b)==b->hash_key);			// make sure TT hash key is still good
			assert(get_pawn_hash_key(b)==b->pawn_hash_key);		// make sure pawn hash key is still good
			assert(bitboards_are_good(b));						// bitboards are good
			assert(get_piece_value(b)==b->piece_value);			// make sure total piece value is still good
			assert(get_mat_key(b)==b->mat_key);					// make sure material key is still good
	
			// This executes 0.41 times per node/0.56 times per call.***
			b->node_count++;	// increase node count
			score=-Qsearch(b,ply+1,-be,-alp,node_type); // call Q search *****************************************************************
			unmake_move(b,&d);
			if( b->em_break ) return(MIN_SCORE);// time out. Return current best.
			if( score>alp ){
				if( score>=be ){
					#if TRAIN==0
					add_hash(alp0,be,score,&list[2*i],0,b,ply);
					#endif
					//s2=score;
					//goto qse;
					return(score);
				}
				alp=score;
				best_move[0]=from;best_move[1]=to;
			}
		}while( (d.move_type&4) && (++d.promotion)<4 );// end of the promotion loop
	}// end of loop over moves. No beta cut-off. This happens 0.40 times per call***. That is, 58% of moves that enter main loop produce no cut-offs!
	if( in_check && !legal_moves ){// no legal moves and in check. Here adding to TT is +6 ELO.
		#if TRAIN==0
		add_hash(MIN_SCORE,MAX_SCORE,ply-10000,(unsigned char*)piece_value_search,63,b,ply);
		#endif
		return(ply-10000); // i have been mated - low (negative) score. Return 100-ply. This happens 0.001 times per call*** - almost never.
	}
	// skip stalemate logic, because here i don't get ALL moves, i only get attack moves
	#if TRAIN==0
	add_hash(alp0,be,alp,best_move,0,b,ply);
	#endif

	/*
	s2=alp;
qse:
	// log position to file
	static FILE *f=NULL;
	static unsigned int cqs=0,cqs2=0;
	if( f==NULL ){
		f=fopen("c://xde//chess//out//qs.csv","w");
		fprintf(f,"node_type,score,alp,stand_pat,i,mc,cut,SEEcut,c*2+S,margin,m0,mi,FEN\n");
	}
	if( cqs>65000 ){
		fclose(f);
		exit(666);
	}
	cqs2++;
	if( cqs2>=4000 && !in_check && mc && node_type>1 ){// skip checks
		cqs2=0;
		cqs++;
		static const char dd[]="PBBRQK"; // call N and B the same
		char sss[100];
		print_position(sss,b);
		if( s2<=alp ) i=0; // reset i to 0 if no cuts
		fprintf(f,"%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%c%c,%c%c,%s",
			node_type,s2,alp,stand_pat,i,mc,
			(s2>alp?1:0),cut,
			2*(s2>alp?1:0)+cut,
			int(min(800,alp-stand_pat)/10)*10+5,
			dd[(b->piece[list[2*0]]&7)-1],dd[(b->piece[list[2*0+1]]&7)-1],
			dd[(b->piece[list[2*i]]&7)-1],dd[(b->piece[list[2*i+1]]&7)-1],
			sss
		);
	}
	return(s2);// here i return alp and not standing pat, because standing pat is already a floor to alp!
	*/

	return(alp);// here i return alp and not standing pat, because standing pat is already a floor to alp!
}

extern UINT64 ccc[];
static void Qsort(board *b,unsigned char *list,unsigned int mc){
	//UINT64 bb,one=1;
	unsigned int i,j;
	int scores[128],temp,temp2;
	unsigned char from,to,v,w;

	//int ccc1[64];
	//memset(ccc1,0,sizeof(ccc1));

	// get mask of squares protected by pawns
	/*if( b->player==1){// white move, black pawns are protecting
		UINT64 a=b->piececolorBB[0][1];
		bb=(a<<7)&(a>>9);
	}else{// black move, white pawns are protecting
		UINT64 a=b->piececolorBB[0][0];
		bb=(a<<9)&(a>>7);
	}*/

	scores[0]=2000000000;// max value. Need this to avoid going into negative j.
	for(i=0;i<mc;++i){
		from=list[2*i];to=list[2*i+1];
		v=b->piece[from];w=b->piece[to];
		assert(v);
		v=v&7; // attacker
		w=w&7; // victim. 0=EP. Or promo.

		if( v==1 && ((to&7)==0 || (to&7)==7) ) v=7;// promotion: a=7.
		static const int Qsort_order[6][8]={34,0,0,0,0,0,7,0,    6, 2, 3, 5, 1, 4,0,0, 25,14,11,12,9, 21,8,0,  22,15,19,17,16,18,10,0, 30,24,23,20,13,28,27,0, 36,33,35,31,26,29,32};// v/a. High means sort to the front.
		//                                  ep z z z z z p* z    pp np bp rp qp kp z z pn nn bn rn qn kn pn* z pb nb bb rb qb kb pb* z pr nr br rr qr kr pr* z pq nq bq rq qq kq pq*
		assert(w<6);
		assert(v-1<7);
		temp=Qsort_order[w][v-1];
		assert(temp);
		

		//if( v>1 && (bb&(one<<to)) ) temp-=6;// piece attacks protected pawn =-X
		
		//ccc1[i]=w*8+v-1;// record the move

		// MVV/LVA
		/*int v1=piece_value_search[w];
		int v2=piece_value_search[v];
		if( v==1 && ((to&7)==0 || (to&7)==7) ){ v1+=800;v2=900; }// promotion: attack v+800 with q
		else if( w==0 ) v1=100;// EP
		temp=(v1<<4)-v2;	// most valuable victim/least valuable attacker
		*/
		

		// insertion sort
		j=i;
		temp2=((short int*)&list[2*i])[0];// save A[i], the value that will be inserted into the array on this iteration
		while( temp>scores[j] ){ //value to insert doesn't belong where the hole currently is, so shift 
			scores[j+1]=scores[j];										//shift the larger value down
			((short int*)&list[2*j])[0]=((short int*)&list[2*(j-1)])[0];//shift the larger value down
			j--;														//move the hole position down
		}
		scores[j+1]=temp;
		((short int*)&list[2*j])[0]=temp2;
	}

	// count moves
	/*for(i=0;i<mc;++i)
		for(j=0;j<mc;++j)
			ccc[ccc1[i]+ccc1[j]*48]++;*/
}