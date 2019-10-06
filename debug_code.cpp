// slow and alternative functions used for debug and not time-critical code only
#include "chess.h"

// perft TT structure
typedef struct{
	UINT64 lock;
	UINT64 nodes;
} perft_TT_type; // 16 bytes

static perft_TT_type *perft_TT;

#define perft_TT_entries (64*1024*1024) // 1 Gb. 16 bytes each: lock, count.
#define perft_TT_mask 0x000003ffffff // 26 bits
void clear_perft_TT(void){
	if( perft_TT==NULL )// init perft TT
		perft_TT=(perft_TT_type*)malloc(sizeof(perft_TT_type)*perft_TT_entries);
	if( perft_TT==NULL )
		exit(123);
	memset(perft_TT,0,sizeof(perft_TT_type)*perft_TT_entries);
}

UINT64 perft(board *b, int depth){// perft. Call with white and starting board.
	UINT64 nodes,one,BB;
	unsigned int mc,i,in_check;
	unsigned char pl,kp0,from,to,list[256];
	assert(!player_is_in_check(b,3-b->player)); // make sure there is no king capture.

	// lookup in perft TT
	if( depth>1 ){
		UINT64 kl=b->hash_key;
		i=kl&perft_TT_mask;
		kl=(kl&0xffffffffffffff00)+depth;
		if( kl==perft_TT[i].lock ){// hit
			nodes=perft_TT[i].nodes;
			return(nodes);
		}
	}

	pl=b->player; // 1/2
	kp0=b->kp[pl-1];
	one=1;
	BB=one<<kp0;
	b->node_count++; // count calls to move list
	in_check=cell_under_attack(b,kp0,pl);
	if(in_check) mc=get_out_of_check_moves(b,list,kp0,in_check-64);
	else mc=get_all_moves(b,list);
	
	// main loop
	if( depth==1 ){// last move - small loop
		nodes=mc;// count all nodes.

		// see if promotions are possible
		static const UINT64 prom_mask[2]={0x4040404040404040,0x0202020202020202};
		if( (b->piececolorBB[0][pl-1]&prom_mask[pl-1]) ){// promotions are possible
			for(i=0;i<mc;++i){// loop over all moves
				from=list[2*i];
				if( from==kp0 ){// king moved
					b->colorBB[pl-1]^=BB;					// update occupied BB of player. here i only need to remove the king from its current position on colorBB board.
					if( player_moved_into_check(b,list[2*i+1],pl) )	// moved into check - illegal move. Skip it.
						nodes--;							// bad node - subtract it.
					b->colorBB[pl-1]^=BB;					// update occupied BB of player
				}else if( ((list[2*i+1]+1)&7)<2 && (b->piece[from]&7)==1 )// promotion - count 3 other pieces 
					nodes+=3;
			}
		}else{// promotions are not possible
			for(i=0;i<mc;++i)// loop over all moves
				if( list[2*i]==kp0 ){// king moved.
					b->colorBB[pl-1]^=BB;					// update occupied BB of player. here i only need to remove the king from its current position on colorBB board.

					// Now loop over all king moves.
					list[2*mc]=64;// terminator, to avoid infinite loop
					do{
						if( player_moved_into_check(b,list[2*i+1],pl) )	// moved into check - illegal move. Skip it.
							nodes--;							// bad node - subtract it.
						i++;
					}while( list[2*i]==kp0 );
					b->colorBB[pl-1]^=BB;					// update occupied BB of player
				}
		}
	}else{ //big loop
		unmake d;
		d.promotion=0;
		nodes=0;
		for(i=0;i<mc;++i){// loop over all moves
			// See if i'm in check after the move
			from=list[2*i];
			to=list[2*i+1];
			if( from==kp0 ){
				b->colorBB[pl-1]^=BB;					// update occupied BB of player. here i only need to remove the king from its current position on colorBB board.
				unsigned int t=player_moved_into_check(b,to,pl);
				b->colorBB[pl-1]^=BB;					// update occupied BB of player.
				if( t )
					continue;
			}

			do{// beginning of the promotion loop
				make_move(b,from,to,&d);
				nodes+=perft(b,depth-1); // call perft
				unmake_move(b,&d);
			}while( (d.move_type&4) && (++d.promotion)<4 );// end of the promotion loop
			d.promotion=0;//reset
		}

		// add to perft TT - for depth 2+ only
		UINT64 kl=b->hash_key;
		i=kl&perft_TT_mask;
		if( (perft_TT[i].lock&0xff)<=depth ){// replace if same or deeper
			kl=(kl&0xffffffffffffff00)+depth;
			perft_TT[i].lock=kl;
			perft_TT[i].nodes=nodes;
		}
	}
	return(nodes);
}

