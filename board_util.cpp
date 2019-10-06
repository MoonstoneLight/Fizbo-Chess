// board utilities
#include "chess.h"
#include <intrin.h>
#define USE_PREFETCH 1

extern UINT64 *eh; // eval hash pointer
extern unsigned int pv10[];

UINT64 knight_masks[64];					// knight masks - all directions.
UINT64 bishop_masks[64];					// bishop masks - 4 directions, all cells excluding the one where bishop is.
UINT64 rook_masks[64];						// rook masks - 4 directions, all cells excluding the one where rook is.
UINT64 king_masks[64];						// king masks - all directions.
UINT64 ray_segment[64][64];					// segment of a ray from X to Y. Zero if X and Y are not on a straight line. 32 Kb in size.
UINT64 dir_mask[5][64];						// mask of allowed move directions. 0-all, 1-1, 2-7, 3-8, 4-9.
static unsigned int castle_mask[64];		// move to/from, 4 bit masks. And with board to update, for both "from" and "to" cells.
UINT64 zorb_castle[16];						// castle zorb
unsigned char dir_norm[64][64];				// move to/from. Normalized direction - 1,7,8,9.
unsigned char dist[64][64];					// move to/from. Distance, 0 to 7. Max over x/y.

const UINT64 pawn_attacks[2][64]={{			// cell attacked by white/black pawns
	0x0000000000000000,0x0000000000000100,0x0000000000000200,0x0000000000000400,0x0000000000000800,0x0000000000001000,0x0000000000002000,0x0000000000004000,
	0x0000000000000000,0x0000000000010001,0x0000000000020002,0x0000000000040004,0x0000000000080008,0x0000000000100010,0x0000000000200020,0x0000000000400040,
	0x0000000000000000,0x0000000001000100,0x0000000002000200,0x0000000004000400,0x0000000008000800,0x0000000010001000,0x0000000020002000,0x0000000040004000,
	0x0000000000000000,0x0000000100010000,0x0000000200020000,0x0000000400040000,0x0000000800080000,0x0000001000100000,0x0000002000200000,0x0000004000400000,
	0x0000000000000000,0x0000010001000000,0x0000020002000000,0x0000040004000000,0x0000080008000000,0x0000100010000000,0x0000200020000000,0x0000400040000000,
	0x0000000000000000,0x0001000100000000,0x0002000200000000,0x0004000400000000,0x0008000800000000,0x0010001000000000,0x0020002000000000,0x0040004000000000,
	0x0000000000000000,0x0100010000000000,0x0200020000000000,0x0400040000000000,0x0800080000000000,0x1000100000000000,0x2000200000000000,0x4000400000000000,
	0x0000000000000000,0x0001000000000000,0x0002000000000000,0x0004000000000000,0x0008000000000000,0x0010000000000000,0x0020000000000000,0x0040000000000000},
	{0x0000000000000200,0x0000000000000400,0x0000000000000800,0x0000000000001000,0x0000000000002000,0x0000000000004000,0x0000000000008000,0x0000000000000000,
	0x0000000000020002,0x0000000000040004,0x0000000000080008,0x0000000000100010,0x0000000000200020,0x0000000000400040,0x0000000000800080,0x0000000000000000,
	0x0000000002000200,0x0000000004000400,0x0000000008000800,0x0000000010001000,0x0000000020002000,0x0000000040004000,0x0000000080008000,0x0000000000000000,
	0x0000000200020000,0x0000000400040000,0x0000000800080000,0x0000001000100000,0x0000002000200000,0x0000004000400000,0x0000008000800000,0x0000000000000000,
	0x0000020002000000,0x0000040004000000,0x0000080008000000,0x0000100010000000,0x0000200020000000,0x0000400040000000,0x0000800080000000,0x0000000000000000,
	0x0002000200000000,0x0004000400000000,0x0008000800000000,0x0010001000000000,0x0020002000000000,0x0040004000000000,0x0080008000000000,0x0000000000000000,
	0x0200020000000000,0x0400040000000000,0x0800080000000000,0x1000100000000000,0x2000200000000000,0x4000400000000000,0x8000800000000000,0x0000000000000000,
	0x0002000000000000,0x0004000000000000,0x0008000000000000,0x0010000000000000,0x0020000000000000,0x0040000000000000,0x0080000000000000,0x0000000000000000}};
static const unsigned int dir_trans[]={0,1,0,0,0,0,0,2,3,4}; // translate direction from 1/7/8/9 to 1/2/3/4

void init_moves(void){
	unsigned int i,j,k;
	
	// init castle masks
	for(i=0;i<64;++i) castle_mask[i]=1+2+4+8;// init to "castling rights not lost"
	castle_mask[0]&=14;		// rook at 0
	castle_mask[56]&=13;	// rook at 56
	castle_mask[7]&=11;		// rook at 7
	castle_mask[63]&=7;		// rook at 63
	castle_mask[32]&=12;	// king at 32, both ways
	castle_mask[39]&=3;		// king at 39, both ways

	// init castle zorb
	for(i=0;i<16;++i){
		zorb_castle[i]=0;
		if( i&1 ) zorb_castle[i]^=zorb[0][0][0];
		if( i&2 ) zorb_castle[i]^=zorb[0][0][8];
		if( i&4 ) zorb_castle[i]^=zorb[0][0][16];
		if( i&8 ) zorb_castle[i]^=zorb[0][0][24];
	}

	// init direction normalization table
	// init distance table
	memset(dir_norm,0,sizeof(dir_norm));// init to "not valid direction"=0
	for(i=0;i<64;++i)
		for(j=0;j<64;++j){
			dist[i][j]=max(abs(((int)i>>3)-((int)j>>3)),abs(((int)i&7)-((int)j&7)));// distance
			if( i==j )// skip same to same attack - it is not real.
				continue;
			int x1=i%8,x2=j%8,y1=i/8,y2=j/8;
			if(x1==x2)
				dir_norm[i][j]=8;
			else if(y1==y2)
				dir_norm[i][j]=1;
			else if( (y1-y2)==(x1-x2) )
				dir_norm[i][j]=9;
			else if( (y1-y2)==(x2-x1) )
				dir_norm[i][j]=7;
		}

	// init attack bitmasks
	UINT64 one=1,m;
	int dx0[8]={1,-1,1,-1,1,-1,0,0},dy0[8]={1,1,-1,-1,0,0,1,-1};// ray directions: Bishop, then Rook
	int dx1[8]={1,1,-1,-1,2,2,-2,-2},dy1[8]={2,-2,2,-2,1,-1,1,-1};// knight directions: +9,+7,-7,-9,+1,-1,+8,-8
	for(i=0;i<64;++i){// loop over cell
		rook_masks[i]=bishop_masks[i]=knight_masks[i]=king_masks[i]=0;
		for(j=0;j<8;++j){// loop over directions
			int x=i&7,y=i>>3,dx=dx0[j],dy=dy0[j];
			x+=dx;y+=dy;// step 1

			// get king masks
			if( x>=0 && x<8 && y>=0 && y<8 )
				king_masks[i]|=(one<<(x+y*8));

			// get rook and bishop masks
			m=0;
			while( x>=0 && x<8 && y>=0 && y<8 ){// steps in direction dir
				m|=(one<<(x+y*8));
				x+=dx;y+=dy;// next step
			}
			if(j<4)
				bishop_masks[i]|=m;
			else
				rook_masks[i]|=m;

			// get knight masks
			x=(i&7)+dx1[j];y=(i>>3)+dy1[j];
			if( x>=0 && x<8 && y>=0 && y<8 )
				knight_masks[i]|=(one<<(x+y*8));

		}
	}

	// ray_segment, from i to j excluding i/j.
	for(i=0;i<64;++i){// from
		for(j=0;j<64;++j){// to
			ray_segment[i][j]=0;
			unsigned char d=dir_norm[i][j];// Normalized direction - 1,7,8,9.
			if( d ){// on a straight line.
				if(j>i){
					for(k=i+d;k<j;k+=d)
						ray_segment[i][j]|=(one<<k);
				}else{
					for(k=j+d;k<i;k+=d)
						ray_segment[i][j]|=(one<<k);
				}
			}
		}
	}

	// mask of allowed move directions. 0-all, 1-1, 2-7, 3-8, 4-9.
	for(i=0;i<64;++i){// from
		dir_mask[0][i]=dir_mask[1][i]=dir_mask[2][i]=dir_mask[3][i]=dir_mask[4][i]=0xffffffffffffffff;// init to "all"
		for(j=0;j<64;++j){// to
			k=dir_norm[i][j];// direction
			if( k==0 ) dir_mask[0][i]^=one<<j;
			if( k!=1 ) dir_mask[1][i]^=one<<j;
			if( k!=7 ) dir_mask[2][i]^=one<<j;
			if( k!=8 ) dir_mask[3][i]^=one<<j;
			if( k!=9 ) dir_mask[4][i]^=one<<j;
		}
	}

	//test - create some masks
	/*{FILE *f=fopen("c://xde//chess//out//v.csv","w");
	for(i=0;i<64;++i){
		UINT64 m=0;
		int x0=i&7,y0=i/8,x,y,z;
		for( int dx=-1;dx<2;dx++){
			for( int dy=-1;dy<2;dy++){
				int dz=dx+dy*8;
				if( dz==0 )
					continue;

				// move in direction dz
				x=x0+dx;y=y0+dy;z=i+dz;
				//while( x<8 && y<8 && x>=0 && y>=0 ){
				while( x+dx<8 && y+dy<8 && x+dx>=0 && y+dy>=0 ){
					m=m|(UINT64(1)<<z);
					x+=dx;y+=dy;z+=dz;
				}
			}
		}
		// write to file
		fprintf(f,"%I64x\n",m);
	}
	fclose(f);
	exit(0);}*/
}

