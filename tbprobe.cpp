// Define DECOMP64 when compiling for a 64-bit platform.
#define DECOMP64
#include "chess.h"
#include <intrin.h>
#if USE_EGTB
#include "tbcore.c"

// Given a position with 6 or fewer pieces, produce a text string
// of the form KQPvKRP, where "KQP" represents the white pieces if
// mirror == 0 and the black pieces if mirror == 1.
static void prt_str(board *b, char *str, int mirror){
  *str++ ='K';//always
  if( b->piececolorBB[4][mirror] )// Q
	  *str++ ='Q';
  if( blsr64l(b->piececolorBB[4][mirror]) )// second Q
	  *str++ ='Q';
  if( popcnt64l(b->piececolorBB[4][mirror])>2 )// third Q
	  *str++ ='Q';
  if( popcnt64l(b->piececolorBB[4][mirror])>3 )// 4th Q
	  *str++ ='Q';
  if( b->piececolorBB[3][mirror] )// R
	  *str++ ='R';
  if( blsr64l(b->piececolorBB[3][mirror]) )// second R
	  *str++ ='R';
  if( popcnt64l(b->piececolorBB[3][mirror])>2 )// third R
	  *str++ ='R';
  if( popcnt64l(b->piececolorBB[3][mirror])>3 )// 4th R
	  *str++ ='R';
  if( b->piececolorBB[2][mirror] )// B
	  *str++ ='B';
  if( blsr64l(b->piececolorBB[2][mirror]) )// second B
	  *str++ ='B';
  if( popcnt64l(b->piececolorBB[2][mirror])>2 )// third B
	  *str++ ='B';
  if( popcnt64l(b->piececolorBB[2][mirror])>3 )// 4th B
	  *str++ ='B';
  if( b->piececolorBB[1][mirror] )// N
	  *str++ ='N';
  if( blsr64l(b->piececolorBB[1][mirror]) )// second N
	  *str++ ='N';
  if( popcnt64l(b->piececolorBB[1][mirror])>2 )// third N
	  *str++ ='N';
   if( popcnt64l(b->piececolorBB[1][mirror])>3 )// 4th N
	  *str++ ='N';
  UINT64 m=b->piececolorBB[0][mirror];
  while( m ){// P
	  *str++ ='P';
	  m=blsr64l(m);
  }

  *str++ = 'v';
  mirror^=1;// change color
  *str++ ='K';//always
  if( b->piececolorBB[4][mirror] )// Q
	  *str++ ='Q';
  if( blsr64l(b->piececolorBB[4][mirror]) )// second Q
	  *str++ ='Q';
  if( popcnt64l(b->piececolorBB[4][mirror])>2 )// third Q
	  *str++ ='Q';
  if( popcnt64l(b->piececolorBB[4][mirror])>3 )// 4th Q
	  *str++ ='Q';
  if( b->piececolorBB[3][mirror] )// R
	  *str++ ='R';
  if( blsr64l(b->piececolorBB[3][mirror]) )// second R
	  *str++ ='R';
  if( popcnt64l(b->piececolorBB[3][mirror])>2 )// third R
	  *str++ ='R';
  if( popcnt64l(b->piececolorBB[3][mirror])>3 )// 4th R
	  *str++ ='R';
  if( b->piececolorBB[2][mirror] )// B
	  *str++ ='B';
  if( blsr64l(b->piececolorBB[2][mirror]) )// second B
	  *str++ ='B';
  if( popcnt64l(b->piececolorBB[2][mirror])>2 )// third B
	  *str++ ='B';
  if( popcnt64l(b->piececolorBB[2][mirror])>3 )// 4th B
	  *str++ ='B';
  if( b->piececolorBB[1][mirror] )// N
	  *str++ ='N';
  if( blsr64l(b->piececolorBB[1][mirror]) )// second N
	  *str++ ='N';
  if( popcnt64l(b->piececolorBB[1][mirror])>2 )// third N
	  *str++ ='N';
  if( popcnt64l(b->piececolorBB[1][mirror])>3 )// 4th N
	  *str++ ='N';
  m=b->piececolorBB[0][mirror];
  while( m ){// P
	  *str++ ='P';
	  m=blsr64l(m);
  }

  *str++ = 0;// terminator
}