static unsigned int get_piece_moves_l(board *b,unsigned char *list,unsigned char player,unsigned int cell){// get list of all available moves for a piece. Return count. Put moves on the list. Only used in debug asserts.
	// limit on piece moves. Order:+9,+7,-7,-9,+1,-1,+8,-8
	static const unsigned int z_limit[8][64]={
	{63,55,47,39,31,23,15,0,62,63,55,47,39,31,23,0,61,62,63,55,47,39,31,0,60,61,62,63,55,47,39,0,59,60,61,62,63,55,47,0,58,59,60,61,62,63,55,0,57,58,59,60,61,62,63,0,0,0,0,0,0,0,0,0},
	{0,8,16,24,32,40,48,56,0,16,24,32,40,48,56,57,0,24,32,40,48,56,57,58,0,32,40,48,56,57,58,59,0,40,48,56,57,58,59,60,0,48,56,57,58,59,60,61,0,56,57,58,59,60,61,62,0,0,0,0,0,0,0,0},
	{64,64,64,64,64,64,64,64,1,2,3,4,5,6,7,64,2,3,4,5,6,7,15,64,3,4,5,6,7,15,23,64,4,5,6,7,15,23,31,64,5,6,7,15,23,31,39,64,6,7,15,23,31,39,47,64,7,15,23,31,39,47,55,64},
	{64,64,64,64,64,64,64,64,64,0,1,2,3,4,5,6,64,8,0,1,2,3,4,5,64,16,8,0,1,2,3,4,64,24,16,8,0,1,2,3,64,32,24,16,8,0,1,2,64,40,32,24,16,8,0,1,64,48,40,32,24,16,8,0},
	{7,7,7,7,7,7,7,7,15,15,15,15,15,15,15,15,23,23,23,23,23,23,23,23,31,31,31,31,31,31,31,31,39,39,39,39,39,39,39,39,47,47,47,47,47,47,47,47,55,55,55,55,55,55,55,55,63,63,63,63,63,63,63,63},
	{0,0,0,0,0,0,0,0,8,8,8,8,8,8,8,8,16,16,16,16,16,16,16,16,24,24,24,24,24,24,24,24,32,32,32,32,32,32,32,32,40,40,40,40,40,40,40,40,48,48,48,48,48,48,48,48,56,56,56,56,56,56,56,56},
	{56,57,58,59,60,61,62,63,56,57,58,59,60,61,62,63,56,57,58,59,60,61,62,63,56,57,58,59,60,61,62,63,56,57,58,59,60,61,62,63,56,57,58,59,60,61,62,63,56,57,58,59,60,61,62,63,56,57,58,59,60,61,62,63},
	{0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7}};
	unsigned int mc=0;

	switch(b->piece[cell]&63){
	case 1:{// pawn
		// capture up
		// only if opponent and not last row and not last column
		if(player==1){// white, move -7
			if( cell>7 && ((cell+1)&7) ){
				if( (b->piece[cell-7]>>6)==2 ){
					list[0]=cell;	// from
					list[1]=cell-7;	// to
					mc++;			// count
					list+=2;		// move list forward
				}else if( b->last_move==cell-7 && b->piece[cell-8]==128+1 ){// en passant capture: opp pawn in the right place, opp pawn just moved
					list[0]=cell;	// from
					list[1]=cell-7;	// to
					mc++;			// count
					list+=2;		// move list forward
				}
			}
		}else{// black, move -9
			if( cell>7 && (cell&7) ){
				if( (b->piece[cell-9]>>6)==1 ){
					list[0]=cell;	// from
					list[1]=cell-9;	// to
					mc++;			// count
					list+=2;		// move list forward
				}else if( b->last_move==cell-9 && b->piece[cell-8]==64+1 ){// en passant capture: opp pawn in the right place, opp pawn just moved
					list[0]=cell;	// from
					list[1]=cell-9;	// to
					mc++;			// count
					list+=2;		// move list forward
				}
			}
		}

		// capture down
		// only if opponent and not last row and not last column
		if(player==1){// white, move +9
			if( cell<56 && ((cell+1)&7) ){
				if( (b->piece[cell+9]>>6)==2 ){
					list[0]=cell;	// from
					list[1]=cell+9;	// to
					mc++;			// count
					list+=2;		// move list forward
				}else if( b->last_move==cell+9 && b->piece[cell+8]==128+1 ){// en passant capture: opp pawn in the right place, opp pawn just moved
					list[0]=cell;	// from
					list[1]=cell+9;	// to
					mc++;			// count
					list+=2;		// move list forward
				}
			}
		}else{// black, move +7
			if( cell<56 && (cell&7) ){
				if( (b->piece[cell+7]>>6)==1 ){
					list[0]=cell;	// from
					list[1]=cell+7;	// to
					mc++;			// count
					list+=2;		// move list forward
				}else if( b->last_move==cell+7 && b->piece[cell+8]==64+1 ){// en passant capture: opp pawn in the right place, opp pawn just moved
					list[0]=cell;	// from
					list[1]=cell+7;	// to
					mc++;			// count
					list+=2;		// move list forward
				}
			}
		}

		// move forward 2 cells
		// only if both empty and first move
		if(player==1){// white, move +2
			if( (cell&7)==1 && !(b->piece[cell+1]+b->piece[cell+2]) ){
				list[0]=cell;	// from
				list[1]=cell+2;	// to
				mc++;			// count
				list+=2;		// move list forward
			}
		}else{// black, move -2
			if( (cell&7)==6 && !(b->piece[cell-1]+b->piece[cell-2]) ){
				list[0]=cell;	// from
				list[1]=cell-2;	// to
				mc++;			// count
				list+=2;		// move list forward
			}
		}

		// move forward
		// only if empty and not last line
		if(player==1){// white, move +1
			if( ((cell+1)&7) && !b->piece[cell+1] ){
				list[0]=cell;	// from
				list[1]=cell+1;	// to
				mc++;			// count
				list+=2;		// move list forward
			}
		}else{// black, move -1
			if( (cell&7) && !b->piece[cell-1] ){
				list[0]=cell;	// from
				list[1]=cell-1;	// to
				mc++;			// count
				list+=2;		// move list forward
			}
		}
		break;}
	case 2:{//knight
		static const unsigned int knight_moves[64][9]={
		{10,17,64,64,64,64,64,64,64},{11,18,16,64,64,64,64,64,64},{12,19,8 ,17,64,64,64,64,64},{13,20,9 ,18,64,64,64,64,64},{14,21,10,19,64,64,64,64,64},{15,22,11,20,64,64,64,64,64},{23,12,21,64,64,64,64,64,64},{13,22,64,64,64,64,64,64,64},
		{18,25,2 ,64,64,64,64,64,64},{19,26,3 ,24,64,64,64,64,64},{20,27,16,4 ,25,0 ,64,64,64},{21,28,17,5 ,26,1 ,64,64,64},{22,29,18,6 ,27,2 ,64,64,64},{23,30,19,7 ,28,3 ,64,64,64},{31,20,29,4 ,64,64,64,64,64},{21,30,5 ,64,64,64,64,64,64},
		{26,33,1 ,10,64,64,64,64,64},{27,34,2 ,11,32,0 ,64,64,64},{28,35,24,3 ,12,33,8 ,1 ,64},{29,36,25,4 ,13,34,9 ,2 ,64},{30,37,26,5 ,14,35,10,3 ,64},{31,38,27,6 ,15,36,11,4 ,64},{39,28,7 ,37,12,5 ,64,64,64},{29,38,13,6 ,64,64,64,64,64},
		{34,41,9 ,18,64,64,64,64,64},{35,42,10,19,40,8 ,64,64,64},{36,43,32,11,20,41,16,9 ,64},{37,44,33,12,21,42,17,10,64},{38,45,34,13,22,43,18,11,64},{39,46,35,14,23,44,19,12,64},{47,36,15,45,20,13,64,64,64},{37,46,21,14,64,64,64,64,64},
		{42,49,17,26,64,64,64,64,64},{43,50,18,27,48,16,64,64,64},{44,51,40,19,28,49,24,17,64},{45,52,41,20,29,50,25,18,64},{46,53,42,21,30,51,26,19,64},{47,54,43,22,31,52,27,20,64},{55,44,23,53,28,21,64,64,64},{45,54,29,22,64,64,64,64,64},
		{50,57,25,34,64,64,64,64,64},{51,58,26,35,56,24,64,64,64},{52,59,48,27,36,57,32,25,64},{53,60,49,28,37,58,33,26,64},{54,61,50,29,38,59,34,27,64},{55,62,51,30,39,60,35,28,64},{63,52,31,61,36,29,64,64,64},{53,62,37,30,64,64,64,64,64},
		{58,33,42,64,64,64,64,64,64},{59,34,43,32,64,64,64,64,64},{60,56,35,44,40,33,64,64,64},{61,57,36,45,41,34,64,64,64},{62,58,37,46,42,35,64,64,64},{63,59,38,47,43,36,64,64,64},{60,39,44,37,64,64,64,64,64},{61,45,38,64,64,64,64,64,64},
		{41,50,64,64,64,64,64,64,64},{42,51,40,64,64,64,64,64,64},{43,52,48,41,64,64,64,64,64},{44,53,49,42,64,64,64,64,64},{45,54,50,43,64,64,64,64,64},{46,55,51,44,64,64,64,64,64},{47,52,45,64,64,64,64,64,64},{53,46,64,64,64,64,64,64,64}};
		unsigned int i=0,j=knight_moves[cell][0];
		player=(player<<6);// same basis as pieces
		do{	if( !(b->piece[j]&player) ){// empty or opp
				list[0]=cell;list[1]=j;mc++;list+=2;
			}
			j=knight_moves[cell][++i];
		}while(j<64);
		break;}
	case 3:{// bishop
		unsigned int z,zl;
		unsigned char vl;
		player=(player<<6);// same basis as pieces

		// direction 1,1=+9.
		for(zl=z_limit[0][cell],z=cell;z<zl;){
			z+=9;
			vl=b->piece[z];
			if( !(vl&player) ){// empty or opp
				list[0]=cell;list[1]=z;mc++;list+=2;
			}
			if( vl )
				break;
		}
	
		// direction -1,1=+7
		for(zl=z_limit[1][cell],z=cell;z<zl;){
			z+=7;
			vl=b->piece[z];
			if( !(vl&player) ){// empty or opp
				list[0]=cell;list[1]=z;mc++;list+=2;
			}
			if( vl )
				break;
		}
	
		// direction 1,-1=-7
		for(zl=z_limit[2][cell],z=cell;z>zl;){
			z-=7;
			vl=b->piece[z];
			if( !(vl&player) ){// empty or opp
				list[0]=cell;list[1]=z;mc++;list+=2;
			}
			if( vl )
				break;
		}
	
		// direction -1,-1=-9
		for(zl=z_limit[3][cell],z=cell;z>zl;){
			z-=9;
			vl=b->piece[z];
			if( !(vl&player) ){// empty or opp
				list[0]=cell;list[1]=z;mc++;list+=2;
			}
			if( vl )
				break;
		}
		break;}
	case 4:{// rook
		unsigned int z,zl;
		unsigned char vl;
		player=(player<<6);// same basis as pieces
		
		// direction 1,0=+1
		for(zl=z_limit[4][cell],z=cell;z<zl;){
			z++;
			vl=b->piece[z];
			if( !(vl&player) ){// empty or opp
				list[0]=cell;list[1]=z;mc++;list+=2;
			}
			if( vl )
				break;
		}
	
		// direction -1,0=-1
		for(zl=z_limit[5][cell],z=cell;z>zl;){
			z--;
			vl=b->piece[z];
			if( !(vl&player) ){// empty or opp
				list[0]=cell;list[1]=z;mc++;list+=2;
			}
			if( vl )
				break;
		}

		// direction 0,1=+8
		for(zl=z_limit[6][cell],z=cell;z<zl;){
			z+=8;
			vl=b->piece[z];
			if( !(vl&player) ){// empty or opp
				list[0]=cell;list[1]=z;mc++;list+=2;
			}
			if( vl )
				break;
		}

		// direction 0,-1=-8
		for(zl=z_limit[7][cell],z=cell;z>zl;){
			z-=8;
			vl=b->piece[z];
			if( !(vl&player) ){// empty or opp
				list[0]=cell;list[1]=z;mc++;list+=2;
			}
			if( vl )
				break;
		}
		break;}
	case 5:{// queen
		unsigned int z,zl;
		unsigned char vl;
		player=(player<<6);// same basis as pieces

		// direction 1,1=+9.
		for(zl=z_limit[0][cell],z=cell;z<zl;){
			z+=9;
			vl=b->piece[z];
			if( !(vl&player) ){// empty or opp
				list[0]=cell;list[1]=z;mc++;list+=2;
			}
			if( vl )
				break;
		}
	
		// direction -1,1=+7
		for(zl=z_limit[1][cell],z=cell;z<zl;){
			z+=7;
			vl=b->piece[z];
			if( !(vl&player) ){// empty or opp
				list[0]=cell;list[1]=z;mc++;list+=2;
			}
			if( vl )
				break;
		}
	
		// direction 1,-1=-7
		for(zl=z_limit[2][cell],z=cell;z>zl;){
			z-=7;
			vl=b->piece[z];
			if( !(vl&player) ){// empty or opp
				list[0]=cell;list[1]=z;mc++;list+=2;
			}
			if( vl )
				break;
		}
	
		// direction -1,-1=-9
		for(zl=z_limit[3][cell],z=cell;z>zl;){
			z-=9;
			vl=b->piece[z];
			if( !(vl&player) ){// empty or opp
				list[0]=cell;list[1]=z;mc++;list+=2;
			}
			if( vl )
				break;
		}

		// direction 1,0=+1
		for(zl=z_limit[4][cell],z=cell;z<zl;){
			z++;
			vl=b->piece[z];
			if( !(vl&player) ){// empty or opp
				list[0]=cell;list[1]=z;mc++;list+=2;
			}
			if( vl )
				break;
		}
	
		// direction -1,0=-1
		for(zl=z_limit[5][cell],z=cell;z>zl;){
			z--;
			vl=b->piece[z];
			if( !(vl&player) ){// empty or opp
				list[0]=cell;list[1]=z;mc++;list+=2;
			}
			if( vl )
				break;
		}

		// direction 0,1=+8
		for(zl=z_limit[6][cell],z=cell;z<zl;){
			z+=8;
			vl=b->piece[z];
			if( !(vl&player) ){// empty or opp
				list[0]=cell;list[1]=z;mc++;list+=2;
			}
			if( vl )
				break;
		}

		// direction 0,-1=-8
		for(zl=z_limit[7][cell],z=cell;z>zl;){
			z-=8;
			vl=b->piece[z];
			if( !(vl&player) ){// empty or opp
				list[0]=cell;list[1]=z;mc++;list+=2;
			}
			if( vl )
				break;
		}
		break;}
	case 6:{// king
		static const unsigned int king_moves[64][9]={
		{1 ,8 ,9 ,64,64,64,64,64,64},{0 ,2 ,8 ,9 ,10,64,64,64,64},{1 ,3 ,9 ,10,11,64,64,64,64},{2 ,4 ,10,11,12,64,64,64,64},{3 ,5 ,11,12,13,64,64,64,64},{4 ,6 ,12,13,14,64,64,64,64},{5 ,7 ,13,14,15,64,64,64,64},{6 ,14,15,64,64,64,64,64,64},
		{0 ,1 ,9 ,16,17,64,64,64,64},{0 ,1 ,2 ,8 ,10,16,17,18,64},{1 ,2 ,3 ,9 ,11,17,18,19,64},{2 ,3 ,4 ,10,12,18,19,20,64},{3 ,4 ,5 ,11,13,19,20,21,64},{4 ,5 ,6 ,12,14,20,21,22,64},{5 ,6 ,7 ,13,15,21,22,23,64},{6 ,7 ,14,22,23,64,64,64,64},
		{8 ,9 ,17,24,25,64,64,64,64},{8 ,9 ,10,16,18,24,25,26,64},{9 ,10,11,17,19,25,26,27,64},{10,11,12,18,20,26,27,28,64},{11,12,13,19,21,27,28,29,64},{12,13,14,20,22,28,29,30,64},{13,14,15,21,23,29,30,31,64},{14,15,22,30,31,64,64,64,64},
		{16,17,25,32,33,64,64,64,64},{16,17,18,24,26,32,33,34,64},{17,18,19,25,27,33,34,35,64},{18,19,20,26,28,34,35,36,64},{19,20,21,27,29,35,36,37,64},{20,21,22,28,30,36,37,38,64},{21,22,23,29,31,37,38,39,64},{22,23,30,38,39,64,64,64,64},
		{24,25,33,40,41,64,64,64,64},{24,25,26,32,34,40,41,42,64},{25,26,27,33,35,41,42,43,64},{26,27,28,34,36,42,43,44,64},{27,28,29,35,37,43,44,45,64},{28,29,30,36,38,44,45,46,64},{29,30,31,37,39,45,46,47,64},{30,31,38,46,47,64,64,64,64},
		{32,33,41,48,49,64,64,64,64},{32,33,34,40,42,48,49,50,64},{33,34,35,41,43,49,50,51,64},{34,35,36,42,44,50,51,52,64},{35,36,37,43,45,51,52,53,64},{36,37,38,44,46,52,53,54,64},{37,38,39,45,47,53,54,55,64},{38,39,46,54,55,64,64,64,64},
		{40,41,49,56,57,64,64,64,64},{40,41,42,48,50,56,57,58,64},{41,42,43,49,51,57,58,59,64},{42,43,44,50,52,58,59,60,64},{43,44,45,51,53,59,60,61,64},{44,45,46,52,54,60,61,62,64},{45,46,47,53,55,61,62,63,64},{46,47,54,62,63,64,64,64,64},
		{48,49,57,64,64,64,64,64,64},{48,49,50,56,58,64,64,64,64},{49,50,51,57,59,64,64,64,64},{50,51,52,58,60,64,64,64,64},{51,52,53,59,61,64,64,64,64},{52,53,54,60,62,64,64,64,64},{53,54,55,61,63,64,64,64,64},{54,55,62,64,64,64,64,64,64}};
		unsigned int i=0,j=king_moves[cell][0];
		player=(player<<6);// same basis as pieces
		do{	if( !(b->piece[j]&player) ){// empty or opp
				list[0]=cell;list[1]=j;mc++;list+=2;
			}
			j=king_moves[cell][++i];
		}while(j<64);

		// check for castling
		// king is not in check, it does not pass through a square that is under attack by an enemy piece, and does not end up in check.
		if( player==64 ){//white
			if( (b->castle&1) && (b->piece[8]+b->piece[16]+b->piece[24])==0 && !cell_under_attack(b,16,1) && !cell_under_attack(b,24,1) && !cell_under_attack(b,32,1) ){// up
				list[0]=32;list[1]=16;mc++;list+=2;
			}
			if( (b->castle&2) && (b->piece[40]+b->piece[48])==0 && !cell_under_attack(b,32,1) && !cell_under_attack(b,40,1) && !cell_under_attack(b,48,1) ){// down
				list[0]=32;list[1]=48;mc++;list+=2;
			}
		}else{// black
			if( (b->castle&4) && (b->piece[15]+b->piece[23]+b->piece[31])==0 && !cell_under_attack(b,23,2) && !cell_under_attack(b,31,2) && !cell_under_attack(b,39,2) ){// up
				list[0]=39;list[1]=23;mc++;list+=2;
			}
			if( (b->castle&8) && (b->piece[47]+b->piece[55])==0 && !cell_under_attack(b,39,2) && !cell_under_attack(b,47,2) && !cell_under_attack(b,55,2) ){// down
				list[0]=39;list[1]=55;mc++;list+=2;
			}
		}
		break;}
	}
	return(mc);
}