static inline void set_piece(board *b,unsigned int cell,unsigned int piece){// set piece at position "cell"
	b->piece[cell]=piece;
}

static inline unsigned int get_knight_moves(board *b,unsigned char *list,unsigned char player,unsigned int cell,UINT64 allowed_mask){
	UINT64 attack_mask=knight_masks[cell]&allowed_mask;
	unsigned int mc=0;
	while( attack_mask ){unsigned long bit;
		GET_BIT(attack_mask)
		list[0]=cell;list[1]=(unsigned char)bit;mc++;list+=2;
	}
	return(mc);
}

static inline unsigned int get_bishop_moves(board *b,unsigned char *list,unsigned char player,unsigned int cell,UINT64 o,UINT64 allowed_mask){
	UINT64 attack_mask=attacks_bb_B(cell,o)&allowed_mask;
	unsigned int mc=0;
	while( attack_mask ){unsigned long bit;
		GET_BIT(attack_mask)
		list[0]=cell;list[1]=(unsigned char)bit;mc++;list+=2;
	}
	return(mc);
}

static inline unsigned int get_rook_moves(board *b,unsigned char *list,unsigned char player,unsigned int cell,UINT64 o,UINT64 allowed_mask){
	UINT64 attack_mask=attacks_bb_R(cell,o)&allowed_mask;
	unsigned int mc=0;
	while( attack_mask ){unsigned long bit;
		GET_BIT(attack_mask)
		list[0]=cell;list[1]=(unsigned char)bit;mc++;list+=2;
	}
	return(mc);
}

static inline unsigned int get_queen_moves(board *b,unsigned char *list,unsigned char player,unsigned int cell,UINT64 o,UINT64 allowed_mask){
	UINT64 attack_mask=(attacks_bb_R(cell,o)|attacks_bb_B(cell,o))&allowed_mask;
	unsigned int mc=0;
	while( attack_mask ){unsigned long bit;
		GET_BIT(attack_mask)
		list[0]=cell;list[1]=(unsigned char)bit;mc++;list+=2;
	}
	return(mc);
}

static inline unsigned int get_king_moves_l(board *b,unsigned char *list,unsigned char player,unsigned int cell){
	UINT64 p_a,attack_mask;

	// eliminate cells attacked by opp pawns
	if( (player&2) )
		p_a=(b->piececolorBB[0][0]<<9) | (b->piececolorBB[0][0]>>7);// white pawns attack
	else
		p_a=(b->piececolorBB[0][1]<<7) | (b->piececolorBB[0][1]>>9);// black pawns attack
	
	p_a|=b->colorBB[player-1];// eliminate cells occupied by player (keep opponent - those are attacks)
	p_a|=king_masks[b->kp[2-player]];// eliminate cells attacked by opp king
	attack_mask=king_masks[cell]&(~p_a);

	unsigned long bit;
	unsigned int mc=0;
	while( attack_mask ){
		GET_BIT(attack_mask)
		list[0]=cell;list[1]=(unsigned char)bit;mc++;list+=2;
	}

	if( b->castle ){// skip castle logic if all flags are off
		// check for castling
		// king is not in check, it does not pass through a square that is under attack by an enemy piece, and does not end up in check.
		if( player==1 ){//white
			if( (b->castle&1) && (b->piece[8]+b->piece[16]+b->piece[24])==0 && !cell_under_attack(b,16,1) && !cell_under_attack(b,24,1) && !cell_under_attack(b,32,1) && dist[b->kp[1]][16]>1 && dist[b->kp[1]][24]>1 && dist[b->kp[1]][32]>1 ){// up
				list[0]=32;list[1]=16;mc++;list+=2;
			}
			if( (b->castle&2) && (b->piece[40]+b->piece[48])==0 && !cell_under_attack(b,32,1) && !cell_under_attack(b,40,1) && !cell_under_attack(b,48,1) && dist[b->kp[1]][48]>1 && dist[b->kp[1]][40]>1 && dist[b->kp[1]][32]>1 ){// down
				list[0]=32;list[1]=48;mc++;list+=2;
			}
		}else{// black
			if( (b->castle&4) && (b->piece[15]+b->piece[23]+b->piece[31])==0 && !cell_under_attack(b,23,2) && !cell_under_attack(b,31,2) && !cell_under_attack(b,39,2) && dist[b->kp[0]][23]>1 && dist[b->kp[0]][31]>1 && dist[b->kp[0]][39]>1 ){// up
				list[0]=39;list[1]=23;mc++;list+=2;
			}
			if( (b->castle&8) && (b->piece[47]+b->piece[55])==0 && !cell_under_attack(b,39,2) && !cell_under_attack(b,47,2) && !cell_under_attack(b,55,2) && dist[b->kp[0]][55]>1 && dist[b->kp[0]][47]>1 && dist[b->kp[0]][39]>1 ){// down
				list[0]=39;list[1]=55;mc++;list+=2;
			}
		}
	}
	return(mc);
}

static inline unsigned int get_king_moves_l_no_captures(board *b,unsigned char *list,unsigned char player,unsigned int cell){
	UINT64 p_a,attack_mask;

	// eliminate cells attacked by opp pawns
	if( (player&2) )
		p_a=(b->piececolorBB[0][0]<<9) | (b->piececolorBB[0][0]>>7);// white pawns attack
	else
		p_a=(b->piececolorBB[0][1]<<7) | (b->piececolorBB[0][1]>>9);// black pawns attack
	
	p_a|=b->colorBB[player-1];// eliminate cells occupied by player (keep opponent - those are attacks)
	p_a|=king_masks[b->kp[2-player]];// eliminate cells attacked by opp king
	attack_mask=king_masks[cell]&(~p_a)&(~b->colorBB[2-player]); // eliminate cells occupied by opponent

	unsigned long bit;
	unsigned int mc=0;
	while( attack_mask ){
		GET_BIT(attack_mask)
		list[0]=cell;list[1]=(unsigned char)bit;mc++;list+=2;
	}

	if( b->castle ){// skip castle logic if all flags are off
		// check for castling
		// king is not in check, it does not pass through a square that is under attack by an enemy piece, and does not end up in check.
		if( player==1 ){//white
			if( (b->castle&1) && (b->piece[8]+b->piece[16]+b->piece[24])==0 && !cell_under_attack(b,16,1) && !cell_under_attack(b,24,1) && !cell_under_attack(b,32,1) && dist[b->kp[1]][16]>1 && dist[b->kp[1]][24]>1 && dist[b->kp[1]][32]>1 ){// up
				list[0]=32;list[1]=16;mc++;list+=2;
			}
			if( (b->castle&2) && (b->piece[40]+b->piece[48])==0 && !cell_under_attack(b,32,1) && !cell_under_attack(b,40,1) && !cell_under_attack(b,48,1) && dist[b->kp[1]][48]>1 && dist[b->kp[1]][40]>1 && dist[b->kp[1]][32]>1 ){// down
				list[0]=32;list[1]=48;mc++;list+=2;
			}
		}else{// black
			if( (b->castle&4) && (b->piece[15]+b->piece[23]+b->piece[31])==0 && !cell_under_attack(b,23,2) && !cell_under_attack(b,31,2) && !cell_under_attack(b,39,2) && dist[b->kp[0]][23]>1 && dist[b->kp[0]][31]>1 && dist[b->kp[0]][39]>1 ){// up
				list[0]=39;list[1]=23;mc++;list+=2;
			}
			if( (b->castle&8) && (b->piece[47]+b->piece[55])==0 && !cell_under_attack(b,39,2) && !cell_under_attack(b,47,2) && !cell_under_attack(b,55,2) && dist[b->kp[0]][55]>1 && dist[b->kp[0]][47]>1 && dist[b->kp[0]][39]>1 ){// down
				list[0]=39;list[1]=55;mc++;list+=2;
			}
		}
	}
	return(mc);
}