// Given a position, produce a 64-bit material signature key.
// If the engine supports such a key, it should equal the engine's key.
static uint64 calc_key(board *b, int mirror){
	UINT64 key=0,bb;
	unsigned int pt,i;

	for(pt=1;pt<=5;pt++){// no kings
		bb=b->piececolorBB[pt-1][mirror];
		for(i=0;bb;i++,bb=blsr64l(bb))
			key^=zorb[pt][1][i+9];
	}
	mirror^=1;
	for(pt=1;pt<=5;pt++){// no kings
		bb=b->piececolorBB[pt-1][mirror];
		for(i=0;bb;i++,bb=blsr64l(bb))
			key^=zorb[pt][0][i+9];
	}

	return(key);
}

// Produce a 64-bit material key corresponding to the material combination
// defined by pcs[16], where pcs[1], ..., pcs[6] is the number of white
// pawns, ..., kings and pcs[9], ..., pcs[14] is the number of black
// pawns, ..., kings.
static uint64 calc_key_from_pcs(int *pcs, int mirror){
	uint64 key=0;
	int color,i;
	unsigned int pt;

  color=mirror<<3;
  for(pt=1;pt<=5;pt++)// no kings
    for(i=0;i<pcs[color+pt];i++)
		key^=zorb[pt][1][i+9];
  color^=8;
  for(pt=1;pt<=5;pt++)// no kings
    for(i=0;i<pcs[color+pt];i++)
      key^=zorb[pt][0][i+9];

  return(key);
}

bool is_little_endian() {
  union {
    int i;
    char c[sizeof(int)];
  } x;
  x.i = 1;
  return x.c[0] == 1;
}

static ubyte decompress_pairs(struct PairsData *d, uint64 idx){
  static const bool isLittleEndian = is_little_endian();
  return isLittleEndian ? decompress_pairs<true >(d, idx)
                        : decompress_pairs<false>(d, idx);
}