unsigned int get_piece_moves(board *b,unsigned char *list,unsigned char player,unsigned int cell){// get list of all available moves for a piece. Return count. Put moves on the list.
	if( (b->piece[cell]>>6)!=player )// this also skips empty cells
		return(0);
	else
		return(get_piece_moves_l(b,list,player,cell));
}

unsigned int get_legal_moves(board *b,unsigned char *list){
	unsigned int i,mc1,move_count=0,mc=0;
	unsigned char list2[256];
	
	// get complete list of moves
	for(i=0;i<64;++i){
		if( (b->piece[i]>>6)==b->player ){// only for the right player
			mc1=get_piece_moves_l(b,list2+2*move_count,b->player,i);
			move_count+=mc1;// count
		}
	}

	// make all the moves, exclude the ones that lead to check
	for(i=0;i<move_count;++i){
		board b_l=*b;	// copy board
		unmake d;
		d.promotion=0;
		make_move(&b_l,list2[2*i],list2[2*i+1],&d);

		// See if i'm in check after the move
		if( cell_under_attack(&b_l,b_l.kp[b->player-1],b->player) )// still in check - illegal move. Skip it.
			continue;

		// good move. Put it on the list.
		list[2*mc]=list2[2*i];
		list[2*mc+1]=list2[2*i+1];
		mc++;
	}

	return(mc);
}