static unsigned int get_ep_moves_l(board *b,unsigned char *list,unsigned char player){
	UINT64 one=1,m1,m2;
	unsigned int move_count=0,c_l;
	// get all en passant captures
	// Here discovered check can be caused by movement of either pawn, as well as only by movement of both -> use full pin logic. Slow but reliable.
	if( player==1 ){// white
		if( b->last_move>=9 && b->piece[b->last_move-9]==65 ){// from LM-9. Don't forget pin check.
			m1=(one<<b->last_move)|(one<<(b->last_move-9));
			m2=(one<<(b->last_move-1));
			b->piececolorBB[0][0]^=m1;b->piececolorBB[0][1]^=m2;
			b->colorBB[0]^=m1;b->colorBB[1]^=m2;// update occupied bitboards
			c_l=cell_under_attack(b,b->kp[0],1); // see if it puts king in check
			b->colorBB[0]^=m1;b->colorBB[1]^=m2;// update occupied bitboards
			b->piececolorBB[0][0]^=m1;b->piececolorBB[0][1]^=m2;
			if( !c_l ){
				list[0]=b->last_move-9;list[1]=b->last_move;move_count++;list+=2;
			}
		}
		if( b->last_move<=55 && b->piece[b->last_move+7]==65 ){// from LM+7. Don't forget pin check
			m1=(one<<b->last_move)|(one<<(b->last_move+7));
			m2=(one<<(b->last_move-1));
			b->piececolorBB[0][0]^=m1;b->piececolorBB[0][1]^=m2;
			b->colorBB[0]^=m1;b->colorBB[1]^=m2;// update occupied bitboards
			c_l=cell_under_attack(b,b->kp[0],1); // see if it puts king in check
			b->colorBB[0]^=m1;b->colorBB[1]^=m2;// update occupied bitboards
			b->piececolorBB[0][0]^=m1;b->piececolorBB[0][1]^=m2;
			if( !c_l ){
				list[0]=b->last_move+7;list[1]=b->last_move;move_count++;list+=2;
			}
		}
	}else{//black
		if( b->last_move>=7 && b->piece[b->last_move-7]==129 ){// from LM-7. Don't forget pin check
			m1=(one<<b->last_move)|(one<<(b->last_move-7));
			m2=(one<<(b->last_move+1));
			b->piececolorBB[0][1]^=m1;b->piececolorBB[0][0]^=m2;
			b->colorBB[1]^=m1;b->colorBB[0]^=m2;// update occupied bitboards
			c_l=cell_under_attack(b,b->kp[1],2); // see if it puts king in check
			b->colorBB[1]^=m1;b->colorBB[0]^=m2;// update occupied bitboards
			b->piececolorBB[0][1]^=m1;b->piececolorBB[0][0]^=m2;
			if( !c_l ){
				list[0]=b->last_move-7;list[1]=b->last_move;move_count++;list+=2;
			}
		}
		if( b->last_move<=55 && b->piece[b->last_move+9]==129 ){// from LM+9. Don't forget pin check
			m1=(one<<b->last_move)|(one<<(b->last_move+9));
			m2=(one<<(b->last_move+1));
			b->piececolorBB[0][1]^=m1;b->piececolorBB[0][0]^=m2;
			b->colorBB[1]^=m1;b->colorBB[0]^=m2;// update occupied bitboards
			c_l=cell_under_attack(b,b->kp[1],2); // see if it puts king in check
			b->colorBB[1]^=m1;b->colorBB[0]^=m2;// update occupied bitboards
			b->piececolorBB[0][1]^=m1;b->piececolorBB[0][0]^=m2;
			if( !c_l ){
				list[0]=b->last_move+9;list[1]=b->last_move;move_count++;list+=2;
			}
		}
	}
	return(move_count);
}

unsigned int get_all_moves_new_part1(board *b,unsigned char *list,UINT64 *pinBB_r){// get list of all available moves - captures and promotions only. Return count. Put moves on the list.
	UINT64 move_count=0,j,mc1,pinBB=0,pinned_pawns=0,one=1,a,a2,o=b->colorBB[0]|b->colorBB[1],allowed_mask=b->colorBB[2-b->player];// only opp cell are allowed
	unsigned long bit;
	unsigned int king_position=b->kp[b->player-1],p_c=0,p_cell[8],p_d[8],player=b->player;
	unsigned char p_d2[64];// for each cell, direction of allowed moves: 1,7,8,9. 0 if all allowed.

	// find all absolutely pinned squares. Don't forget the pinned directions.
	// a is mask of all opp sliders on king attack lines, excluding the ones blocked by OPP
	a=(b->piececolorBB[2][2-player]|b->piececolorBB[4][2-player])&attacks_bb_B(king_position,b->colorBB[2-player]);// exclude OPP blockers
	a|=(b->piececolorBB[3][2-player]|b->piececolorBB[4][2-player])&attacks_bb_R(king_position,b->colorBB[2-player]);// exclude OPP blockers
	memset(p_d2,0,64);// init to "all allowed"
	while( a ){
		GET_BIT(a)
		a2=ray_segment[king_position][bit]&b->colorBB[player-1];
		if( popcnt64l(a2)==1 ){// exactly 1 player blocker. Use popcnt to avoid branch mispredicts.
			BSF64l(&bit,a2);				// now bit is position of the pinned piece
			pinBB|=a2;								// add it to bitboard. This adds a whole ray, not just one bit. But it does not matter. Used for pawns and knights only.
			unsigned int dirnorm=dir_norm[bit][king_position];
			if( !(b->piece[bit]&6) ){               // pawn only
				if( dirnorm!=8 ){                   // exclude pawns allowed in direction 8 only - that is not a valid pawn direction!
					p_cell[p_c]=bit;				// pinned pawn position
					p_d[p_c]=dirnorm;				// pinned pawn direction
					p_c++;							// count it
					pinned_pawns|=a2;				// mask of pinned pawns
				}
			}else
				p_d2[bit]=dir_trans[dirnorm];				// pinned piece direction2=0,1,2,3,4. By cell. Used for sliders only.
		}
	}
	*pinBB_r=pinBB; // return BB of pins


	// get all en passant captures
	if( b->last_move!=INVALID_LAST_MOVE ){// valid last move. This guarantees that at least 1 opp pawn is in correct place. They can only be disallowed by pins.
		mc1=get_ep_moves_l(b,list,player);
		move_count+=mc1;// count
		list+=(mc1<<1);// move up the list pointer
	}


	// get queen moves. Pin has to be considered.
	a=b->piececolorBB[4][player-1];
	while( a ){
		GET_BIT(a)
		mc1=get_queen_moves(b,list,player,bit,o,allowed_mask&dir_mask[p_d2[bit]][bit]);
		move_count+=mc1;// count
		list+=(mc1<<1);// move up the list pointer
	}


	// get rook moves. Pin has to be considered.
	a=b->piececolorBB[3][player-1];
	while( a ){
		GET_BIT(a)
		mc1=get_rook_moves(b,list,player,bit,o,allowed_mask&dir_mask[p_d2[bit]][bit]);
		move_count+=mc1;// count
		list+=(mc1<<1);// move up the list pointer
	}


	// get bishop moves. Pin has to be considered.
	a=b->piececolorBB[2][player-1];
	while( a ){
		GET_BIT(a)
		mc1=get_bishop_moves(b,list,player,bit,o,allowed_mask&dir_mask[p_d2[bit]][bit]);
		move_count+=mc1;// count
		list+=(mc1<<1);// move up the list pointer
	}


	// get knight moves. Pin has to be considered.
	a=b->piececolorBB[1][player-1]&(~pinBB);// BB of unpinned knights
	while( a ){
		GET_BIT(a)
		mc1=get_knight_moves(b,list,player,bit,allowed_mask);
		move_count+=mc1;// count
		list+=(mc1<<1);// move up the list pointer
	}
	

	// get pawn moves. Pin has to be considered.
	if( player==1 ){// for white pawns***************************************************************
		// first, unpinned pawns
		a=b->piececolorBB[0][0]&(~pinBB);// BB of unpinned pawns
		// move forward one square. Only select rank 7 - promotions
		a2=((a&0x4040404040404040)<<1)&(~o);// moving to empty squares
		// serialize
		while( a2 ){
			GET_BIT(a2)
			list[0]=(unsigned char)bit-1;list[1]=(unsigned char)bit;move_count++;list+=2;
		}
		// attack to +9.
		a2=(a<<9)&b->colorBB[1];// moving to OPP squares
		// serialize
		while( a2 ){
			GET_BIT(a2)
			list[0]=(unsigned char)bit-9;list[1]=(unsigned char)bit;move_count++;list+=2;
		}
		// attack to -7.
		a2=(a>>7)&b->colorBB[1];// moving to OPP squares
		// serialize
		while( a2 ){
			GET_BIT(a2)
			list[0]=(unsigned char)bit+7;list[1]=(unsigned char)bit;move_count++;list+=2;
		}

		// second, pinned pawns
		a=b->piececolorBB[0][0]&pinned_pawns;// BB of pinned pawns
		if( a){
		// move forward one square. Only select rank 7 - promotions
		a2=((a&0x4040404040404040)<<1)&(~o);// moving to empty squares
		// serialize
		while( a2 ){
			GET_BIT(a2)

			// exclude disallowed moves
			unsigned int pin=0;
			for(j=0;j<p_c;++j)// find pinned data
				if( bit-1==p_cell[j] ){// here pawn moves from bit-1
					pin=1;
					break;
				}
			if( !pin || p_d[j]==1 ){// allowed. Or not pinned. Record it
				list[0]=(unsigned char)bit-1;list[1]=(unsigned char)bit;move_count++;list+=2;
			}
		}
		// attack to +9.
		a2=(a<<9)&b->colorBB[1];// moving to OPP squares
		// serialize
		while( a2 ){
			GET_BIT(a2)

			// exclude disallowed moves
			unsigned int pin=0;
			for(j=0;j<p_c;++j)// find pinned data
				if( bit-9==p_cell[j] ){// here pawn moves from bit-9
					pin=1;
					break;
				}
			if( !pin || p_d[j]==9 ){// allowed. Or not pinned. Record it
				list[0]=(unsigned char)bit-9;list[1]=(unsigned char)bit;move_count++;list+=2;
			}
		}
		// attack to -7.
		a2=(a>>7)&b->colorBB[1];// moving to OPP squares
		// serialize
		while( a2 ){
			GET_BIT(a2)

			// exclude disallowed moves
			unsigned int pin=0;
			for(j=0;j<p_c;++j)// find pinned data
				if( bit+7==p_cell[j] ){// here pawn moves from bit+7
					pin=1;
					break;
				}
			if( !pin || p_d[j]==7 ){// allowed. Or not pinned. Record it
				list[0]=(unsigned char)bit+7;list[1]=(unsigned char)bit;move_count++;list+=2;
			}
		}
		}
	}else{// for black pawns*****************************************************************************
		// first, unpinned pawns
		a=b->piececolorBB[0][1]&(~pinBB);// BB of unpinned pawns
		// move forward one square. Onlt select rank 2 - promotions.
		a2=((a&0x0202020202020202)>>1)&(~o);// moving to empty squares
		// serialize
		while( a2 ){
			GET_BIT(a2)
			list[0]=(unsigned char)bit+1;list[1]=(unsigned char)bit;move_count++;list+=2;
		}
		// attack to +7.
		a2=(a<<7)&b->colorBB[0];// moving to OPP squares
		// serialize
		while( a2 ){
			GET_BIT(a2)
			list[0]=(unsigned char)bit-7;list[1]=(unsigned char)bit;move_count++;list+=2;
		}
		// attack to -9.
		a2=(a>>9)&b->colorBB[0];// moving to OPP squares
		// serialize
		while( a2 ){
			GET_BIT(a2)
			list[0]=(unsigned char)bit+9;list[1]=(unsigned char)bit;move_count++;list+=2;
		}

		// second, pinned pawns
		a=b->piececolorBB[0][1]&pinned_pawns;// BB of pinned pawns
		if( a ){
		// move forward one square. Onlt select rank 2 - promotions.
		a2=((a&0x0202020202020202)>>1)&(~o);// moving to empty squares
		// serialize
		while( a2 ){
			GET_BIT(a2)

			// exclude disallowed moves
			unsigned int pin=0;
			for(j=0;j<p_c;++j)// find pinned data
				if( bit+1==p_cell[j] ){// here pawn moves from bit+1
					pin=1;
					break;
				}
			if( !pin || p_d[j]==1 ){// allowed. Or not pinned. Record it
				list[0]=(unsigned char)bit+1;list[1]=(unsigned char)bit;move_count++;list+=2;
			}
		}
		// attack to +7.
		a2=(a<<7)&b->colorBB[0];// moving to OPP squares
		// serialize
		while( a2 ){
			GET_BIT(a2)

			// exclude disallowed moves
			unsigned int pin=0;
			for(j=0;j<p_c;++j)// find pinned data
				if( bit-7==p_cell[j] ){// here pawn moves from bit-7
					pin=1;
					break;
				}
			if( !pin || p_d[j]==7 ){// allowed. Or not pinned. Record it
				list[0]=(unsigned char)bit-7;list[1]=(unsigned char)bit;move_count++;list+=2;
			}
		}
		// attack to -9.
		a2=(a>>9)&b->colorBB[0];// moving to OPP squares
		// serialize
		while( a2 ){
			GET_BIT(a2)

			// exclude disallowed moves
			unsigned int pin=0;
			for(j=0;j<p_c;++j)// find pinned data
				if( bit+9==p_cell[j] ){// here pawn moves from bit+9
					pin=1;
					break;
				}
			if( !pin || p_d[j]==9 ){// allowed. Or not pinned. Record it
				list[0]=(unsigned char)bit+9;list[1]=(unsigned char)bit;move_count++;list+=2;
			}
		}
		}
	}


	// get king moves.
	a=king_masks[b->kp[player-1]]&allowed_mask;// attacks only
	if( a ) {
		// eliminate cells attacked by opp pawns
		if( (player&2) )
			a2=(b->piececolorBB[0][0]<<9) | (b->piececolorBB[0][0]>>7);// white pawns attack
		else
			a2=(b->piececolorBB[0][1]<<7) | (b->piececolorBB[0][1]>>9);// black pawns attack
		a2|=king_masks[b->kp[2-player]];// eliminate cells attacked by opp king
		a&=(~a2);
		while( a ){
			GET_BIT(a)
			list[0]=b->kp[player-1];list[1]=(unsigned char)bit;move_count++;list+=2;
		}
	}
	return(unsigned int(move_count));// return move count
}