static const unsigned int this_to_me[15]={0,0,1*2,2*2,3*2,4*2,5*2,0,0,0+1,1*2+1,2*2+1,3*2+1,4*2+1,5*2+1};// index: current form, 1-14. Out: index into "b"
static int probe_wdl_table(board *b,int *success){
	struct TBEntry *ptr;
	struct TBHashEntry *ptr2;
	uint64 idx,key;
	int i,p[TBPIECES];
	ubyte res;

	// Obtain the position's material signature key.
	key=calc_key(b,0);// for white

	// Test for KvK.
	if(!key) return 0;

	ptr2=TB_hash[key>>(64-TBHASHBITS)];// 10 bits
	for(i=0;i<HSHMAX;i++)// 5
		if(ptr2[i].key==key) break;
	if(i==HSHMAX){
		*success=0;
		return 0;
	}

	ptr=ptr2[i].ptr;
	if(!ptr->ready){
		// release current entry?
		LOCK(TB_mutex);
		if(!ptr->ready){
			char str[16];
			prt_str(b,str,ptr->key!=key);
			if(!init_table_wdl(ptr,str)){
				ptr->data=NULL;
				ptr2[i].key=0ULL;
				*success=0;
				UNLOCK(TB_mutex);
				return 0;
			}
			ptr->ready=1;
		}
		UNLOCK(TB_mutex);
	}

	int bside,mirror,cmirror;
	if(!ptr->symmetric){
		if(key!=ptr->key){
			cmirror=8;
			mirror=0x38;
			bside=(b->player==1);
		}else{
			cmirror=mirror=0;
			bside=!(b->player==1);
		}
	}else{
		cmirror=b->player==1?0:8;
		mirror=b->player==1?0:0x38;
		bside=0;
	}

	// p[i] is to contain the square 0-63 (A1-H8) for a piece of type
	// pc[i] ^ cmirror, where 1 = white pawn, ..., 14 = black king.
	// Pieces of the same type are guaranteed to be consecutive.
	unsigned long bit;
	UINT64 bb;
	if(!ptr->has_pawns){// no pawns
		struct TBEntry_piece *entry=(struct TBEntry_piece *)ptr;
		ubyte *pc=entry->pieces[bside];
		for(i=0;i<entry->num;){
			bb=b->piececolorBB[0][this_to_me[pc[i]^cmirror]];
			do{
				GET_BIT(bb)
				bit=(bit>>3)+((bit&7)<<3);// transpose it.
				p[i++]=bit;
			}while(bb);
		}
		idx=encode_piece(entry,entry->norm[bside],p,entry->factor[bside]);
		res=decompress_pairs(entry->precomp[bside],idx);
	}else{// pawns
		struct TBEntry_pawn *entry=(struct TBEntry_pawn *)ptr;
		int k=entry->file[0].pieces[0][0]^cmirror;
		bb=b->piececolorBB[0][this_to_me[k]];
		i=0;
		do{
			GET_BIT(bb)
			bit=(bit>>3)+((bit&7)<<3);// transpose it.
			p[i++]=bit^mirror;
		}while(bb);
		int f=pawn_file(entry,p);
		ubyte *pc=entry->file[f].pieces[bside];
		for(;i<entry->num;){
			bb=b->piececolorBB[0][this_to_me[pc[i]^cmirror]];
			do{
				GET_BIT(bb)
				bit=(bit>>3)+((bit&7)<<3);// transpose it.
				p[i++]=bit^mirror;
			}while(bb);
		}
		idx=encode_pawn(entry,entry->file[f].norm[bside],p,entry->file[f].factor[bside]);
		res=decompress_pairs(entry->file[f].precomp[bside],idx);
	}
	return ((int)res)-2;
}


static int probe_ab(board *b,int alpha,int beta,int *success){
	int v;
	unsigned int in_check,mc,i;
	unsigned char list[128];

	// Generate (at least) all legal non-ep captures including (under)promotion captures. Exclude noncapture promotions!
	// It is OK to generate more, as long as they are filtered out below.
	in_check=cell_under_attack(b,b->kp[b->player-1],b->player); // from this point on in_check is defined.
	if(in_check)
		mc=get_out_of_check_moves(b,list,b->kp[b->player-1],in_check-64);
	else
		mc=get_all_attack_moves(b,list);// captures only
	unsigned char player=b->player,kp0=b->kp[player-1];
	for(i=0;i<mc;++i){
		unmake d;
		d.promotion=0;
		do{// start of the promotion loop
		make_move(b,list[2*i],list[2*i+1],&d);

		// See if i'm in check after the move
		if( list[2*i]==kp0 && player_moved_into_check(b,list[2*i+1],player) ){// king moved, into check - illegal move. Skip it.
			unmake_move(b,&d);
			continue;
		}

		// check for disallowed moves of pinned pieces
		if( dir_norm[kp0][list[2*i]] && cell_under_attack(b,kp0,player) ){
			unmake_move(b,&d);
			continue;
		}

		// here only keep captures, excluding EP and promotion captures, including underpromotion captures
		if( !(d.move_type&2) || (d.move_type&8) ){// skip EP captures and non-captures
			unmake_move(b,&d);
			continue;
		}
		
		v=-probe_ab(b,-beta,-alpha,success);
		unmake_move(b,&d);

		if( *success==0 ) return 0;
		if( v>alpha ){
			if( v>=beta ){
				*success=2;
				return v;
			}
			alpha=v;
		}

		// add underpromotion captures here.
		}while( (d.move_type&4) && (++d.promotion)<4 );// end of the promotion loop
	}

	v=probe_wdl_table(b,success);
	if( *success==0 ) return 0;
	if( alpha>=v ){
		*success=1+(alpha>0);
		return alpha;
	}else{
		*success=1;
		return v;
	}
}