unsigned int move_list_is_good(board *b,unsigned char *list,unsigned int mc){// verify that all legal moves are on this list, and only king moves lead to check.
	unsigned int i,j,mc1,move_count=0;
	unsigned char list2[256];
	board b_s=*b;

	// make sure all move are from not empty cells
	for(i=0;i<mc;++i)
		if( !b->piece[list[2*i]] ){
			char sss[100];
			print_position(sss,b);
			return(0);// problem-
		}

	// get complete list of moves
	for(i=0;i<64;++i){
		if( (b->piece[i]>>6)==b->player ){// only for the right player
			mc1=get_piece_moves_l(b,list2+2*move_count,b->player,i);
			move_count+=mc1;// count
		}
	}

	// make all the moves, exclude the ones that lead to check
	for(i=0;i<move_count;++i){
		board b_l=*b;	// copy board
		unmake d;
		d.promotion=0;
		make_move(&b_l,list2[2*i],list2[2*i+1],&d);

		// See if i'm in check after the move
		if( dist[b_l.kp[0]][b_l.kp[1]]==1 || cell_under_attack(&b_l,b_l.kp[b->player-1],b->player) )// still in check - illegal move. Skip it.
			continue;

		// good move. See if it is on the list.
		unsigned int good_move=0;
		for(j=0;j<mc;++j){
			if( list[2*j]==list2[2*i] && list[2*j+1]==list2[2*i+1]){
				good_move=1;
				break;
			}
		}
		if(!good_move){
			*b=b_s;
			#if ALLOW_LOG
			char sss[200];print_position(sss,b);
			fprintf(f_log,"move_list_is_not_good v1:%s\n runtime mc=%d vs debug mc=%d\n Debug move %d to %d is not found on runtime list\n",sss,mc,move_count,list2[2*i],list2[2*i+1]);
			fclose(f_log);f_log=NULL;// close and reset
			#endif
			return(0);// move not found - list is no good.
		}
	}

	// look at all the incoming non-king moves, make sure they are on good list.
	for(i=0;i<mc;++i){
		if(list[2*i]==b->kp[b->player-1])// king moved
			continue;

		// good move. See if it is on the list.
		unsigned int good_move=0;
		for(j=0;j<move_count;++j){
			if( list[2*i]==list2[2*j] && list[2*i+1]==list2[2*j+1]){
				good_move=1;
				break;
			}
		}
		if(!good_move){
			*b=b_s;
			#if ALLOW_LOG
			char sss[200];print_position(sss,b);
			fprintf(f_log,"move_list_is_not_good v2:%s\n runtime mc=%d vs debug mc=%d\n",sss,mc,move_count);
			fclose(f_log);f_log=NULL;// close and reset
			#endif
			return(0);// move not found - list is no good.
		}
	}

	*b=b_s;
	return(1);// no problems found - move list is good.
}