unsigned int get_all_moves_new_part2(board *b,unsigned char *list,UINT64 pinBB){// get list of all available moves - excluding captures and promotions. Return count. Put moves on the list.
	UINT64 move_count=0,mc1,pinned_pawns=0,one=1,a,a2,o=b->colorBB[0]|b->colorBB[1],allowed_mask=~o; // only empties are allowed
	unsigned long bit;
	unsigned int king_position=b->kp[b->player-1],j,p_c=0,p_cell[8],p_d[8],player=b->player;
	unsigned char p_d2[64];// for each cell, direction of allowed moves: 1,7,8,9. 0 if all allowed.

	// find all absolutely pinned squares. Don't forget the pinned directions.
	// a is mask of all opp sliders on king attack lines, excluding the ones blocked by OPP
	a=pinBB;
	memset(p_d2,0,64);// init to "all allowed"
	while( a ){
		GET_BIT(a)
		unsigned int dirnorm=dir_norm[bit][king_position];
		if( !(b->piece[bit]&6) ){               // pawn only
			if( dirnorm!=8 ){                   // exclude pawns allowed in direction 8 only - that is not a valid pawn direction!
				p_cell[p_c]=bit;				// pinned pawn position
				p_d[p_c]=dirnorm;				// pinned pawn direction
				p_c++;							// count it
				pinned_pawns|=(UINT64(1)<<bit);	// mask of pinned pawns
			}
		}else
			p_d2[bit]=dir_trans[dirnorm];				// pinned piece direction2=0,1,2,3,4. By cell. Used for sliders only.
	}


	// get queen moves. Pin has to be considered.
	a=b->piececolorBB[4][player-1];
	while( a ){
		GET_BIT(a)
		mc1=get_queen_moves(b,list,player,bit,o,allowed_mask&dir_mask[p_d2[bit]][bit]);
		move_count+=mc1;// count
		list+=(mc1<<1);// move up the list pointer
	}


	// get rook moves. Pin has to be considered.
	a=b->piececolorBB[3][player-1];
	while( a ){
		GET_BIT(a)
		mc1=get_rook_moves(b,list,player,bit,o,allowed_mask&dir_mask[p_d2[bit]][bit]);
		move_count+=mc1;// count
		list+=(mc1<<1);// move up the list pointer
	}


	// get bishop moves. Pin has to be considered.
	a=b->piececolorBB[2][player-1];
	while( a ){
		GET_BIT(a)
		mc1=get_bishop_moves(b,list,player,bit,o,allowed_mask&dir_mask[p_d2[bit]][bit]);
		move_count+=mc1;// count
		list+=(mc1<<1);// move up the list pointer
	}


	// get knight moves. Pin has to be considered.
	a=b->piececolorBB[1][player-1]&(~pinBB);// BB of unpinned knights
	while( a ){
		GET_BIT(a)
		mc1=get_knight_moves(b,list,player,bit,allowed_mask);
		move_count+=mc1;// count
		list+=(mc1<<1);// move up the list pointer
	}
	

	// get pawn moves. Pin has to be considered.
	if( player==1 ){// for white pawns***************************************************************
		// first, unpinned pawns
		a=b->piececolorBB[0][0]&(~pinBB);// BB of unpinned pawns
		// move forward two squares. Only select pawns on row 2.
		a2=((a&0x0202020202020202)<<2)&(~o)&(~(o<<1));// moving to empty squares, through empty squares
		// serialize
		while( a2 ){
			GET_BIT(a2)
			list[0]=(unsigned char)bit-2;list[1]=(unsigned char)bit;move_count++;list+=2;
		}
		// move forward one square. Exclude rank 7=promotions
		a2=((a&0xbfbfbfbfbfbfbfbf)<<1)&(~o);// moving to empty squares
		// serialize
		while( a2 ){
			GET_BIT(a2)
			list[0]=(unsigned char)bit-1;list[1]=(unsigned char)bit;move_count++;list+=2;
		}

		// second, pinned pawns
		a=b->piececolorBB[0][0]&pinned_pawns;// BB of pinned pawns
		if( a){
		// move forward two squares. Only select pawns on row 2.
		a2=((a&0x0202020202020202)<<2)&(~o)&(~(o<<1));// moving to empty squares, through empty squares
		// serialize
		while( a2 ){
			GET_BIT(a2)

			// exclude disallowed moves
			unsigned int pin=0;
			for(j=0;j<p_c;++j)// find pinned data
				if( bit-2==p_cell[j] ){// here pawn moves from bit-2
					pin=1;
					break;
				}
			if( !pin || p_d[j]==1 ){// allowed. Or not pinned. Record it
				list[0]=(unsigned char)bit-2;list[1]=(unsigned char)bit;move_count++;list+=2;
			}
		}
		// move forward one square. Exclude rank 7=promotions
		a2=((a&0xbfbfbfbfbfbfbfbf)<<1)&(~o);// moving to empty squares
		// serialize
		while( a2 ){
			GET_BIT(a2)

			// exclude disallowed moves
			unsigned int pin=0;
			for(j=0;j<p_c;++j)// find pinned data
				if( bit-1==p_cell[j] ){// here pawn moves from bit-1
					pin=1;
					break;
				}
			if( !pin || p_d[j]==1 ){// allowed. Or not pinned. Record it
				list[0]=(unsigned char)bit-1;list[1]=(unsigned char)bit;move_count++;list+=2;
			}
		}
		}
	}else{// for black pawns*****************************************************************************
		// first, unpinned pawns
		a=b->piececolorBB[0][1]&(~pinBB);// BB of unpinned pawns
		// move forward two squares. Only select pawns on row 7.
		a2=((a&0x4040404040404040)>>2)&(~o)&(~(o>>1));// moving to empty squares, through empty squares
		// serialize
		while( a2 ){
			GET_BIT(a2)
			list[0]=(unsigned char)bit+2;list[1]=(unsigned char)bit;move_count++;list+=2;
		}
		// move forward one square. Exclude rank 2=promotions
		a2=((a&0xfdfdfdfdfdfdfdfd)>>1)&(~o);// moving to empty squares
		// serialize
		while( a2 ){
			GET_BIT(a2)
			list[0]=(unsigned char)bit+1;list[1]=(unsigned char)bit;move_count++;list+=2;
		}

		// second, pinned pawns
		a=b->piececolorBB[0][1]&pinned_pawns;// BB of pinned pawns
		if( a ){
		// move forward two squares. Only select pawns on row 7.
		a2=((a&0x4040404040404040)>>2)&(~o)&(~(o>>1));// moving to empty squares, through empty squares
		// serialize
		while( a2 ){
			GET_BIT(a2)

			// exclude disallowed moves
			unsigned int pin=0;
			for(j=0;j<p_c;++j)// find pinned data
				if( bit+2==p_cell[j] ){// here pawn moves from bit+2
					pin=1;
					break;
				}
			if( !pin || p_d[j]==1 ){// allowed. Or not pinned. Record it
				list[0]=(unsigned char)bit+2;list[1]=(unsigned char)bit;move_count++;list+=2;
			}
		}
		// move forward one square. Exclude rank 2=promotions
		a2=((a&0xfdfdfdfdfdfdfdfd)>>1)&(~o);// moving to empty squares
		// serialize
		while( a2 ){
			GET_BIT(a2)

			// exclude disallowed moves
			unsigned int pin=0;
			for(j=0;j<p_c;++j)// find pinned data
				if( bit+1==p_cell[j] ){// here pawn moves from bit+1
					pin=1;
					break;
				}
			if( !pin || p_d[j]==1 ){// allowed. Or not pinned. Record it
				list[0]=(unsigned char)bit+1;list[1]=(unsigned char)bit;move_count++;list+=2;
			}
		}
		}
	}


	// get king moves. Pin does not get into consideration.
	mc1=get_king_moves_l_no_captures(b,list,player,king_position);
	move_count+=mc1;
	list+=(mc1<<1);// move up the list pointer
	return(unsigned int(move_count));// return move count
}