// Probe the WDL table for a particular position.
// If *success != 0, the probe was successful.
// The return value is from the point of view of the side to move:
// -2 : loss
// -1 : loss, but draw under 50-move rule
//  0 : draw
//  1 : win, but draw under 50-move rule
//  2 : win
int probe_wdl(board *b,int *success){
	int v;

	*success=1;
	v=probe_ab(b,-2,2,success);

	// If en passant is not possible, we are done.
	if( b->last_move==INVALID_LAST_MOVE )
		return v;
	if( !(*success) ) return 0;

	// Now handle en passant.
	// Generate (at least) all legal en passant captures.
	*success=0;
	return 0;// for now
}

static int probe_dtz_table(board *b,int wdl,int *success){
	struct TBEntry *ptr;
	uint64 idx;
	int i,res,p[TBPIECES];
	*success=1;

	// Obtain the position's material signature key.
	UINT64 key=calc_key(b,0);// for white

	LOCK(TB_mutex);// lock at the very beginning, unlock on return. This stops crashes in multithreaded runs.
	if(DTZ_table[0].key1!=key && DTZ_table[0].key2!=key){
		for(i=1;i<DTZ_ENTRIES;i++)
			if( DTZ_table[i].key1==key ) break;
		if(i<DTZ_ENTRIES){
			struct DTZTableEntry table_entry=DTZ_table[i];
			for (;i>0;i--)
				DTZ_table[i]=DTZ_table[i-1];
			DTZ_table[0]=table_entry;
		}else{
			struct TBHashEntry *ptr2=TB_hash[key>>(64-TBHASHBITS)];
			for(i=0;i<HSHMAX;i++)
				if( ptr2[i].key==key ) break;
			if( i==HSHMAX ){
				*success=0;
				UNLOCK(TB_mutex);
				return 0;
			}
			ptr=ptr2[i].ptr;
			char str[16];
			int mirror=(ptr->key!=key);
			prt_str(b,str,mirror);
			UINT64 k1=calc_key(b,mirror);
			UINT64 k2=calc_key(b,!mirror);
			if(DTZ_table[DTZ_ENTRIES-1].entry)
				free_dtz_entry(DTZ_table[DTZ_ENTRIES-1].entry);
			for(i=DTZ_ENTRIES-1;i>0;i--)
				DTZ_table[i]=DTZ_table[i-1];
			// put a lock around this. Maybe that will stop it from crashing
			LOCK(TB_mutex);
			load_dtz_table(str,k1,k2);
			UNLOCK(TB_mutex);
		}
	}

	ptr=DTZ_table[0].entry;
	if(!ptr){
		*success=0;
		UNLOCK(TB_mutex);
		return 0;
	}

	int bside,mirror,cmirror;
	if(!ptr->symmetric){
		if(key!=ptr->key){
			cmirror=8;
			mirror=0x38;
			bside=(b->player==1);
		}else{
			cmirror=mirror=0;
			bside=!(b->player==1);
		}
	}else{
		cmirror=b->player==1?0:8;
		mirror=b->player==1?0:0x38;
		bside=0;
	}

	unsigned long bit;
	UINT64 bb;
	if(!ptr->has_pawns){// no pawns
		struct DTZEntry_piece *entry=(struct DTZEntry_piece *)ptr;
		if((entry->flags&1)!=bside && !entry->symmetric){
			*success=-1;
			UNLOCK(TB_mutex);
			return 0;
		}
		ubyte *pc=entry->pieces;
		for(i=0;i<entry->num;){
			bb=b->piececolorBB[0][this_to_me[pc[i]^cmirror]];
			do{
				GET_BIT(bb)
				bit=(bit>>3)+((bit&7)<<3);// transpose it.
				p[i++]=bit;
			}while(bb);
		}
		idx=encode_piece((struct TBEntry_piece *)entry,entry->norm,p,entry->factor);
		res=decompress_pairs(entry->precomp,idx);

		if( entry->flags&2 )
			res=entry->map[entry->map_idx[wdl_to_map[wdl+2]]+res];

		if ( !(entry->flags&pa_flags[wdl+2]) && !(wdl&1) )
			res*=2;
	}else{// pawns
		struct DTZEntry_pawn *entry=(struct DTZEntry_pawn *)ptr;
		int k=entry->file[0].pieces[0]^cmirror;
		bb=b->piececolorBB[0][this_to_me[k]];
		i=0;
		do{
			GET_BIT(bb)
			bit=(bit>>3)+((bit&7)<<3);// transpose it.
			p[i++]=bit^mirror;
		}while(bb);
		int f=pawn_file((struct TBEntry_pawn *)entry,p);
		if( (entry->flags[f]&1)!=bside ){
			*success=-1;
			UNLOCK(TB_mutex);
			return 0;
		}
		ubyte *pc=entry->file[f].pieces;
		for(;i<entry->num;){
			bb=b->piececolorBB[0][this_to_me[pc[i]^cmirror]];
			do{
				GET_BIT(bb)
				bit=(bit>>3)+((bit&7)<<3);// transpose it.
				p[i++]=bit^mirror;
			}while(bb);
		}
		idx=encode_pawn((struct TBEntry_pawn *)entry,entry->file[f].norm,p,entry->file[f].factor);
		res=decompress_pairs(entry->file[f].precomp,idx);

		if( entry->flags[f]&2 )
			res=entry->map[entry->map_idx[f][wdl_to_map[wdl+2]]+res];

		if( !(entry->flags[f]&pa_flags[wdl+2]) && !(wdl&1) )
			res*=2;
	}
	UNLOCK(TB_mutex);
	return res;
}