unsigned int find_all_get_out_of_check_moves_slow(board *b,unsigned char *list){
	unsigned char list2[256];
	unsigned int mc=get_all_moves(b,list2);
	unsigned int gm=0;
	
	for(unsigned int i=0;i<mc;++i){
		board b_l=*b;
		unmake d;
		d.promotion=0;
		make_move(&b_l,list2[2*i],list2[2*i+1],&d);

		// check if my king is under attack
		if( !cell_under_attack(&b_l,b_l.kp[b->player-1],b->player) ){// not in check anymore - record the move
			list[0]=list2[2*i];
			list[1]=list2[2*i+1];
			gm++;
			list+=2;
		}
	}
	return(gm);
}

unsigned int player_is_in_check(board *b,unsigned int player){
	return( cell_under_attack(b,b->kp[player-1],player) );
}

void check_move(unsigned char *m,unsigned char *list,unsigned int mc){// check that move m is on the list
	for(unsigned int i=0;i<mc;++i)
		if(list[2*i]==m[0] && list[2*i+1]==m[1] )
			return;
	char scm[200];
	print_position(scm,&b_m);
	assert(false);
	exit(0);
}

void decode_move(unsigned char *m,char *d,board *b){// take move from "d", put it in "m"
	unsigned int mc,mc2,mc3,offset,i;
	unsigned char list[256],list2[256],list3[256];

	// get all legal moves
	mc=get_legal_moves(b,list);

	// see if castling
	if( d[0]=='O' ){
		m[0]=b->kp[b->player-1];// king moves
		if(d[4]=='O')// long castle
			m[1]=m[0]-16;
		else// short castle
			m[1]=m[0]+16;
		check_move(m,list,mc);// check that this move is on the list.
		assert(m[0]!=m[1]);
		return;
	}

	// find "to"
	offset=0;
	while ( d[offset]!='=' && d[offset]!=';' && d[offset]!=' ' ) offset++;// find "=" or ";" or " ", back up 1. Or 2, if "+"
	offset--;
	if( d[offset]=='+') offset--;
	m[1]=d[offset]-'1';
	m[1]+=(d[offset-1]-'a')*8;
	assert(m[1]<64);

	// get all moves leading to "to"
	for(mc2=i=0;i<mc;++i){
		if( list[2*i+1]==m[1] ){
			list2[2*mc2]=list[2*i];
			list2[2*mc2+1]=list[2*i+1];
			mc2++;
		}
	}
	if( mc2==1 ){// only one move to "to". Select it and return.
		m[0]=list2[0];
		assert(m[0]!=m[1]);
		return;
	}
	#if NDEBUG
	#else 
	if( !mc2 ){
		char sss[100];
		print_position(sss,b);
		assert(mc2>0);
	}
	#endif
	

	// find "from"
	// first see if notation is like "a1"
	// only if there are 2 chars before "to" move
	if( offset-1==2 ){
		if(d[0]>='a' && d[0]<='h' && d[1]>='1' && d[1]<='9'){// it is. Wrap it up now.
			m[0]=d[1]-'1';
			m[0]+=(d[0]-'a')*8;
			assert(m[0]!=m[1]);
			return;
		}
	}
	unsigned char from_piece=0;
	unsigned int file=8;// init to invalid
	switch(d[0]){
		case 'N':from_piece=2;break;
		case 'B':from_piece=3;break;
		case 'R':from_piece=4;break;
		case 'Q':from_piece=5;break;
		case 'K':from_piece=6;break;
		case 'a':from_piece=1;file=0;break;
		case 'b':from_piece=1;file=1;break;
		case 'c':from_piece=1;file=2;break;
		case 'd':from_piece=1;file=3;break;
		case 'e':from_piece=1;file=4;break;
		case 'f':from_piece=1;file=5;break;
		case 'g':from_piece=1;file=6;break;
		case 'h':from_piece=1;file=7;break;
		default:assert(false);// this should never happen
	}
	from_piece+=1<<(5+b->player);// add player bit

	// find all "from_piece" on list2
	list3[0]=list3[1]=0;// init to nothing
	for(mc3=i=0;i<mc2;++i){
		if( b->piece[list2[2*i]]==from_piece ){
			list3[2*mc3]=list2[2*i];
			list3[2*mc3+1]=list2[2*i+1];
			mc3++;
		}
	}
	if( mc3==1 ){// only one "from piece"
		m[0]=list3[0];
		assert(m[0]!=m[1]);
		return;
	}
	if(mc3!=2)
		m[0]=5;
	assert(mc3==2);

	// multiple from pieces. File is coded by second letter of input. Or by first letter, if pawn moves
	if( file==8 ){
		if( d[1]>='a' && d[1]<='h' ){// file
			file=d[1]-'a';

			if( list3[0]/8==file){
				m[0]=list3[0];
				assert(m[0]!=m[1]);
				return;
			}
		}else{// rank
			file=d[1]-'1';// rank

			if( list3[0]%8==file){
				m[0]=list3[0];
				assert(m[0]!=m[1]);
				return;
			}
		}
	}else{// file is already defined, use it
		if( list3[0]/8==file){
			m[0]=list3[0];
			assert(m[0]!=m[1]);
			return;
		}
	}
	m[0]=list3[2];
	assert(m[0]!=m[1]);
	return;
}