unsigned int get_all_moves(board *b,unsigned char *list){// get list of all available moves. Return count. Put moves on the list.
	UINT64 pi;
	unsigned int mc=get_all_moves_new_part1(b,list,&pi);	// captures generated
	mc+=get_all_moves_new_part2(b,&list[2*mc],pi);	// non-captures generated
	return(mc);// return move count
}

unsigned int get_all_attack_moves(board *b,unsigned char *list){// get list of all attack moves. Return count. Put moves on the list. Order is different: now pinned and unpinned pawn moves are together!
	UINT64 move_count=0,mc1,a,a2,o=b->colorBB[0]|b->colorBB[1],allowed_mask=b->colorBB[2-b->player];
	unsigned long bit;
	unsigned int player=b->player;


	// get pawn moves.
	if( player==1 ){// for white pawns***************************************************************
		a=b->piececolorBB[0][0];// BB of pawns
		// count promotions as attack moves
		a2=((a&0x4040404040404040)<<1)&(~o);// moving to empty squares
		// serialize
		while( a2 ){
			GET_BIT(a2)
			list[0]=(unsigned char)bit-1;list[1]=(unsigned char)bit;move_count++;list+=2;
		}
		// attack to +9.
		a2=(a<<9)&b->colorBB[1];// moving to OPP squares
		// serialize
		while( a2 ){
			GET_BIT(a2)
			list[0]=(unsigned char)bit-9;list[1]=(unsigned char)bit;move_count++;list+=2;
		}
		// attack to -7.
		a2=(a>>7)&b->colorBB[1];// moving to OPP squares
		// serialize
		while( a2 ){
			GET_BIT(a2)
			list[0]=(unsigned char)bit+7;list[1]=(unsigned char)bit;move_count++;list+=2;
		}
	}else{// for black pawns*****************************************************************************
		a=b->piececolorBB[0][1];// BB of pawns
		// count promotions as attack moves
		a2=((a&0x0202020202020202)>>1)&(~o);// moving to empty squares
		// serialize
		while( a2 ){
			GET_BIT(a2)
			list[0]=(unsigned char)bit+1;list[1]=(unsigned char)bit;move_count++;list+=2;
		}
		// attack to +7.
		a2=(a<<7)&b->colorBB[0];// moving to OPP squares
		// serialize
		while( a2 ){
			GET_BIT(a2)
			list[0]=(unsigned char)bit-7;list[1]=(unsigned char)bit;move_count++;list+=2;
		}
		// attack to -9.
		a2=(a>>9)&b->colorBB[0];// moving to OPP squares
		// serialize
		while( a2 ){
			GET_BIT(a2)
			list[0]=(unsigned char)bit+9;list[1]=(unsigned char)bit;move_count++;list+=2;
		}
	}


	// get rook moves.
	a=b->piececolorBB[3][player-1];
	while( a ){
		GET_BIT(a)
		mc1=get_rook_moves(b,list,player,bit,o,allowed_mask);
		move_count+=mc1;// count
		list+=(mc1<<1);// move up the list pointer
	}


	// get bishop moves.
	a=b->piececolorBB[2][player-1];
	while( a ){
		GET_BIT(a)
		mc1=get_bishop_moves(b,list,player,bit,o,allowed_mask);
		move_count+=mc1;// count
		list+=(mc1<<1);// move up the list pointer
	}
	

	// get knight moves.
	a=b->piececolorBB[1][player-1];
	while( a ){
		GET_BIT(a)
		mc1=get_knight_moves(b,list,player,bit,allowed_mask);
		move_count+=mc1;// count
		list+=(mc1<<1);// move up the list pointer
	}
	

	// get king moves.
	a=king_masks[b->kp[player-1]]&allowed_mask;
	if( a ) {
		// eliminate cells attacked by opp pawns
		if( (player&2) )
			a2=(b->piececolorBB[0][0]<<9) | (b->piececolorBB[0][0]>>7);// white pawns attack
		else
			a2=(b->piececolorBB[0][1]<<7) | (b->piececolorBB[0][1]>>9);// black pawns attack
		a2|=king_masks[b->kp[2-player]];// eliminate cells attacked by opp king
		a&=(~a2);// keep opponent - those are attacks
		while( a ){
			GET_BIT(a)
			list[0]=b->kp[player-1];list[1]=(unsigned char)bit;move_count++;list+=2;
		}
	}


	// get all en passant captures. Check them for pin, since they are so wierd.
	if( b->last_move!=INVALID_LAST_MOVE ){// valid last move. This guarantees that at least 1 opp pawn is in correct place. They can only be disallowed by pins.
		mc1=get_ep_moves_l(b,list,player);
		move_count+=mc1;// count
		list+=(mc1<<1);// move up the list pointer
	}


	// get queen moves.
	a=b->piececolorBB[4][player-1];
	while( a ){
		GET_BIT(a)
		mc1=get_queen_moves(b,list,player,bit,o,allowed_mask);
		move_count+=mc1;// count
		list+=(mc1<<1);// move up the list pointer
	}
	return(unsigned int(move_count));// return move count
}

static unsigned int get_all_nonking_moves_to_cell(board *b,unsigned char *list,unsigned int cell,unsigned int capture){
	UINT64 bb;
	unsigned long bit;
	unsigned int mc=0,player0=b->player-1;

	// pawns
	if( !capture ){// only get non-capture pawn moves. Include ep captures.
		if( player0==0 ){// white
			if( (cell&7)==3 && b->piece[cell-1]==0 && b->piece[cell-2]==64+1 ){// move forward 2 cells, only if both empty and first move
				list[0]=cell-2;list[1]=cell;mc++;list+=2;
			}else if( cell && b->piece[cell-1]==64+1 ){// move forward, only if empty and not last cell
				list[0]=cell-1;list[1]=cell;mc++;list+=2;
			}
		}else{// black
			if( (cell&7)==4 && b->piece[cell+1]==0 && b->piece[cell+2]==128+1 ){// move forward 2 cells, only if both empty and first move
				list[0]=cell+2;list[1]=cell;mc++;list+=2;
			}else if( cell<63 && b->piece[cell+1]==128+1 ){// move forward, only if empty and not last cell
				list[0]=cell+1;list[1]=cell;mc++;list+=2;
			}
		}
	}else{// only get capture pawn moves
		if( player0==1 ){// black moves
			if( cell>=7 && b->piece[cell-7]==1+128 ){//move -7
				list[0]=cell-7;list[1]=cell;mc++;list+=2;
			}
			if( cell<=54 && b->piece[cell+9]==1+128 ){//move +9
				list[0]=cell+9;list[1]=cell;mc++;list+=2;
			}
		}else{// white moves
			if( cell>=9 && b->piece[cell-9]==1+64 ){//move -9
				list[0]=cell-9;list[1]=cell;mc++;list+=2;
			}
			if( cell<=56 && b->piece[cell+7]==1+64 ){//move +7
				list[0]=cell+7;list[1]=cell;mc++;list+=2;
			}
		}
	}

	// knight
	bb=knight_masks[cell]&b->piececolorBB[1][player0];// select player's knights
	while( bb ){
		GET_BIT(bb)
		list[0]=(unsigned char)bit;list[1]=cell;mc++;list+=2;
	}

	// use magics to look at all directions and see if they terminate at appropriate opp sliding piece - non-looping solution.
	bb=(b->piececolorBB[2][player0]|b->piececolorBB[4][player0])&attacks_bb_B(cell,b->colorBB[0]|b->colorBB[1]);// bishop (and queen) attacks
	bb|=(b->piececolorBB[3][player0]|b->piececolorBB[4][player0])&attacks_bb_R(cell,b->colorBB[0]|b->colorBB[1]);// rook (and queen) attacks
	while( bb ){
		GET_BIT(bb)
		list[0]=(unsigned char)bit;list[1]=cell;mc++;list+=2;
	}
	return(mc);
}