// This routine treats a position with en passant captures as one without.
int probe_dtz(board*,int*);
static int probe_dtz_no_ep(board *b,int *success){
	int wdl,dtz;
	unsigned int i,mc,in_check;
	unsigned char list[256],player,kp0;

	wdl=probe_ab(b,-2,2,success);
	if( *success==0 ) return 0;
	if( wdl==0 ) return 0;
	if( *success==2 ) return wdl==2?1:101;

	if( wdl>0 ){
		// Generate at least all legal non-capturing pawn moves
		// including non-capturing promotions.
		in_check=cell_under_attack(b,b->kp[b->player-1],b->player); // from this point on in_check is defined.
		if(in_check)
			mc=get_out_of_check_moves(b,list,b->kp[b->player-1],in_check-64);
		else
			mc=get_all_moves(b,list);// all moves, not just captures
		player=b->player,kp0=b->kp[player-1];
		for(i=0;i<mc;++i){
			unmake d;
			d.promotion=0;
			do{// start of the promotions loop
			make_move(b,list[2*i],list[2*i+1],&d);

			// See if i'm in check after the move
			if( list[2*i]==kp0 && player_moved_into_check(b,list[2*i+1],player) ){// king moved, into check - illegal move. Skip it.
				unmake_move(b,&d);
				continue;
			}

			if( !(d.move_type==0 && (b->piece[d.to]&7)==1 ) && !(d.move_type==4) ){// keep non-capturing pawn moves and non-capturing promotions
				unmake_move(b,&d);
				continue;
			}

			int v=-probe_ab(b,-2,-wdl+1,success);
			unmake_move(b,&d);
			if( *success==0 ) return 0;
			if( v==wdl ) return v==2?1:101;
			}while( (d.move_type&4) && (++d.promotion)<4 );// end of the promotion loop
		}
	}

	dtz=1+probe_dtz_table(b,wdl,success);
	if( *success>=0 ){
		if( wdl&1 ) dtz+=100;
		return wdl>=0?dtz:-dtz;
	}

	if( wdl>0 ){
		int best=0xffff;
	    for(i=0;i<mc;++i){
			unmake d;
			d.promotion=0;
			do{// start of the promotions loop
			make_move(b,list[2*i],list[2*i+1],&d);

			// See if i'm in check after the move
			if( list[2*i]==kp0 && player_moved_into_check(b,list[2*i+1],player) ){// king moved, into check - illegal move. Skip it.
				unmake_move(b,&d);
				continue;
			}

			if( !(d.move_type==0 && (b->piece[d.to]&7)!=1 ) && !(d.move_type==4) ){// keep non-capturing NON-pawn moves and non-capturing promotions. Change made on 2/15/2016
				unmake_move(b,&d);
				continue;
			}

			int v=-probe_dtz(b,success);
			unmake_move(b,&d);
			if( *success==0 ) return 0;
			if( v>0 && v+1<best )
				best=v+1;
			}while( (d.move_type&4) && (++d.promotion)<4 );// end of the promotion loop
		}
		return best;
	}else{
		int best=-1;
		in_check=cell_under_attack(b,b->kp[b->player-1],b->player); // from this point on in_check is defined.
		if(in_check)
			mc=get_out_of_check_moves(b,list,b->kp[b->player-1],in_check-64);
		else
			mc=get_all_moves(b,list);// all moves, not just captures
		player=b->player,kp0=b->kp[player-1];
		for(i=0;i<mc;++i){
			unmake d;
			int v;
			d.promotion=0;
			make_move(b,list[2*i],list[2*i+1],&d);

			// See if i'm in check after the move
			if( list[2*i]==kp0 && player_moved_into_check(b,list[2*i+1],player) ){// king moved, into check - illegal move. Skip it.
				unmake_move(b,&d);
				continue;
			}

			if( b->halfmoveclock==0 ){// i think this is correct, but i'm not sure.
				if( wdl==-2 )
					v=-1;
				else{
					v=probe_ab(b,1,2,success);
					v=(v==2)?0:-101;
				}
			}else
				v=-probe_dtz(b,success)-1;

			unmake_move(b,&d);
			if( *success==0 ) return 0;
			if( v<best ) best=v;
		}
		return best;
	}
}