unsigned int get_out_of_check_moves(board *b,unsigned char *list,unsigned int k_cell,unsigned int a_cell){// get list of moves that get king out of check
	UINT64 one=1,attack_mask,p_a;
	unsigned int i,mc=0,mc2,double_check,attacker;
	unsigned char list2[256];

	// 0. see if this is a double check.
	i=(b->piece[a_cell]&7)-1;
	b->piececolorBB[i][2-b->player]^=one<<a_cell;			// reset bitboard for attackin OPP
	double_check=cell_under_attack(b,k_cell,b->player);		// see if i am still under attack
	b->piececolorBB[i][2-b->player]^=one<<a_cell;			// restore bitboard for attackin OPP

	// 1. get moves that capture attacking piece (only if not double check)
	if( !double_check ){
		mc2=get_all_nonking_moves_to_cell(b,list2,a_cell,1);// pawn - capture moves only
		for(i=0;i<mc2;++i){// here i could move pinned piece. Handle it.
			if( !moving_piece_is_pinned(b,list2[2*i],list2[2*i+1],3-b->player) ){// include
				list[2*mc]=list2[2*i];list[2*mc+1]=list2[2*i+1];mc++;
			}
		}
		// 1a. see if en passant capture is a legal move
		if( b->last_move!=INVALID_LAST_MOVE ){
			mc2=get_ep_moves_l(b,list+2*mc,b->player);
			mc+=mc2;// count
		}
	}

	// 2. get king moves, including captures of attacking piece.
	// eliminate cells attacked by opp pawns
	if( b->player&2 )
		p_a=(b->piececolorBB[0][0]<<9) | (b->piececolorBB[0][0]>>7);// white pawns attack
	else
		p_a=(b->piececolorBB[0][1]<<7) | (b->piececolorBB[0][1]>>9);// black pawns attack
	
	p_a|=b->colorBB[b->player-1];// eliminate cells occupied by player (keep opponent - those are attacks)
	p_a|=king_masks[b->kp[2-b->player]];// eliminate cells attacked by opp king
	attack_mask=king_masks[k_cell]&(~p_a);

	attacker=b->piece[a_cell]&63;// 1-5. Cannot be 0 and cannot be 6.
	switch(attacker){
		case 1:break;// other pawn attack is always outside of king move area.
		case 2:
			attack_mask&=~knight_masks[a_cell];
			break;
		case 3:
			attack_mask&=~bishop_masks[a_cell];
			break;
		case 4:
			attack_mask&=~rook_masks[a_cell];
			break;
		case 5:{
			unsigned int dir=dir_norm[a_cell][k_cell];// do not apply queen mask: it incorrectly excludes blocked attack in other ray direction.
			if(dir==1 || dir==8)
				attack_mask&=~rook_masks[a_cell];
			else
				attack_mask&=~bishop_masks[a_cell];
			break;}
		default:assert(false);break;
	}
	while( attack_mask ){unsigned long bit;
		GET_BIT(attack_mask)
		list[2*mc]=k_cell;list[2*mc+1]=(unsigned char)bit;mc++;
	}
	
	// 3. get blockers (only if not double check)
	if( !double_check && attacker>2 ){// only if attacker is a ray piece: B,R,Q.
		int dst=dist[k_cell][a_cell];// distance from king to attacker
		if( dst>1 ){// only if there are squares in between
			int dx=(char)(k_cell&7)-(char)(a_cell&7);
			int dy=(char)(k_cell>>3)-(char)(a_cell>>3);
			int dir=(dx+dy*8)/dst;// get direction, from a_cell
			unsigned int cell=a_cell;
			for(;dst>1;--dst){
				cell+=dir;
				mc2=get_all_nonking_moves_to_cell(b,list2,cell,0);// pawn - non-capture moves only
				for(i=0;i<mc2;++i){// here i could move pinned piece. Handle it.
					if( !moving_piece_is_pinned(b,list2[2*i],list2[2*i+1],3-b->player) ){// include
						list[2*mc]=list2[2*i];list[2*mc+1]=list2[2*i+1];mc++;
					}
				}
			}
		}
	}
	return(mc);
}

void unmake_null_move(board *b){// null move: change player, score and TT hash key
	b->hash_key^=player_zorb;	// flip player
	b->player=3-b->player;		// switch player
	#if calc_pst==0
	b->scorem=-b->scorem;		// change score2
	b->scoree=-b->scoree;		// change score2
	#endif
}

void make_null_move(board *b){// null move: change player, score and TT hash key
	unmake_null_move(b);
	#if USE_PREFETCH
	_mm_prefetch((const char*)&h[get_hash_index],_MM_HINT_T0); // prefetch the main hash
	#endif
}

void make_move(board *b,unsigned char from,unsigned char to,unmake *d){// make a move.
	UINT64 z,zp,one=1,ToBB=(one<<to),fromToBB;
	unsigned int pll=b->player-1;	// now player is 0/1. Use ^1 to invert.
	#if calc_pst==0
	int sl4;						// local scores
	#endif
	unsigned char v=b->piece[from];	// get piece to move
	unsigned char w=b->piece[to];	// get piece replaced
	unsigned int vi=(((v&7)-1)<<1)+pll; // index of "from" piece

	// save un-make data - 40 bytes including fillers
	d->hash_key=b->hash_key;								// save 8 bytes - main hash.
	((UINT64*)&d->kp[0])[0]=((UINT64*)&b->kp[0])[0];		// save 8 bytes - kp[2], player, last move, halfmoveclock. 5 bytes. Plus filler(3).

	// Z key update
	z=zorb[0][vi][to];		// update Z key for "to", new piece. Here piece is never empty!
	z^=zorb[0][vi][from];	// update Z key for "from". Here piece is never empty!
	if( b->last_move!=INVALID_LAST_MOVE ){
		assert(b->last_move<64 && (b->last_move&7)==2 || (b->last_move&7)==5);
		b->hash_key^=last_move_hash_adj;
		b->last_move=INVALID_LAST_MOVE;	// illegal last move. Overwrite later if needed.
	}
	if( !w ){// no capture - prefetch now!
		b->hash_key^=z^player_zorb;	// flip player;
		#if USE_PREFETCH
		_mm_prefetch((const char*)&h[get_hash_index],_MM_HINT_T0); // prefetch the main hash
		_mm_prefetch((const char*)&eh[get_eval_hash_index],_MM_HINT_T0); // prefetch eval hash
		#endif
	}

	// save un-make data - 40 bytes including fillers
	d->pawn_hash_key=b->pawn_hash_key;						// save 8 bytes - pawn hash.
	((UINT64*)&d->castle)[0]=((UINT64*)&b->castle)[0];		// save 8 bytes - castle and scores.
	((UINT64*)&d->mat_key)[0]=((UINT64*)&b->mat_key)[0];	// save 8 bytes - material hash+piece value

	// assign some un-make vars
	d->move_type=0;			// move type is quiet
	d->w=0;					// piece captured
	d->from=from;			// from
	d->to=to;				// to

	// score update
	#if calc_pst==0
	sl4=((int*)&piece_square[0][vi][to][0])[0];	// add new "to". Here piece is never empty!
	sl4-=((int*)&piece_square[0][vi][from][0])[0];// remove current "from". Here piece is never empty!
	#endif

	if( w ){// capture.
		unsigned int wi=(((w&7)-1)<<1)+pll^1;		// index of "to" piece
		UINT64 zz=zorb[0][wi][to];
		b->hash_key^=z^player_zorb^zz;				// update Z key for "to", old. Only for captures. Here piece is never empty!. flip player;
		#if USE_PREFETCH
		_mm_prefetch((const char*)&h[get_hash_index],_MM_HINT_T0); // prefetch the main hash
		_mm_prefetch((const char*)&eh[get_eval_hash_index],_MM_HINT_T0); // prefetch eval hash
		#endif
		zp=(v&7)==1?z:0;							// Zp=Z, if pawn move. This has to be before application of "wi".
		zp^=(w&7)==1?zz:0;							// update Zp key for "to", old. Only for captures.
		b->pawn_hash_key^=zp;
		#if calc_pst==0
		sl4-=((int*)&piece_square[0][wi][to][0])[0];	// remove current "to". Only for captures. Here piece is never empty!
		#endif
		b->mat_key-=mat_key_mult[wi];				// remove captured piece from material key
		d->move_type=2;								// move type is capture
		d->w=w;										// piece captured
		d->cc=to;									// square captured
		b->colorBB[pll^1]^=ToBB;					// update occupied BB of opponent for capture
		b->piececolorBB[0][wi]^=ToBB;				// update occupied BB of opponent for capture
		b->halfmoveclock=0;							// reset half-move clock for capture
		b->piece_value-=pv10[w&7];					// update total piece value
	}else{// no capture
		if( !(v&6) ){// not capture and pawn move.
			zp=z;										// here zp is the same as z.
			b->halfmoveclock=0;							// pawn moves - reset half-move clock
			if( dir_norm[from][to]>1 ){// en passant capture: move diag, pawn, to empty cell. Last 2 are already handled.
				unsigned char to1;
				if(to>from)
					to1=from+8;
				else
					to1=from-8;
				b->hash_key^=zorb[0][pll^1][to1];			// update Z key for deleted pawn. Here piece is never empty!
				#if USE_PREFETCH
				_mm_prefetch((const char*)&h[get_hash_index],_MM_HINT_T0); // prefetch the main hash
				_mm_prefetch((const char*)&eh[get_eval_hash_index],_MM_HINT_T0); // prefetch eval hash
				#endif
				zp^=zorb[0][pll^1][to1];					// update ZP key for deleted pawn.
				b->mat_key-=mat_key_mult[pll^1];			// remove captured pawn from material key
				#if calc_pst==0
				sl4-=((int*)&piece_square[0][pll^1][to1][0])[0];	// update score for deleted pawn. Here piece is never empty!
				#endif
				d->move_type=2+8;							// move type is (ep) capture. Add 8 here.
				d->w=b->piece[to1];							// piece captured
				d->cc=to1;									// square captured
				set_piece(b,to1,0);							// delete the pawn
				b->colorBB[pll^1]^=(one<<to1);				// update occupied BB of opponent - delete pawn
				b->piececolorBB[0][pll^1]^=(one<<to1);		// update occupied BB of opponent - delete pawn
			}else if( dist[from][to]>1 ){					// record pawn move just made. Only for move by 2, by pawn - for future en passant capture
				unsigned char opp_pawn;
				if( pll )// black moved
					opp_pawn=64+1;// opp is white
				else// white moved
					opp_pawn=128+1;// opp is black
				if( (to>=8 && b->piece[to-8]==opp_pawn) || (to<56 && b->piece[to+8]==opp_pawn) ){// only mark last move if opp has a pawn in the right place.
					b->last_move=(to+from)>>1;
					assert(b->last_move<64 && (b->last_move&7)==2 || (b->last_move&7)==5);
					b->hash_key^=last_move_hash_adj;
					#if USE_PREFETCH
					_mm_prefetch((const char*)&h[get_hash_index],_MM_HINT_T0); // prefetch the main hash
					_mm_prefetch((const char*)&eh[get_eval_hash_index],_MM_HINT_T0); // prefetch eval hash
					#endif
				}
			}
			b->pawn_hash_key^=zp;
		}// end of pawn move
		else b->halfmoveclock++;							// increment half-move clock
	}//end of no capture

	// update bitboards
	fromToBB=ToBB|(one<<from);
	b->colorBB[pll]^=fromToBB;					// update occupied BB of player
	b->piececolorBB[0][vi]^=fromToBB;			// update occupied BB of player

	// castling rights update - only if there are castling rights left.
	if( b->castle && (fromToBB&0x8100008100000081) ){// only if castling rights left and to/from impacts kings or rooks.
		b->hash_key^=zorb_castle[b->castle]; // undo current
		b->castle&=castle_mask[from]&castle_mask[to];
		b->hash_key^=zorb_castle[b->castle]; // apply new
	}

	// move the piece on the board
	set_piece(b,to,v);		// set to "to"
	set_piece(b,from,0);	// clear from "from"

	// check for pawn promotion
	if( !(v&6) ){// pawn move
		if( (to&7)==0 || (to&7)==7 ){// promotion
			b->pawn_hash_key^=zorb[4][pll][to];		// remove queen from pawn hash
			b->mat_key-=mat_key_mult[pll];			// remove pawn from material key
			d->move_type+=4;						// move type is promotion
			b->piececolorBB[0][pll]^=ToBB;			// delete player pawn
			set_piece(b,to,v+4-d->promotion);		// make it X
			b->mat_key+=mat_key_mult[(4-d->promotion)*2+pll]; // add X to material key
			b->piececolorBB[4-d->promotion][pll]^=ToBB;	// add player X
			b->piece_value+=pv10[5-d->promotion];	// update total piece value
			if( d->promotion ){
				#if calc_pst==0
				sl4-=((int*)&piece_square[4][pll][to][0])[0];	// remove Q
				sl4+=((int*)&piece_square[4-d->promotion][pll][to][0])[0];	// add X
				#endif
				b->hash_key^=zorb[4][pll][to];			// remove queen from main hash
				b->hash_key^=zorb[4-d->promotion][pll][to];	// add X to main hash
			}
		}
	}else if( (v&7)==6 ){// king moves
		b->kp[pll]=to;								// update king position
		if( from==32 ){// white king
			if( to==16 ){// castle: white, lower
				b->hash_key^=zorb[3][0][0];			// update Z key for white rook
				b->hash_key^=zorb[3][0][24];		// update Z key for white rook
				d->move_type=1;						// move type is casltling
				set_piece(b,24,64+4);				// place rook where it belongs
				set_piece(b,0,0);					// clear it in the old place
				#if calc_pst==0
				sl4+=((int*)&piece_square[3][0][24][0])[0];	// place rook where it belongs
				sl4-=((int*)&piece_square[3][0][0][0])[0];	// clear it in the old place
				#endif
				UINT64 m=(one<<0)^(one<<24);
				b->colorBB[0]^=m;					// update occupied BB of player - move rook
				b->piececolorBB[3][0]^=m;			// update occupied BB of player - move rook
			}else if( to==48 ){// castle: white, higher
				b->hash_key^=zorb[3][0][40];		// update Z key for white rook
				b->hash_key^=zorb[3][0][56];		// update Z key for white rook
				d->move_type=1;						// move type is casltling
				set_piece(b,40,64+4);				// place rook where it belongs
				set_piece(b,56,0);					// clear it in the old place
				#if calc_pst==0
				sl4+=((int*)&piece_square[3][0][40][0])[0];	// place rook where it belongs
				sl4-=((int*)&piece_square[3][0][56][0])[0];	// clear it in the old place
				#endif
				UINT64 m=(one<<40)^(one<<56);
				b->colorBB[0]^=m;					// update occupied BB of player - move rook
				b->piececolorBB[3][0]^=m;			// update occupied BB of player - move rook
			}
		}else if( from==39 ){// black king
			if( to==23 ){// castle: black, lower
				b->hash_key^=zorb[3][1][31];		// update Z key for black rook
				b->hash_key^=zorb[3][1][7];			// update Z key for black rook
				d->move_type=1;						// move type is casltling
				set_piece(b,31,128+4);				// place rook where it belongs
				set_piece(b,7,0);					// clear it in the old place
				#if calc_pst==0
				sl4+=((int*)&piece_square[3][1][31][0])[0];	// place rook where it belongs
				sl4-=((int*)&piece_square[3][1][7][0])[0];	// clear it in the old place
				#endif
				UINT64 m=(one<<7)^(one<<31);
				b->colorBB[1]^=m;					// update occupied BB of player - move rook
				b->piececolorBB[3][1]^=m;			// update occupied BB of player - move rook
			}else if( to==55 ){// castle: black, higher
				b->hash_key^=zorb[3][1][47];		// update Z key for black rook
				b->hash_key^=zorb[3][1][63];		// update Z key for black rook
				d->move_type=1;						// move type is casltling
				set_piece(b,47,128+4);				// place rook where it belongs
				set_piece(b,63,0);					// clear it in the old place
				#if calc_pst==0
				sl4+=((int*)&piece_square[3][1][47][0])[0];	// place rook where it belongs
				sl4-=((int*)&piece_square[3][1][63][0])[0];	// clear it in the old place
				#endif
				UINT64 m=(one<<47)^(one<<63);
				b->colorBB[1]^=m;					// update occupied BB of player - move rook
				b->piececolorBB[3][1]^=m;			// update occupied BB of player - move rook
			}
		}
	}

	b->player=2-pll;			// switch player

	// return score for the player.
	#if calc_pst==0
	if( pll ) // black moves
		((int*)&b->scorem)[0]=-((int*)&b->scorem)[0]+sl4;
	else // white moves
		((int*)&b->scorem)[0]=-((int*)&b->scorem)[0]-sl4;
	#endif
}