// Probe the DTZ table for a particular position.
// If *success != 0, the probe was successful.
// The return value is from the point of view of the side to move:
//         n < -100 : 
// -100 <= n < -1   : loss in n ply (assuming 50-move counter == 0)
//         0	    : draw
//     1 < n <= 100 : win in n ply (assuming 50-move counter == 0)
//   100 < n        : win, but draw under 50-move rule
//
// The return value n can be off by 1: a return value -n can mean a loss
// in n+1 ply and a return value +n can mean a win in n+1 ply. This
// cannot happen for tables with positions exactly on the "edge" of
// the 50-move rule.
//
// This implies that if dtz > 0 is returned, the position is certainly
// a win if dtz + 50-move-counter <= 99. Care must be taken that the engine
// picks moves that preserve dtz + 50-move-counter <= 99.
//
// If n = 100 immediately after a capture or pawn move, then the position
// is also certainly a win, and during the whole phase until the next
// capture or pawn move, the inequality to be preserved is
// dtz + 50-movecounter <= 100.
//
// In short, if a move is available resulting in dtz + 50-move-counter <= 99,
// then do not accept moves leading to dtz + 50-move-counter == 100.
int probe_dtz(board *b,int *success){
	*success=1;
	int v=probe_dtz_no_ep(b,success);

	// If en passant is not possible, we are done.
	if( b->last_move==INVALID_LAST_MOVE )
		return v;
	if( !(*success) ) return 0;

	// Now handle en passant.
	*success=0;
	return 0;// for now
}
#endif