void unmake_move(board *b,unmake *d){
	// restore un-make data - 40 bytes including fillers
	b->hash_key=d->hash_key;								// 8 bytes
	b->pawn_hash_key=d->pawn_hash_key;						// 8 bytes
	((UINT64*)&b->castle)[0]=((UINT64*)&d->castle)[0];		// 8 bytes - castle and scores.
	((UINT64*)&b->kp[0])[0]=((UINT64*)&d->kp[0])[0];		// 8 bytes - kp[2], player, last move, halfmoveclock. 5 bytes. Plus filler.
	((UINT64*)&b->mat_key)[0]=((UINT64*)&d->mat_key)[0];	// 8 bytes - material hash+piece value

	// define bitmasks
	UINT64 one=1;
	UINT64 ToBB=(one<<d->to);
	UINT64 fromToBB=ToBB|(one<<d->from);

	// undo promotion
	if( d->move_type&4 ){
		b->piece[d->to]=1+(b->player<<6);		// change board piece back to pawn
		b->piececolorBB[4-d->promotion][b->player-1]^=ToBB;	// update occupied BB of player - remove X
		b->piececolorBB[0][b->player-1]^=ToBB;	// update occupied BB of player - add pawn
	}

	// update bitboards
	b->colorBB[b->player-1]^=fromToBB;								// update occupied BB of player
	b->piececolorBB[(b->piece[d->to]&7)-1][b->player-1]^=fromToBB;	// update occupied BB of player

	// undo the move
	b->piece[d->from]=b->piece[d->to];	// move the piece back
	b->piece[d->to]=0;					// reset "to" square

	// 2=capture. Restore captured piece, regular or ep
	if( d->move_type&2 ){
		b->piece[d->cc]=d->w; // piece
		b->colorBB[2-b->player]^=(one<<d->cc);// color bitboard
		b->piececolorBB[(d->w&7)-1][2-b->player]^=(one<<d->cc);// piececolor bitboard
	}else if( d->move_type&1 ){// 1=castling - move the rook back to where it was
		if( d->from==32 ){// white king moved
			if( d->to==16 ){// lower
				set_piece(b,0,64+4);				// place rook where it belongs
				set_piece(b,24,0);					// clear it in the old place
				UINT64 m=(one<<0)^(one<<24);
				b->colorBB[0]^=m;					// update occupied BB of player - move rook
				b->piececolorBB[3][0]^=m;			// update occupied BB of player - move rook
			}else{// higher
				set_piece(b,56,64+4);				// place rook where it belongs
				set_piece(b,40,0);					// clear it in the old place
				UINT64 m=(one<<40)^(one<<56);
				b->colorBB[0]^=m;					// update occupied BB of player - move rook
				b->piececolorBB[3][0]^=m;			// update occupied BB of player - move rook
			}
		}else{// black king moved
			if( d->to==23 ){// lower
				set_piece(b,7,128+4);				// place rook where it belongs
				set_piece(b,31,0);					// clear it in the old place
				UINT64 m=(one<<7)^(one<<31);
				b->colorBB[1]^=m;					// update occupied BB of player - move rook
				b->piececolorBB[3][1]^=m;			// update occupied BB of player - move rook
			}else{// higher
				set_piece(b,63,128+4);				// place rook where it belongs
				set_piece(b,47,0);					// clear it in the old place
				UINT64 m=(one<<47)^(one<<63);
				b->colorBB[1]^=m;					// update occupied BB of player - move rook
				b->piececolorBB[3][1]^=m;			// update occupied BB of player - move rook
			}
		}
	}
}

_declspec(noinline) unsigned int player_moved_into_check(board *b,unsigned int cell,unsigned char player){// returns 1 if in check, 0 otherwise. Only after king moves, where move into check is possible. Exclude attacks by pawns and king - those are excluded during move generation. This uses bitmasks, but not mailbox values.
	UINT64 bb,o;	
	assert(cell<64);

	// knight
	player=2-player;
	bb=knight_masks[cell]&b->piececolorBB[1][player];// select OPP knights
	if( bb ) return(1); // attacked by OPP knight. Return.

	// use rays to look at all directions and see if they terminate at appropriate opp sliding piece - non-looping solution.
	o=b->colorBB[0]|b->colorBB[1];
	bb=(b->piececolorBB[2][player]|b->piececolorBB[4][player])&attacks_bb_B(cell,o);// bishop (and queen) attacks
	if( bb ) return(1);
	bb=(b->piececolorBB[3][player]|b->piececolorBB[4][player])&attacks_bb_R(cell,o);// rook (and queen) attacks
	if( bb ) return(1);
	return(0);// no attack found
}

_declspec(noinline) unsigned int move_gives_check(board *b,unsigned int from,unsigned int to){// returns 0/1 indicator. Board is unchanged. Called BEFORE the move is made. "b->player" makes the move, opponent is analysed for check. All board info is needed.
	UINT64 a,one,o;
	unsigned int pll=b->player-1,opp_king_cell=b->kp[pll^1],d;

	// 1. see if moving piece produces a check
	switch( b->piece[from]&7 ){
		case 1:// pawn
			if( (pawn_attacks[pll^1][to]&b->piececolorBB[5][pll^1]) ) return(1); // pawn checks opp king

			// logic for promotion
			if( (to&7)==0 || (to&7)==7 ){// promotion. Assume always to queen.
				if( dir_norm[opp_king_cell][to] ){// new Q and opp K are on the same line. See if there are blockers in between.
					one=1;
					if( (ray_segment[opp_king_cell][to]&((~(one<<from))&(b->colorBB[0]|b->colorBB[1])))==0 ) return(1);// promoted queen checks opp king
				}
			}

			// is this ep capture?
			if( to==b->last_move ){// en passant capture: pawn, moves to last move.
				if( to>from )// position of captured pawn
					d=from+8;
				else
					d=from-8;
				if( dir_norm[opp_king_cell][d] ){// possible attack.
					one=1;
					o=(b->colorBB[0]|b->colorBB[1]|(one<<to))&(~(one<<from))&(~(one<<d));							 // occupied cells after the move is completed
					if( attacks_bb_B(opp_king_cell,o)&(b->piececolorBB[2][pll]|b->piececolorBB[4][pll]) ) return(1); // attacked by B or Q
					if( attacks_bb_R(opp_king_cell,o)&(b->piececolorBB[3][pll]|b->piececolorBB[4][pll]) ) return(1); // attacked by R or Q
				}
			}
			break;
		case 2:// knight
			if( knight_masks[to]&b->piececolorBB[5][pll^1] ) return(1);
			break;
		case 3:// bishop
			d=dir_norm[opp_king_cell][to];
			if( d==7 || d==9 ){
				a=ray_segment[opp_king_cell][to];
				if( (a&(b->colorBB[0]|b->colorBB[1]))==0 ) return(1);
			}
			break;
		case 4:// rook
			d=dir_norm[opp_king_cell][to];
			if( d==1 || d==8 ){
				a=ray_segment[opp_king_cell][to];
				if( (a&(b->colorBB[0]|b->colorBB[1]))==0 ) return(1);
			}
			break;
		case 5:// queen
			d=dir_norm[opp_king_cell][to];
			if( d ){
				a=ray_segment[opp_king_cell][to];
				if( (a&(b->colorBB[0]|b->colorBB[1]))==0 ) return(1);
			}
			break;
		default:// king - can never produce check. Except for castling.
			if( (from>>3)==4 && dist[from][to]>1 ){// castling. See if rook from new position checks. Here check on "from" is to improve runtime.
				if( to==48 ) d=40; // d is new position of rook
				else if( to==16 ) d=24;
				else if( to==23 ) d=31;
				else d=47;
				one=1;
				a=attacks_bb_R(d,(b->colorBB[0]|b->colorBB[1]|(one<<to))&~(one<<from))&b->piececolorBB[5][pll^1];// rook in new position checks opp king
				if( a ) return(1);
			}
			break;
	}

	// 2. see if moving piece opens an existing slider attack (moving piece is pinned)
	return(moving_piece_is_pinned(b,from,to,pll+1));
}

_declspec(noinline) unsigned int moving_piece_is_pinned(board *b,unsigned int from,unsigned int to,unsigned int player){// returns 0/1 indicator. Board is unchanged. Called before the move. See if moving piece opens an existing slider attack (moving piece is pinned). "player" sliders are attacking "opponent's" king
	UINT64 a,a2,o;
	unsigned int pll=player-1,opp_king_cell=b->kp[pll^1],d=dir_norm[opp_king_cell][from],d2;
	if( !d || d==dir_norm[to][from] ) return(0);			// early return: not on an attack line, or move is along the attack line
	d2=dir_trans[d];										// direction 1/2/3/4 for d=1/7/8/9
	a=dir_mask[d2][opp_king_cell];							// attack line - always non-empty
	d2&=1;													// 0-bishop, 1-rook
	a2=b->piececolorBB[2+d2][pll]|b->piececolorBB[4][pll];	// sliders or correct type
	if( (a2=a&a2) ){										// there are correct sliders on the king attack line
		o=(b->colorBB[0]|b->colorBB[1])&~(UINT64(1)<<from);	// blockers after the move; don't need to add "to" - i already know it is not on the attack line.
		if( !d2 ) a=attacks_bb_B(opp_king_cell,o);			// bishop moves
		else a=attacks_bb_R(opp_king_cell,o);				// rook moves
		if( (a&a2) ) return(1);								// valid slider attack - return 1
	}
	return(0);
}

_declspec(noinline) unsigned int cell_under_attack(board *b,unsigned int cell,unsigned char player){// returns position where opponent's attack originates, plus 64
	UINT64 bb,o;
	unsigned long bit;
	assert(cell<64);
	
	// pawns
	player=2-player;// 1/2 -> 1/0
	bb=pawn_attacks[player][cell]&b->piececolorBB[0][player];
	if( bb ){// attacked by OPP pawn. Return.
		BSF64l(&bit,bb);
		return(64+bit);
	}

	// knight
	bb=knight_masks[cell]&b->piececolorBB[1][player];// select OPP knights
	if( bb ){// attacked by OPP knight. Return.
		BSF64l(&bit,bb);
		return(64+bit);
	}

	// use magics to look at all directions and see if they terminate at appropriate opp sliding piece - non-looping solution.
	o=b->colorBB[0]|b->colorBB[1];
	bb=(b->piececolorBB[2][player]|b->piececolorBB[4][player])&attacks_bb_B(cell,o);// bishop (and queen) attacks
	if( bb ){
		BSF64l(&bit,bb);
		return(64+bit);
	}
	bb=(b->piececolorBB[3][player]|b->piececolorBB[4][player])&attacks_bb_R(cell,o);// rook (and queen) attacks
	if( bb ){
		BSF64l(&bit,bb);
		return(64+bit);
	}

	// skip this code - it can only come into play for castling logic, and i fixed it there.
	// king, using kp.
	/*unsigned int z=b->kp[player];
	if( dist[cell][z]==1 )// OPP king is 1 step away.
		return(64+z);*/

	return(0);// no attack found
}