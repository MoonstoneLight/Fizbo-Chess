// training set code
#include "chess.h"
#include <math.h>
#include <intrin.h>
#include <immintrin.h>
#include "threads.h"
#include "coeffs.h"

//#include "pgn_utils2.cpp" // CCRL logs
//#include "pgn_utils.cpp" // self-play logs

// ts entry data structure
typedef struct{
	short int score;				// deep evaluation score.
	unsigned char piece[32];		// cell values are: 0=empty, 1=pawn, 2=knight, 3=bishop, 4=rook, 5=queen, 6=king. Plus 8 if black(top bit). So, 0-14. Use 1/2 byte.
	unsigned char c1:1,c2:1,c3:1,c4:1,player:1,remarks:3;
		// 4: castling possible: c1=white lower(Q), c2=white upper(K), c3=black lower(q), c4=black upper(k). 1=allowed, 0=not allowed.
		// 1: player. 0/1 for w/b.
		// 3: remarks: 0 is unsolved. 1 is fruit_231 for 3 sec. 2 is sf5 for 3 sec.
	unsigned char last_move;		// last move made to, for ep captures only
	unsigned char fullmoveclock;	// can be up to 100 or more - use byte for 0-255
	unsigned char dummy[3];
} ts_entry; // 2+32+1*6=40 bytes.

typedef struct {
	double mgw;
	board b;
	short int score_deep;
	short int score_shallow;
	unsigned char fullmoveclock;
} board_plus;

void convert_TS_to_board(board *b, ts_entry *ts){
	unsigned int i;
	static const unsigned char r[]={0,65,66,67,68,69,70,0,0,129,130,131,132,133,134};
	//								0  1  2  3  4  5  6 7 8   9  10  11  12  13  14

	// first, piece[] values
	for(i=0;i<64;i+=2){
		unsigned char v=ts->piece[i/2];
		b->piece[i]=r[v&15];
		b->piece[i+1]=r[v>>4];
	}

	// second, castling
	b->castle=ts->c1;
	b->castle+=2*ts->c2;
	b->castle+=4*ts->c3;
	b->castle+=8*ts->c4;

	// third, player
	b->player=ts->player+1; // turn 0/1 into 1/2

	// fourth, last move
	if( ts->last_move==0 || ts->last_move>=64 )
		b->last_move=INVALID_LAST_MOVE;
	else
		b->last_move=(ts->last_move&56)+(b->player==2?2:5);// new format


	// FYI - set hmc to 0
	b->halfmoveclock=0;
}

static unsigned char convert_board_piece_to_TS_piece(unsigned char v){
	unsigned int player=v>>7;// 0/1
	return((v&7)+8*player);// 8 for player, 0-6 for piece
}

void convert_board_to_TS(board *b, ts_entry *ts){
	unsigned int i;

	// first, piece[] values
	for(i=0;i<32;i++)// was 64!!!
		ts->piece[i]=convert_board_piece_to_TS_piece(b->piece[i*2])+(convert_board_piece_to_TS_piece(b->piece[i*2+1])<<4);

	// second, castling
	ts->c1=b->castle&1;
	ts->c2=(b->castle>>1)&1;
	ts->c3=(b->castle>>2)&1;
	ts->c4=(b->castle>>3)&1;

	// third, player
	ts->player=b->player-1; // turn 1/2 into 0/1

	// fourth, last move
	ts->last_move=b->last_move;
}

static unsigned int piece_count(ts_entry *p){
	unsigned int i,j;
	unsigned char c;
	static const unsigned int pvl[]={0,1,1,1,1,1,1,1};

	// count pieces
	for(i=j=0;i<32;++i){
		c=p->piece[i]&7;
		j+=pvl[c];
		c=(p->piece[i]>>4)&7;
		j+=pvl[c];
	}
	return(j);
}

static int fmc_p_sort_function(const void *arg1, const void *arg2){
	ts_entry *p1=(ts_entry *)arg1,*p2=(ts_entry *)arg2;
	unsigned int pc1=piece_count(p1),pc2=piece_count(p2);

	// on pieces
	if( pc1<pc2 )
		return(1);
	if( pc1>pc2 )
		return(-1);

	// tie
	return(0);
}

static void export_TS(void){
	// load TS
	FILE *f1=fopen("c://xde//chess//data//TS.bin","rb");// main TS file
	unsigned int i0,i;
	fread(&i0,sizeof(unsigned int),1,f1);
	ts_entry *ts_all=(ts_entry*)malloc(sizeof(ts_entry)*i0); // storage of ts entries.
	fread(ts_all,sizeof(ts_entry),i0,f1);
	fclose(f1);

	// here positions should be sorted by pieces.
	qsort(ts_all,i0,sizeof(ts_entry),fmc_p_sort_function);

	// now write summary file
	f1=fopen("c://xde//chess//data//TS_summary.csv","w");
	fprintf(f1,"pieces,count,c+,c-,c0\n");
	unsigned fmc0=32,c=0,cp=0,cm=0,c0=0;
	for(i=0;i<i0-5;++i){
		if( piece_count(&ts_all[i])*2!=fmc0*2 ){// new line - init
			fprintf(f1,"%d,%d,%d,%d,%d\n",fmc0,c,cp,cm,c0);
			fmc0=piece_count(&ts_all[i]);
			c=cp=cm=c0=0;// reset
		}
		c++;
		if( ts_all[i].score>0 )
			cp++;
		if( ts_all[i].score<0 )
			cm++;
		if( ts_all[i].score==0 )
			c0++;
	}
	// print the last one
	fprintf(f1,"%d,%d,%d,%d,%d\n",fmc0,c,cp,cm,c0);
	fclose(f1);
	exit(0);
}

static unsigned char quad(ts_entry *p1){
	unsigned int king_q[4]={0,0,0,0},i,kp[2],j;

	// set king positions
	i=j=0;
	do{
		UINT64 d=((UINT64*)&p1->piece[i])[0];// 8 cells
		d=d&0x6666666666666666; // mask out all but 2 bits for each cell
		d=d&(d<<1); // mask in the 2 bits
		if(d){
			char s=p1->piece[i]&15;
			if( (s&7)==6 ){// king found
				if( s>7 ) kp[1]=i*2;
				else kp[0]=i*2;
				j++;// increment king count
			}
			s=p1->piece[i]>>4;
			if( (s&7)==6 ){// king found
				if( s>7 ) kp[1]=i*2+1;
				else kp[0]=i*2+1;
				j++;// increment king count
			}
			i++;
		}else
			i+=8;
	}while(j<2);// stop when 2 kings are found
	if( kp[0]>=32 ) king_q[2]=1;// white king in Q2
	else king_q[0]=1;// white king in Q0
	if( kp[1]>=32 ) king_q[3]=1;// black king in Q3
	else king_q[1]=1;// black king in Q1
	return(king_q[0]*8+king_q[1]*4+king_q[2]*2+king_q[3]);  // 1 2 4 8
}

static int pos_sort_function_PST(const void *arg1, const void *arg2){
	ts_entry *p1=(ts_entry *)arg1;
	ts_entry *p2=(ts_entry *)arg2;
	unsigned int king_q1=p1->dummy[2];
	unsigned int king_q2=p2->dummy[2];

	// result
	if( king_q1>king_q2 )
		return(1);
	else if( king_q1<king_q2 )
		return(-1);
	else
		return(0);
}

static int pos_sort_function(const void *arg1, const void *arg2){
	ts_entry *p1=(ts_entry *)arg1;
	ts_entry *p2=(ts_entry *)arg2;

	// first, on ALL pieces
	for(unsigned int i=0;i<32;++i){
		if( p1->piece[i] > p2->piece[i] )
			return(1);
		if( p1->piece[i] < p2->piece[i] )
			return(-1);
	}

	// second, on player
	if( p1->player > p2->player )
		return(1);
	if( p1->player < p2->player )
		return(-1);

	// third, on remarks, decreasing
	if( p1->remarks < p2->remarks )
		return(1);
	if( p1->remarks > p2->remarks )
		return(-1);

	// tie
	return(0);
}

static int pos_sort_function0(const void *arg1, const void *arg2){// no FMC
	ts_entry *p1=(ts_entry *)arg1;
	ts_entry *p2=(ts_entry *)arg2;

	// first, on ALL pieces
	for(unsigned int i=0;i<32;++i){
		if( p1->piece[i] > p2->piece[i] )
			return(1);
		if( p1->piece[i] < p2->piece[i] )
			return(-1);
	}

	// second, on player
	if( p1->player > p2->player )
		return(1);
	if( p1->player < p2->player )
		return(-1);

	// tie
	return(0);
}

UINT64 get_hash_from_TS(ts_entry *t){
	UINT64 z;
	unsigned int i;

	// set main hash key.
	if( t->player==1 )
		z=player_zorb;// apply this for player==black only.
	else
		z=0;
	// cell values are: 0=empty, 1=pawn, 2=knight, 3=bishop, 4=rook, 5=queen, 6=king. Plus 8 if black(top bit). So, 0-14. Use 1/2 byte.
	for(i=0;i<32;++i){
		if( t->piece[i]&15 )
			z^=zorb[(t->piece[i]&7)-1][(t->piece[i]&15)>>3][i];
		if( t->piece[i]>>4 )
			z^=zorb[((t->piece[i]>>4)&7)-1][t->piece[i]>>7][i];
	}
	return(z);
}

int fmc_p_sort_function_hash(const void *arg1, const void *arg2){
	ts_entry *p1=(ts_entry *)arg1;
	ts_entry *p2=(ts_entry *)arg2;
	
	// first, on hash
	UINT64 h1=get_hash_from_TS(p1);
	UINT64 h2=get_hash_from_TS(p2);
	if( h1 < h2 )
		return(1);
	if( h1 > h2 )
		return(-1);

	// tie
	return(0);
}

static void eliminate_duplicates(void){// eliminate duplicate TS entries. Do not drop anything else.
	FILE *f1=fopen("c://xde//chess//data//TSnew.bin","rb");// main TS file
	unsigned int i0,i,j;
	fread(&i0,sizeof(unsigned int),1,f1);
	ts_entry *ts_all=(ts_entry*)malloc(sizeof(ts_entry)*i0); // storage of ts entries.
	if( ts_all==NULL ) exit(123);
	i0=(unsigned int)fread(ts_all,sizeof(ts_entry),i0,f1);
	fclose(f1);

	// sort TS by position+player+remarks (decr)
	qsort(ts_all,i0,sizeof(ts_entry),pos_sort_function);

	// pass over it and mark dups as dummy[0]=127
	//f1=fopen("c://xde//chess//out//dups.csv","w");
	//fprintf(f1,"FEN,remarks,player,score,i,dup_c\n");
	ts_all[0].dummy[0]=0;
	unsigned int last_deleted=1000000000,dup_c=0;
	for(i=1;i<i0;++i){
		if( ts_all[i].remarks<2 || pos_sort_function0(ts_all+i-1,ts_all+i)==0 ){// dups at i-1 and i. Keep i-1, mark i for deletion.
			//char sss[200];

			// keep this one 
			/*if( last_deleted!=i-1 ){// do not display twice
				convert_TS_to_board(&b_m,ts_all+i-1);
				j=print_position(sss,&b_m);
				sss[j-1]=0;
				if( last_deleted!=100000000 )
					fprintf(f1,"\n"); // new line
				fprintf(f1,"%s,%d,%d,%d,%d,%d\n",sss,ts_all[i-1].remarks,ts_all[i-1].player,ts_all[i-1].score,i-1,dup_c);
			}*/

			// delete this one
			dup_c++;
			ts_all[i].dummy[0]=127;// mark i for deletion
			/*last_deleted=i;
			convert_TS_to_board(&b_m,ts_all+i);
			j=print_position(sss,&b_m);
			sss[j-1]=0;
			fprintf(f1,"%s,%d,%d,%d,%d,%d\n",sss,ts_all[i].remarks,ts_all[i].player,ts_all[i].score,i,dup_c);
			*/
		}else
			ts_all[i].dummy[0]=0;
	}
	//fclose(f1);

	// mark more items for deletion: remarks=0
	/*for(i=0;i<i0;++i){
		if( abs(ts_all[i].remarks)==0 ){
			ts_all[i].dummy[0]=127;
			continue;
		}
		convert_TS_to_board(&b_m,ts_all+i);
		set_bitboards(&b_m);
		if( popcnt64l(b_m.colorBB[0]|b_m.colorBB[1])<7 || popcnt64l(b_m.colorBB[0])<2 || popcnt64l(b_m.colorBB[1])<2 )// exclude <7 pieces, or highly assimetric positions.
			ts_all[i].dummy[0]=127;
	}*/


	// eliminate dups
	for(j=i=0;i<i0;++i){
		if( ts_all[i].dummy[0]==127 ){// dup at i. Replace it with [j], and inc j
			ts_all[i]=ts_all[j];
			j++;
		}
	}
	i=i0-j; // new size - drop j elements
	ts_all+=j; // new start - drop j elements

	// sort TS by hash - to simulate random sorting.
	int k,l,j0=j;
	for(j=k=l=0;j<(int)i;++j){
		int q=quad(ts_all+j); // populate king quadrant
		ts_all[j].dummy[2]=q;
		if( q<k ) l=1; // out of order - sort needed
		k=q; //save as previous result
	}
	if( l ) qsort(ts_all,i,sizeof(ts_entry),pos_sort_function_PST);// sort TS by castling - so that PST does not change (much) between positions, and parallel search will work well.
	qsort(ts_all,i,sizeof(ts_entry),fmc_p_sort_function_hash);
	j=j0;//restore

	// save it
	f1=fopen("c://xde//chess//data//TSnew2.bin","wb");// main TS file
	fwrite(&i,sizeof(unsigned int),1,f1); // write size - i
	fwrite(ts_all,sizeof(ts_entry),i,f1); // data - i
	fclose(f1);

	// display results
	char res[300];
	sprintf(res,"Deleted %d records. New count is %d records.",j,i);
	MessageBox( hWnd_global, res,TEXT("Training Set Dups"),MB_ICONERROR | MB_OK );
	exit(0);
}

typedef struct {
	double w1,w2;
	unsigned char piece[64];// board pieces
	short int resid;		// residual score, +-5000
} b3t;

_declspec(thread) unsigned int use_hash;
_declspec(thread) unsigned int eval_counter;	// count of eval calls
_declspec(thread) board_light *eval_b;			// evaluation boards
_declspec(thread) short int *eval_score;		// evaluation scores
_declspec(thread) int pawn_deriv_coeffs[330];	// here increase size if a lot of pawn coeffs are used. Keep it small to avoid long reset times.

static ts_entry *ts_all;
static unsigned int pos_count0,ii,pos_count1,*coeff_count,pos_cnt_2,pos_cnt;
static double *coeffs,*derivT1,sumsq2_2,sumsq2,d1,d2,*dir;
int ka[2];

static unsigned int get_mat_key2(board *b){// lower of B or W
	unsigned int wN,bN,wB,bB,wR,bR,wQ,bQ,wP,bP,i,j;

	wP=(unsigned int)popcnt64l(b->piececolorBB[0][0]);
	bP=(unsigned int)popcnt64l(b->piececolorBB[0][1]);
	wN=(unsigned int)popcnt64l(b->piececolorBB[1][0]);
	bN=(unsigned int)popcnt64l(b->piececolorBB[1][1]);
	wB=(unsigned int)popcnt64l(b->piececolorBB[2][0]);
	bB=(unsigned int)popcnt64l(b->piececolorBB[2][1]);
	wR=(unsigned int)popcnt64l(b->piececolorBB[3][0]);
	bR=(unsigned int)popcnt64l(b->piececolorBB[3][1]);
	wQ=(unsigned int)popcnt64l(b->piececolorBB[4][0]);
	bQ=(unsigned int)popcnt64l(b->piececolorBB[4][1]);
	i=wP*mat_key_mult[0]+bP*mat_key_mult[1]
		+wN*mat_key_mult[2]+bN*mat_key_mult[3]
		+wB*mat_key_mult[4]+bB*mat_key_mult[5]
		+wR*mat_key_mult[6]+bR*mat_key_mult[7]
		+wQ*mat_key_mult[8]+bQ*mat_key_mult[9];
	j=bP*mat_key_mult[0]+wP*mat_key_mult[1]
		+bN*mat_key_mult[2]+wN*mat_key_mult[3]
		+bB*mat_key_mult[4]+wB*mat_key_mult[5]
		+bR*mat_key_mult[6]+wR*mat_key_mult[7]
		+bQ*mat_key_mult[8]+wQ*mat_key_mult[9];
	return(min(i,j));
}

typedef struct {
	unsigned int a[10];
	int mat;
} ddd2;

void decomp_key(unsigned int key,ddd2* dd){
	// decompose key into components
	unsigned int ii=key;
	dd->a[0]=ii/26244;//bP
	ii-=dd->a[0]*26244;
	dd->a[1]=ii/2916;//wP
	ii-=dd->a[1]*2916;
	dd->a[2]=ii/1458;//bQ
	ii-=dd->a[2]*1458;
	dd->a[3]=ii/729;//wQ
	ii-=dd->a[3]*729;
	dd->a[4]=ii/243;//bR
	ii-=dd->a[4]*243;
	dd->a[5]=ii/81;//wR
	ii-=dd->a[5]*81;
	dd->a[6]=ii/27;//bB
	ii-=dd->a[6]*27;
	dd->a[7]=ii/9;//wB
	ii-=dd->a[7]*9;
	dd->a[8]=ii/3;//bN
	ii-=dd->a[8]*3;
	dd->a[9]=ii/1;//wN
	ii-=dd->a[9]*1;

	// get mat diff
	dd->mat=dd->a[1]-dd->a[0]+9*(dd->a[3]-dd->a[2])+5*(dd->a[5]-dd->a[4])+3*(dd->a[7]-dd->a[6])+3*(dd->a[9]-dd->a[8]);
}

extern unsigned short int index_pawn[];
extern unsigned short int convert2_3[];
extern unsigned short int *ind1;

static void analyze(void){
	// load training set
	FILE *f=fopen("c://xde//chess//data//TSn2.bin","rb");// main TS file, after Qs. Now just call eval on it.
	fread(&pos_count0,sizeof(unsigned int),1,f);
	ts_all=(ts_entry*)malloc(sizeof(ts_entry)*pos_count0); // storage of ts entries - 40 bytes each.
	pos_count0=(unsigned int)fread(ts_all,sizeof(ts_entry),pos_count0,f);
	fclose(f);

	
	// loop over all entries
	UINT64 ss=1000*64;
	unsigned long bit;
	unsigned int ii,i,index,bit2;
	unsigned int *cc=(unsigned int*)malloc(ss*sizeof(unsigned int));
	memset(cc,0,(ss/2)*sizeof(unsigned int));
	memset(&cc[ss/2],0,(ss/2)*sizeof(unsigned int));
	for(ii=0;ii<pos_count0;++ii){
		board_plus ts_b;

		// set board
		convert_TS_to_board(&ts_b.b,ts_all+ii);

		// set bitboards
		set_bitboards(&ts_b.b);

		// set king positions
		_BitScanForward64(&bit,ts_b.b.piececolorBB[5][0]);ts_b.b.kp[0]=(unsigned char)bit;// white
		_BitScanForward64(&bit,ts_b.b.piececolorBB[5][1]);ts_b.b.kp[1]=(unsigned char)bit;// black

		// skip some positions.
		if( popcnt64l(ts_b.b.piececolorBB[4][0])>1 || popcnt64l(ts_b.b.piececolorBB[4][1])>1 )// skip if >1 Q
			continue;
		if( popcnt64l(ts_b.b.piececolorBB[1][0])>2 || popcnt64l(ts_b.b.piececolorBB[1][1])>2 )// skip if >2 N
			continue;
		if( popcnt64l(ts_b.b.piececolorBB[2][0])>2 || popcnt64l(ts_b.b.piececolorBB[2][1])>2 )// skip if >2 B
			continue;
		if( popcnt64l(ts_b.b.piececolorBB[3][0])>2 || popcnt64l(ts_b.b.piececolorBB[3][1])>2 )// skip if >2 R
			continue;
		unsigned int j=(unsigned int)popcnt64l(ts_b.b.colorBB[0]|ts_b.b.colorBB[1]);
		if( j<=5 )// skip if 5 or fewer pieces
			continue;

		board *b=&ts_b.b;

		// get list of pawns
		unsigned int sq[2][8],sq_cnt[2]; //sq: list of all pawns.
		UINT64 w_bb=b->piececolorBB[0][0];//white pawn
		UINT64 b_bb=flip_color(b->piececolorBB[0][1]);//black pawn, flipped so that it is from point of view of white
		sq_cnt[0]=0;
		while( w_bb ){// white
			GET_BIT(w_bb)
			sq[0][sq_cnt[0]++]=bit; // record position of this pawn
		}
		sq_cnt[1]=0;
		while( b_bb ){// black
			GET_BIT(b_bb)
			sq[1][sq_cnt[1]++]=bit; // record position of this pawn - in flipped format(white's point of view)
		}

		// my pawn vs my pawn - new index development logic, with look-up table.
		unsigned int i1,i2,f;
		for(i=0;i+1<sq_cnt[0];++i){
			bit=sq[0][i];
			for(j=i+1;j<sq_cnt[0];++j){// loop over P2, P2>P1
				bit2=sq[0][j];
				index=index_pawn[bit*64+bit2]; // 0-695
				i1=min(bit,bit2)+max(bit,bit2)*(max(bit,bit2)-1)/2;
				i2=min(flips[bit][2],flips[bit2][2])+max(flips[bit][2],flips[bit2][2])*(max(flips[bit][2],flips[bit2][2])-1)/2;
				if( i2<i1 ) f=2;else f=0; // flip

				// look at friendly king
				i1=flips[ts_b.b.kp[0]][f];
				index=index*64+i1; // 
				cc[index]++;
			}
		}
		for(i=0;i+1<sq_cnt[1];++i){// all bits are already from white's POV
			bit=sq[1][i];
			for(j=i+1;j<sq_cnt[1];++j){// loop over P2, P2>P1
				bit2=sq[1][j];
				index=index_pawn[bit*64+bit2]; // 0-695
				i1=min(bit,bit2)+max(bit,bit2)*(max(bit,bit2)-1)/2;
				i2=min(flips[bit][2],flips[bit2][2])+max(flips[bit][2],flips[bit2][2])*(max(flips[bit][2],flips[bit2][2])-1)/2;
				if( i2<i1 ) f=3;else f=1; // flip

				// look at friendly king
				i1=flips[ts_b.b.kp[1]][f];
				index=index*64+i1; // 
				cc[index]++;
			}
		}
	}

	// save it, for review only
	f=fopen("c://xde//chess//out//tt.csv","w");
	fprintf(f,"key,k1,k2,count\n");
	for(i=0;i<ss;++i){
		if( cc[i]==0 ) continue;
		fprintf(f,"%u,%u,%u,%u\n",i,(i%65536),(i/65536),cc[i]);
	}
	fclose(f);
	exit(0);
}


#define coeff_num (12+672+576*2+24*64*10*2+2048*2+3321*2) // number of regression coefficients. 43,294

static Spinlock lll;		// spinlock

typedef struct {
	float d1; // d o / d eval
	float d2; // d2 o / d eval / d eval
	float eval;
	float A;
} regr_t;

typedef struct {
	float eval0;
	float nn;
	float deep;
	float egw;
} regr2_t;

UINT64 d1d_o; // offset into data
UINT64 *d1i; // offset to start
unsigned int *d1s; // size of each entry
short unsigned int *d1da;
unsigned char *d1db;
regr_t *rd; // regression data
regr2_t *rd2; // regression data v2
unsigned int pass;

static int __cdecl c3(const void *key, const void *datum){
	if( *((unsigned int*)key)==*((unsigned int*)datum) )
		return(0);
	else if( *((unsigned int*)key)<*((unsigned int*)datum) )
		return(-1);
	else
		return(1);
}

#define obj_type 0 // 0=square of actual-expected scores; 1=cross-entropy
void obj(double s0,double deep,double *res){// objective function. Return o, o', o''.
	#if obj_type==0 // o=(A-S)^X
		double z=s0/173.72;
		double ez=exp(-z);
		double E=1./(1.+ez); // expected score, sigmoid, based on 10^-x/400. Range is 0 to 1.
		double y=(min(1,max(-1,deep))+1)/2; // 0-0.5-1
		
		res[0]=100000000.*(E-y)*(E-y); // o
		res[1]=100000000./173.72*2.*(E-y)*E*(1.-E); // o'
		res[2]=100000000./173.72/173.72*2.*E*(1.-E)*(2.*E-3.*E*E-y+2.*E*y); // o''
	#endif
	#if obj_type==1 // o=-y*ln(a)-(1-y)*ln(1-a)=ln(1+exp(-z))+z*(1-y)
		double z=s0/173.72;
		double y=(min(1,max(-1,deep))+1)/2; // 0-0.5-1
		double ez=exp(-z);
		double v=log(1+ez)+z*(1-y);
		double E=1./(1.+ez);

		res[0]=10000000.*v; // o
		res[1]=10000000./173.72*(E-y); // o'
		res[2]=10000000./173.72/173.72*E*(1.-E); // o''
	#endif
}

static unsigned int process_position(unsigned int i,double *d,unsigned int *ind2l){// return number of coeffs. And board_plus, by pointer.
	board_plus ts_b;
	DWORD bit;
	unsigned int j,k,coeff_num2,ind[1000];

	// skip unsolved. This comes into effect.
	if( ts_all[i].remarks<2 ) return(0);

	// set board
	convert_TS_to_board(&ts_b.b,ts_all+i);

	// set bitboards
	set_bitboards(&ts_b.b);

	// Get midgame weight
	ts_b.mgw=1.-endgame_weight_all_i[get_piece_value(&ts_b.b)]/1024.;

	// save score into TS
	ts_b.score_deep=ts_all[i].score;

	// set king positions
	_BitScanForward64(&bit,ts_b.b.piececolorBB[5][0]);ts_b.b.kp[0]=(unsigned char)bit;// white
	_BitScanForward64(&bit,ts_b.b.piececolorBB[5][1]);ts_b.b.kp[1]=(unsigned char)bit;// black

	if( pass&1 ){
		ts_b.score_shallow=(short)rd[i].eval; // restore
	}else{
		// init PST scores
		get_scores(&ts_b.b);

		// get material key
		ts_b.b.mat_key=get_mat_key(&ts_b.b);

		// get pawn hash. For first pass only.
		if( pass==0) ts_b.b.pawn_hash_key=get_pawn_hash_key(&ts_b.b); // this is currently needed. But why?

		// init search
		eval_counter=0; // reset count
		use_hash=1; // has to be 1.
		ts_b.b.em_break=0; // reset
		ts_b.b.slave_index=0;
	
		ts_b.score_shallow=eval(&ts_b.b); // call eval directly
		rd[i].eval=ts_b.score_shallow; //save
	}
	if( abs(ts_b.score_shallow)>2000 ) // skip if search sees a checkmate. This comes into effect.
		return(0);

	// skip some positions.
	if( popcnt64l(ts_b.b.colorBB[0]|ts_b.b.colorBB[1])<=5 )// skip if 5 or fewer pieces
		return(0);

	if( fabs(ts_b.mgw)<0.001 ) ts_b.mgw=0.; // "round" to 0.001
	double w1=ts_b.mgw*(ts_b.b.player==1?1.:-1.);// midgame weight, accounting for side to move
	double w2=(1.-ts_b.mgw)*(ts_b.b.player==1?1.:-1.);// endgame weight, accounting for side to move
	double w12=(ts_b.b.player==1?1.:-1.);// +-1, accounting for side to move
	
	// use coeffs as already calculated on first iteration
	if( pass ){
		unsigned int cn3=d1s[i],offset=0;
		UINT64 o1=d1i[i];

		for(j=0;j<cn3;++j){
			if( j && d1da[o1+j]<d1da[o1+j-1] )// if index decreases, add 2^16 to offset
				offset+=65536;
			ind2l[j]=((unsigned int)d1da[o1+j])+offset;
			double v;
			unsigned int x=d1db[o1+j];

			if( x<49 ){
				// get type: w1/w2/w3
				unsigned int x3=(x-1)%3;
				if( x3==0 ) v=w1;
				else if( x3==1 ) v=w2;
				else v=w12;

				// get multiple: +-1-8
				x3=(x-1)/3;
				static const int m[]={1,-1,2,-2,3,-3,4,-4,5,-5,6,-6,7,-7,8,-8};
				v*=m[x3];
			}else if( x<24+58 )// KS, positive
				v=w12/32.*(x-24-25);
			else// KS, negative
				v=-w12/32.*(x-24-58);
			
			d[j]=v;
		}
		return(cn3);
	}

	// first pass - format true score
	rd[i].A=ts_b.score_deep;

	memset(pawn_deriv_coeffs,0,sizeof(pawn_deriv_coeffs));// reset: 330*4 bytes
	use_hash=0;
	pawn_score(&ts_b.b);//*********************************************************************************************************************************************
	use_hash=1;

	j=0;// coeff counter - init
	// material
	for(k=0;k<6;++k){
		int cc=int(popcnt64l(ts_b.b.piececolorBB[k][0]))-int(popcnt64l(ts_b.b.piececolorBB[k][1]));
		d[j+k]=w1*cc;
		d[j+k+6]=w2*cc;
	}
	j+=12;

	// isolated pawn
	d[j++]=pawn_deriv_coeffs[1]*w1;
	d[j++]=pawn_deriv_coeffs[1]*w2;
		
	// passed pawns, on ranks 2-6=5*2=10, m+e
	for(k=0;k<5;++k){
		d[j+k*2]=pawn_deriv_coeffs[2+k]*w1;// midgame
		d[j+k*2+1]=pawn_deriv_coeffs[2+k]*w2;// endgame
	}
	j+=10;

	// passed protected pawn
	d[j++]=pawn_deriv_coeffs[8]*w1;
	d[j++]=pawn_deriv_coeffs[8]*w2;


	//knight PST, 32 cells, mid+end.
	UINT64 bb;
	bb=ts_b.b.piececolorBB[1][0];
	while( bb ){// loop over white knights
		GET_BIT(bb)
		if( bit>=32 )
			bit=(bit&7)+((7-(bit>>3))<<3);// turn into 0-31
		d[j+bit]+=w1;
		d[j+bit+32]+=w2;
	}
	bb=ts_b.b.piececolorBB[1][1];
	while( bb ){// loop over black knights
		GET_BIT(bb)
		bit=(7-(bit&7))+(bit&56);// convert bit to black format, flip 0-7
		if( bit>=32 )
			bit=(bit&7)+((7-(bit>>3))<<3);// turn into 0-31
		d[j+bit]-=w1;
		d[j+bit+32]-=w2;
	}
	j+=64;
		

	//bishop PST, 32 cells, mid+end.
	bb=ts_b.b.piececolorBB[2][0];
	while( bb ){// loop over white bishops
		GET_BIT(bb)
		if( bit>=32 )
			bit=(bit&7)+((7-(bit>>3))<<3);// turn into 0-31
		d[j+bit]+=w1;
		d[j+bit+32]+=w2;
	}
	bb=ts_b.b.piececolorBB[2][1];
	while( bb ){// loop over black bishops
		GET_BIT(bb)
		bit=(7-(bit&7))+(bit&56);// convert bit to black format, flip 0-7
		if( bit>=32 )
			bit=(bit&7)+((7-(bit>>3))<<3);// turn into 0-31
		d[j+bit]-=w1;
		d[j+bit+32]-=w2;
	}
	j+=64;


	//rook PST, 32 cells, mid+end.
	bb=ts_b.b.piececolorBB[3][0];
	while( bb ){// loop over white rooks
		GET_BIT(bb)
		if( bit>=32 )
			bit=(bit&7)+((7-(bit>>3))<<3);// turn into 0-31
		d[j+bit]+=w1;
		d[j+bit+32]+=w2;
	}
	bb=ts_b.b.piececolorBB[3][1];
	while( bb ){// loop over black rooks
		GET_BIT(bb)
		bit=(7-(bit&7))+(bit&56);// convert bit to black format, flip 0-7
		if( bit>=32 )
			bit=(bit&7)+((7-(bit>>3))<<3);// turn into 0-31
		d[j+bit]-=w1;
		d[j+bit+32]-=w2;
	}
	j+=64;


	//queen PST, 32 cells, mid+end.
	bb=ts_b.b.piececolorBB[4][0];
	while( bb ){// loop over white queens
		GET_BIT(bb)
		if( bit>=32 )
			bit=(bit&7)+((7-(bit>>3))<<3);// turn into 0-31
		d[j+bit]+=w1;
		d[j+bit+32]+=w2;
	}
	bb=ts_b.b.piececolorBB[4][1];
	while( bb ){// loop over black queens
		GET_BIT(bb)
		bit=(7-(bit&7))+(bit&56);// convert bit to black format, flip 0-7
		if( bit>=32 )
			bit=(bit&7)+((7-(bit>>3))<<3);// turn into 0-31
		d[j+bit]-=w1;
		d[j+bit+32]-=w2;
	}
	j+=64;


	//king PST, 32 cells, mid+end.
	bit=ts_b.b.kp[0]; // white king
	if( bit>=32 ) bit=(bit&7)+((7-(bit>>3))<<3);// turn into 0-31
	if( (bit&7)<3 ) // only apply midgame for ranks 1-3
		d[j+bit]+=w1;
	else			// put the rest in cell 4
		d[j+4]+=w1;
	d[j+bit+32]+=w2;
	bit=ts_b.b.kp[1]; // black king
	bit=(7-(bit&7))+(bit&56);// convert bit to black format, flip 0-7
	if( bit>=32 ) bit=(bit&7)+((7-(bit>>3))<<3);// turn into 0-31
	if( (bit&7)<3 ) // only apply midgame for ranks 1-3
		d[j+bit]-=w1;
	else			// put the rest in cell 4
		d[j+4]-=w1;
	d[j+bit+32]-=w2;
	j+=64;
	

	//pawn PST, 32 cells, mid+1/2 mid noking+end.
	bb=ts_b.b.piececolorBB[0][0];
	while( bb ){// loop over white pawns
		GET_BIT(bb)
		if( bit>=32 )
			bit=(bit&7)+((7-(bit>>3))<<3);// turn into 0-31
		d[j+bit]+=w1;
		d[j+bit+32]+=w2;
	}
	bb=ts_b.b.piececolorBB[0][1];
	while( bb ){// loop over black pawns
		GET_BIT(bb)
		bit=(7-(bit&7))+(bit&56);// convert bit to white format, flip 0-7
		if( bit>=32 )
			bit=(bit&7)+((7-(bit>>3))<<3);// turn into 0-31
		d[j+bit]-=w1;
		d[j+bit+32]-=w2;
	}
	j+=64;// mid+end


	// 23-28=candidate passed pawns, on ranks 2-6. 5 of them.
	for(k=0;k<5;++k){
		d[j+k*2]=pawn_deriv_coeffs[23+k]*w1;// midgame
		d[j+k*2+1]=pawn_deriv_coeffs[23+k]*w2;// endgame
	}
	j+=5*2;


	// insert additional pawn coeff code here**********************************************************************************************************************************
	int pawn_deriv_coeffs_l[330];
	memcpy(pawn_deriv_coeffs_l,pawn_deriv_coeffs,sizeof(pawn_deriv_coeffs));// save: 330*4 bytes
				
	// get eval coeffs
	memset(pawn_deriv_coeffs,0,sizeof(pawn_deriv_coeffs));// reset: 330*4 bytes
	use_hash=0;
	int score=eval(&ts_b.b);// eval coeffs************************************************************************************************************************************************
	use_hash=1;

	// knight mob, 9 of them.
	for(k=0;k<9;++k){
		d[j+k*2]+=pawn_deriv_coeffs[0+k]*w1;// midgame.
		d[j+k*2+1]+=pawn_deriv_coeffs[0+k]*w2;// endgame.
	}
	j+=9*2;
	
	// bishop mob, 14 of them.
	for(k=0;k<14;++k){
		d[j+k*2]+=pawn_deriv_coeffs[9+k]*w1;// midgame.
		d[j+k*2+1]+=pawn_deriv_coeffs[9+k]*w2;// endgame.
	}
	j+=14*2;
	
	// rook mob, 15 of them.
	for(k=0;k<15;++k){
		d[j+k*2]+=pawn_deriv_coeffs[23+k]*w1;// midgame.
		d[j+k*2+1]+=pawn_deriv_coeffs[23+k]*w2;// endgame.
	}
	j+=15*2;
	
	// queen mob, 28 of them.
	for(k=0;k<28;++k){
		d[j+k*2]+=pawn_deriv_coeffs[38+k]*w1;// midgame.
		d[j+k*2+1]+=pawn_deriv_coeffs[38+k]*w2;// endgame.
	}
	j+=28*2;
		
	// knight outposts, 22 of them. 11 base, 11 bonuses for no opp minors.
	for(k=0;k<22;++k) d[j++]=pawn_deriv_coeffs[230+k]*w1; // midgame only

	// rook pin - 1+1. Midgame only
	d[j++]=pawn_deriv_coeffs[88]*w1; // Q
	d[j++]=pawn_deriv_coeffs[183]*w1; // K
				
	// king safety - 62. Turn them into 13 - 1 every 5, interpolate in between.
	static const unsigned int kstr[]={0,0,0,0,0,1,1,1,1,1,2,2,2,2,2,3,3,3,3,3,4,4,4,4,4,5,5,5,5,5,6,6,6,6,6,7,7,7,7,7,8,8,8,8,8,9,9,9,9,9,10,10,10,10,10,11,11,11,11,11,11,12};
	for(k=0;k<62;++k) d[j+kstr[k]]+=pawn_deriv_coeffs[99+k]*w12/32.;// always need /32 here.Apply to both mid and end game
	j+=62;

	// kings close to passed pawn (16)
	for(k=0;k<16;++k) d[j++]=pawn_deriv_coeffs[161+k]*w12;
		
	// unstoppable passed pawn (1)
	d[j++]=pawn_deriv_coeffs[177]*w12;

	// penalty for no pawns (1)
	d[j++]=((ts_b.b.piececolorBB[0][1]==0)-(ts_b.b.piececolorBB[0][0]==0))*w12;

	// penalty for no mating potential in pieces (1)
	if( !(ts_b.b.piececolorBB[3][0]|ts_b.b.piececolorBB[4][0]) && ( !ts_b.b.piececolorBB[2][0] || popcnt64l(ts_b.b.piececolorBB[2][0]|ts_b.b.piececolorBB[1][0])<2 ) ) d[j]-=w12;
	if( !(ts_b.b.piececolorBB[3][1]|ts_b.b.piececolorBB[4][1]) && ( !ts_b.b.piececolorBB[2][1] || popcnt64l(ts_b.b.piececolorBB[2][1]|ts_b.b.piececolorBB[1][1])<2 ) ) d[j]+=w12;
	j++;

	// bonus for bishop pair (1)
	d[j++]+=((popcnt64l(ts_b.b.piececolorBB[2][0])>1)-(popcnt64l(ts_b.b.piececolorBB[2][1])>1))*w12;
		
	// bishop pin - 1+1. (4)
	d[j++]=pawn_deriv_coeffs[184]*w1; // Q
	d[j++]=pawn_deriv_coeffs[184]*w2; // Q
	d[j++]=pawn_deriv_coeffs[185]*w1; // K
	d[j++]=pawn_deriv_coeffs[185]*w2; // K

	// kings close to passed pawn (16)
	for(k=0;k<16;++k) d[j++]+=pawn_deriv_coeffs[186+k]*w12;
		
	// king mob=4
	for(k=0;k<4;++k) d[j++]+=pawn_deriv_coeffs[203+k]*w2;

	// rook protecting other rook
	d[j++]=pawn_deriv_coeffs[207]*w1;
	d[j++]=pawn_deriv_coeffs[207]*w2;

	// get a list of non-empty coeffs******************************************************************
	coeff_num2=0;// loop over 746 coeffs
	for(UINT64 kk=0;kk<j;++kk){// this is slightly faster than conditional expression. Using UINT64 helps. This is the slowest part.
		ind[coeff_num2]=(unsigned int)kk;
		coeff_num2+=((((unsigned int*)d)[kk*2+1]&0x7ff00000)!=0);
	}


	// large pattern #1: my pawns vs my pawns
	bb=ts_b.b.piececolorBB[0][0];
	unsigned long bit2;
	while( bb ){// loop over P1
		GET_BIT(bb)
		UINT64 bb2=bb;
		while( bb2 ){// loop over P2, P2>P1
			GET_BIT2(bb2)
			unsigned int index=index_pawn[bit*64+bit2];
			assert(index<576);
			d[j+index*2]+=w1;
			d[j+index*2+1]+=w2;
			ind[coeff_num2++]=j+index*2;
			ind[coeff_num2++]=j+index*2+1;
		}
	}
	bb=ts_b.b.piececolorBB[0][1];
	while( bb ){// loop over P1
		GET_BIT(bb)
		bit=flips[bit][1]; // change from black to white
		UINT64 bb2=bb;
		while( bb2 ){// loop over P2, P2>P1
			GET_BIT2(bb2)
			bit2=flips[bit2][1]; // change from black to white
			unsigned int index=index_pawn[bit*64+bit2];
			assert(index<576);
			d[j+index*2]-=w1;
			d[j+index*2+1]-=w2;
			ind[coeff_num2++]=j+index*2;
			ind[coeff_num2++]=j+index*2+1;
		}
	}
	j+=576*2;


	// K/N/B/R/Q vs pawns, w+b(10)
	// get the list of all pawns
	unsigned int pw=0,pb=0,pawns_w[8],pawns_b[8];
	bb=ts_b.b.piececolorBB[0][0];
	while( bb ){
		GET_BIT(bb)
		pawns_w[pw++]=bit;
	}
	bb=ts_b.b.piececolorBB[0][1];
	while( bb ){
		GET_BIT(bb)
		pawns_b[pb++]=bit;
	}
	
	// loop over all pieces
	unsigned int index,s;
	bb=(ts_b.b.colorBB[0]|ts_b.b.colorBB[1])^ts_b.b.piececolorBB[0][0]^ts_b.b.piececolorBB[0][1]; // excl P
	while( bb ){
		GET_BIT(bb)
		unsigned int color=ts_b.b.piece[bit]>>7;	// 0/1
		unsigned int type=ts_b.b.piece[bit]&7;		// 1-6
		s=(bit>>4)&2; // 0/2 - none vs l-r
		for(k=0;k<pw;++k){// loop over W pawns
			index=2*(48*(flips[bit][s]*10+(type-2+color*5))+flips[pawns_w[k]][s+4]);
			assert(index<1536*10*2);
			//if( ts_b.b.player==2 ) index+=30720; // side-to-move adj: if not white, incr index
			d[j+index]+=w1;
			d[j+index+1]+=w2;
			ind[coeff_num2++]=j+index;
			ind[coeff_num2++]=j+index+1;
		}
		s++; // add w/b flip
		for(k=0;k<pb;++k){// loop over B pawns
			index=2*(48*(flips[bit][s]*10+(type-2+5-color*5))+flips[pawns_b[k]][s+4]);
			assert(index<1536*10*2);
			//if( ts_b.b.player==1 ) index+=30720; // side-to-move adj: if not black, incr index
			d[j+index]-=w1;
			d[j+index+1]-=w2;
			ind[coeff_num2++]=j+index;
			ind[coeff_num2++]=j+index+1;
		}
	}
	j+=1536*10*2;  // 10 large patterns of 24*64=1536
	

	// Nb patterns
	// flips: 0=as is, 1=w-B, 2=l-r, 3=w-b and l-r, 4=to 48, 5=to 48 w-b, 6=to 48 l-r, 7=to 48 w-b and l-r
	bb=ts_b.b.piececolorBB[1][0];
	while( bb ){// W N
		GET_BIT(bb)

		// N opp B patterns
		UINT64 bb_2=ts_b.b.piececolorBB[2][1];
		while( bb_2 ){// loop over black bishops
			GET_BIT2(bb_2)
			unsigned int index;
			if( bit<32 ) index=bit+bit2*32;
			else index=flips[bit][2]+flips[bit2][2]*32;// l-r only
			d[j+index*2]+=w1;
			d[j+index*2+1]+=w2;
			ind[coeff_num2++]=j+index*2;
			ind[coeff_num2++]=j+index*2+1;
		}
	}
	bb=ts_b.b.piececolorBB[1][1];
	while( bb ){// B N
		GET_BIT(bb)

		// N opp B patterns
		UINT64 bb_2=ts_b.b.piececolorBB[2][0];
		while( bb_2 ){// loop over white bishops
			GET_BIT2(bb_2)
			unsigned int index;
			if( bit<32 ) index=flips[bit][1]+flips[bit2][1]*32;// w-b only
			else index=flips[bit][3]+flips[bit2][3]*32;// w-b and l-r
			d[j+index*2]-=w1;
			d[j+index*2+1]-=w2;
			ind[coeff_num2++]=j+index*2;
			ind[coeff_num2++]=j+index*2+1;
		}
	}
	j+=2048*2; // Nb patterns


	// rank 1, W+2*B
	// first, need to turn the board: make bits 0/8/16/24/32/40/48/56 into bits 0/1/2/3/4/5/6/7
	bb=(ts_b.b.colorBB[0]&0x101010101010101)*0x0102040810204080;
	UINT64 bb_2=(ts_b.b.colorBB[1]&0x101010101010101)*0x0102040810204080;
	index=convert2_3[bb>>56]+2*convert2_3[bb_2>>56]; // 6561
	index=ind1[index];
	d[j+index*2]+=w1;
	d[j+index*2+1]+=w2;
	ind[coeff_num2++]=j+index*2;
	ind[coeff_num2++]=j+index*2+1;

	// rank 8, B+2*W
	// first, need to turn the board: make bits 7+0/8/16/24/32/40/48/56 into bits 0/1/2/3/4/5/6/7
	bb=((ts_b.b.colorBB[1]>>7)&0x101010101010101)*0x0102040810204080;
	bb_2=((ts_b.b.colorBB[0]>>7)&0x101010101010101)*0x0102040810204080;
	index=convert2_3[bb>>56]+2*convert2_3[bb_2>>56]; // 6561
	index=ind1[index];
	d[j+index*2]-=w1;
	d[j+index*2+1]-=w2;
	ind[coeff_num2++]=j+index*2;
	ind[coeff_num2++]=j+index*2+1;
	j+=2*3321;

	// get a list of non-empty coeffs - second pass. This has very small impact on results, but also does not take much time.
	unsigned int cn3=0;
	assert(coeff_num2<1000);
	for(unsigned int kk=0;kk<coeff_num2;++kk){
		unsigned int l=ind[kk];
		ind2l[cn3]=l;										// list of coeffs used. Returned. With cn3 and d.
		cn3+=((((unsigned int*)d)[l*2+1]&0x7ff00000)!=0);	// skip empties
	}
	if( cn3==0 ){
		d1i[i]=d1d_o;
		d1s[i]=cn3;
		return(0);
	}

	// sort ind2l to make it ascending.
	#define tol1 1e-6
	qsort(ind2l,cn3,4,c3);
	// and eliminate duplicates.
	assert( fabs(d[ind2l[0]])>tol1 );
	memcpy(ind,ind2l,cn3*4);
	k=1;
	for(j=1;j<cn3;++j){
		if( ind[j]!=ind[j-1] ){
			assert( fabs(d[ind[j]])>tol1 );
			ind2l[k]=ind[j];
			k++;
		}
	}
	cn3=k;

	// format results for saving
	unsigned short int d1dal[1000];
	unsigned char d1dbl[1000];
	// type: 0-empty, 1/2/3=w1/w2/w12, increased by 3*x, where x is: 1=0,-1=1,2=2,-2=3,3=4,-3=5,4=6,-4=7,5=8,-5=9. This goes to 30.
	// Then KS: w12/32*X, x=0 to 32: 25 to 25+32=57. Then negative KS, 58 to 90.
	for(j=0;j<cn3;++j){
		double v=d[ind2l[j]];
		if( fabs(v-w1)<tol1 ) d1dbl[j]=1;
		else if( fabs(v-w2)<tol1 ) d1dbl[j]=2;
		else if( fabs(v-w12)<tol1 ) d1dbl[j]=3;
		else if( fabs(v+w1)<tol1 ) d1dbl[j]=4;
		else if( fabs(v+w2)<tol1 ) d1dbl[j]=5;
		else if( fabs(v+w12)<tol1 ) d1dbl[j]=6;
		else if( fabs(v-2*w1)<tol1 ) d1dbl[j]=7;
		else if( fabs(v-2*w2)<tol1 ) d1dbl[j]=8;
		else if( fabs(v-2*w12)<tol1 ) d1dbl[j]=9;
		else if( fabs(v+2*w1)<tol1 ) d1dbl[j]=10;
		else if( fabs(v+2*w2)<tol1 ) d1dbl[j]=11;
		else if( fabs(v+2*w12)<tol1 ) d1dbl[j]=12;
		else if( fabs(v-3*w1)<tol1 ) d1dbl[j]=13;
		else if( fabs(v-3*w2)<tol1 ) d1dbl[j]=14;
		else if( fabs(v-3*w12)<tol1 ) d1dbl[j]=15;
		else if( fabs(v+3*w1)<tol1 ) d1dbl[j]=16;
		else if( fabs(v+3*w2)<tol1 ) d1dbl[j]=17;
		else if( fabs(v+3*w12)<tol1 ) d1dbl[j]=18;
		else if( fabs(v-4*w1)<tol1 ) d1dbl[j]=19;
		else if( fabs(v-4*w2)<tol1 ) d1dbl[j]=20;
		else if( fabs(v-4*w12)<tol1 ) d1dbl[j]=21;
		else if( fabs(v+4*w1)<tol1 ) d1dbl[j]=22;
		else if( fabs(v+4*w2)<tol1 ) d1dbl[j]=23;
		else if( fabs(v+4*w12)<tol1 ) d1dbl[j]=24;
		else if( fabs(v-5*w1)<tol1 ) d1dbl[j]=25;
		else if( fabs(v-5*w2)<tol1 ) d1dbl[j]=26;
		else if( fabs(v-5*w12)<tol1 ) d1dbl[j]=27;
		else if( fabs(v+5*w1)<tol1 ) d1dbl[j]=28;
		else if( fabs(v+5*w2)<tol1 ) d1dbl[j]=29;
		else if( fabs(v+5*w12)<tol1 ) d1dbl[j]=30;
		else if( fabs(v-6*w1)<tol1 ) d1dbl[j]=31;
		else if( fabs(v-6*w2)<tol1 ) d1dbl[j]=32;
		else if( fabs(v-6*w12)<tol1 ) d1dbl[j]=33;
		else if( fabs(v+6*w1)<tol1 ) d1dbl[j]=34;
		else if( fabs(v+6*w2)<tol1 ) d1dbl[j]=35;
		else if( fabs(v+6*w12)<tol1 ) d1dbl[j]=36;
		else if( fabs(v-7*w1)<tol1 ) d1dbl[j]=37;
		else if( fabs(v-7*w2)<tol1 ) d1dbl[j]=38;
		else if( fabs(v-7*w12)<tol1 ) d1dbl[j]=39;
		else if( fabs(v+7*w1)<tol1 ) d1dbl[j]=40;
		else if( fabs(v+7*w2)<tol1 ) d1dbl[j]=41;
		else if( fabs(v+7*w12)<tol1 ) d1dbl[j]=42;
		else if( fabs(v-8*w1)<tol1 ) d1dbl[j]=43;
		else if( fabs(v-8*w2)<tol1 ) d1dbl[j]=44;
		else if( fabs(v-8*w12)<tol1 ) d1dbl[j]=45;
		else if( fabs(v+8*w1)<tol1 ) d1dbl[j]=46;
		else if( fabs(v+8*w2)<tol1 ) d1dbl[j]=47;
		else if( fabs(v+8*w12)<tol1 ) d1dbl[j]=48;
		else if(  v*w12>0 && fabs(v*32/w12-int(tol1+v*32/w12))<tol1 ) d1dbl[j]=24+25+int(tol1+v*32/w12);
		else if(  v*w12<0 && fabs(-v*32/w12-int(tol1-v*32/w12))<tol1 ) d1dbl[j]=24+58+int(tol1-v*32/w12);
		else{
			assert(0);
			exit(5);
		}
		d1dal[j]=ind2l[j]; // here i squize 4 byte into 2 byte
	}
	// save the results
	lll.acquire();		// lock the memory. This almost never matters, since it is outside the loop.
	d1i[i]=d1d_o;
	assert(cn3<65536); // to make sure this fits into 16 bit var
	d1s[i]=cn3;
	memcpy(d1da+d1d_o,d1dal,sizeof(unsigned short int)*cn3);
	memcpy(d1db+d1d_o,d1dbl,sizeof(unsigned char)*cn3);
	d1d_o+=cn3;
	lll.release();		// release the memory


	return(cn3);
}

#define excl_res 3
static DWORD WINAPI thread_function_0(PVOID p){// 1 thread loops over positions. Pass 0 - init the coeffs
	double *d=(double*)malloc((coeff_num+10)*sizeof(double));
	unsigned int i,j,coeff_num2;
	unsigned int ind[10000]; // may need to extend this

	if( d==NULL ) exit(123);
	memset(d,0,sizeof(double)*(coeff_num+10)); // reset "d" here.
	while(1){
		i=InterlockedExchangeAdd((LONG*)&ii,1); // this is equivalent to locked "i=ii;ii++;", but is a lot faster with many threads.
		if( i>=pos_count0 ) break;	// exit
		if( (coeff_num2=process_position(i,d,ind))==0 ) continue;
		
		for(j=0;j<coeff_num2;++j)
			d[ind[j]]=0;// reset all used elements of "d" here.*********************************************************************
	}// end of main loop*******************************************************************************
	free(d);
	return(0);
}

static DWORD WINAPI thread_function_1(PVOID p){// 1 thread loops over positions. Pass 1.
	double sumsq2_l,sumsq2_2_l;
	double *d=(double*)malloc((coeff_num+10)*sizeof(double));
	double *derivT1_l=(double*)malloc(sizeof(double)*coeff_num);
	unsigned int *coeff_count_l=(unsigned int*)malloc(coeff_num*sizeof(unsigned int));
	unsigned int i,j,coeff_num2,pos_cnt_2_l,pos_cnt_l;
	unsigned int ind[10000]; // may need to extend this

	pos_cnt_2_l=pos_cnt_l=0;
	sumsq2_2_l=sumsq2_l=0.;
	if( d==NULL || derivT1_l==NULL || coeff_count_l==NULL ) exit(123);
	memset(d,0,sizeof(double)*(coeff_num+10)); // reset "d" here.
	memset(coeff_count_l,0,coeff_num*sizeof(unsigned int));	
	memset(derivT1_l,0,coeff_num*sizeof(double));	
	while(1){
		i=InterlockedExchangeAdd((LONG*)&ii,1); // this is equivalent to locked "i=ii;ii++;", but is a lot faster with many threads.
		if( i>=pos_count0 ) break;	// exit
		if( (coeff_num2=process_position(i,d,ind))==0 ) continue;
		
		// first loop: get adjusted residual
		double y2=rd[i].eval;// eval
		for(j=0;j<coeff_num2;++j)// get y2=y+sum coeff*d. This is y for current set of coeffs
			y2+=d[j]*coeffs[ind[j]];
		rd[i].eval=(float)y2;// corrected eval, for non-integer coeffs, to be used in second pass.
		

		// new way to get derivatives of objective function
		double res[3];
		obj(y2,double(rd[i].A),res);
		rd[i].d1=float(res[1]);// first derivative
		rd[i].d2=float(res[2]);// second derivative
		

		// second set
		if( (i%5)==excl_res ){
			sumsq2_2_l+=res[0];// objective function
			pos_cnt_2_l++;// count
		}else{
			sumsq2_l+=res[0];// objective function
			pos_cnt_l++;// count
	
			// second loop: calculate derivative, for first set only
			y2=rd[i].d1;
			for(j=0;j<coeff_num2;++j){
				unsigned int jj=ind[j];
				coeff_count_l[jj]++;// count how often each coeff occurs
				derivT1_l[jj]+=y2*d[j];
			}
		}
	}// end of main loop*******************************************************************************
	lll.acquire();		// lock the memory. This almost never matters, since it is outside the loop.
	for(j=0;j<coeff_num;++j){
		derivT1[j]+=derivT1_l[j];
		coeff_count[j]+=coeff_count_l[j];
	}
	pos_cnt+=pos_cnt_l;
	pos_cnt_2+=pos_cnt_2_l;
	sumsq2+=sumsq2_l;
	sumsq2_2+=sumsq2_2_l;
	lll.release();		// release the memory
	free(d);
	free(derivT1_l);
	free(coeff_count_l);
	return(0);
}

static DWORD WINAPI thread_function_2(PVOID p){// 1 thread loops over positions. Pass 2.
	double d1_l=0.,d2_l=0.;
	double *d=(double*)malloc((coeff_num+10)*sizeof(double));
	unsigned int i,j,coeff_num2;
	unsigned int ind[10000]; // may need to extend this
	
	if( d==NULL ) exit(123);
	memset(d,0,sizeof(double)*(coeff_num+10)); // reset "d" here.
	while(1){
		i=InterlockedExchangeAdd((LONG*)&ii,1); // this is equivalent to locked "i=ii;ii++;", but is a lot faster with many threads.
		if( i>=pos_count0 ) break;	// exit
		if( (i%5)==excl_res ) continue;	//second set - skip
		if( (coeff_num2=process_position(i,d,ind))==0 ) continue;
		
		double ty=0.;
		for(j=0;j<coeff_num2;++j)
			ty+=d[j]*dir[ind[j]];
		d1_l+=ty*rd[i].d1;// first directional derivative.
		d2_l+=ty*ty*rd[i].d2;// second directional derivative
	}// end of main loop*********************************************************************************
	lll.acquire();	// lock the memory. This almost never matters, since it is outside the loop.
	d1+=d1_l;
	d2+=d2_l;
	lll.release();	// release the memory
	free(d);
	return(0);
}

#define calc_threads 12 // here 18 threads is 1.17 performance
extern short int adj[];
static void run_over_TS_v2(void){
	FILE *f;
	derivT1=(double*)malloc(sizeof(double)*coeff_num);
	double *derivT1_old=(double*)malloc(sizeof(double)*coeff_num);
	dir=(double*)malloc(sizeof(double)*coeff_num);
	double *dir_old=(double*)malloc(sizeof(double)*coeff_num);
	coeffs=(double*)malloc(sizeof(double)*coeff_num);
	double max_change,avg_change,sumsq20,sumsq2_h[1000][2],beta=0;
	coeff_count=(unsigned int*)malloc(coeff_num*sizeof(unsigned int));
	unsigned int i,j,iter=0;
	DWORD t1=get_time(),t2;
	
	// load training set
	f=fopen("c://xde//chess//data//TSn2.bin","rb");// main TS file, after Qs. Now just call eval on it.
	fread(&pos_count0,sizeof(unsigned int),1,f);
	ts_all=(ts_entry*)malloc(sizeof(ts_entry)*pos_count0); // storage of ts entries - 40 bytes each.
	pos_count0=(unsigned int)fread(ts_all,sizeof(ts_entry),pos_count0,f);
	fclose(f);
	
	pass=0;
	d1d_o=0;
	
	#define patt_cc 230
	rd=(regr_t*)malloc(sizeof(regr_t)*pos_count0); // regression data
	rd2=(regr2_t*)malloc(sizeof(regr2_t)*pos_count0); // regression data v2
	d1i=(UINT64*)malloc(pos_count0*sizeof(UINT64)); // offset to start
	d1s=(unsigned int*)malloc(pos_count0*sizeof(unsigned int)); // size of each entry
	d1da=(short unsigned int*)malloc(pos_count0*sizeof(short unsigned int)*patt_cc); // here "230" needs to be increased as new features are added. Currently i use 204.
	d1db=(unsigned char*)malloc(pos_count0*sizeof(unsigned char)*patt_cc); // here "230" needs to be increased as new features are added. Currently i use 204.
	if( d1s==NULL || d1da==NULL || d1db==NULL || d1i==NULL ) exit(666);

	// prep the iterations
	memset(coeffs,0,coeff_num*sizeof(double)); // final coefficients
	memset(derivT1_old,0,coeff_num*sizeof(double));	
	memset(dir_old,0,coeff_num*sizeof(double));
	f=fopen("c://xde//chess//out//iter2.csv","w");fprintf(f,"iter,sumsq2,sumsq2_2,alp*1000,beta,max_change,avg_change,max_change2,time\n");fclose(f);// clear output file
	if( excl_res<5 ) pos_count1=(pos_count0*4)/5; // reduce to account for second set
	else pos_count1=pos_count0; // no second set

	// save and reset
	short int *adj0=(short int*)malloc(2*coeff_num);
	short int *adj1=(short int*)malloc(2*coeff_num);
	short int *adj_all=(short int*)malloc(2*coeff_num*1100);
	memcpy(adj0,adj,2*coeff_num);
	//memset(adj,0,2*coeff_num);// reset all starting coeffs
	memset(adj+12,0,2*(coeff_num-12));// reset starting coeffs, excl piece values
	//memset(adj+12+746,0,2*(coeff_num-12-746));// reset starting coeffs, excl some values
	
	memcpy(adj1,adj,2*coeff_num); // save after reset
	init_all(1);// quiet. Get new coeffs into PST and all other places.

	// test - skip all
	//goto skip_all;

	// pass 0 - get all the coeffs
	lll.release();				// release the lock on split-point
	ii=0;						// start from the beginning
	#if calc_threads>1
		HANDLE cc[calc_threads];	// calculation thread handle
		for(j=0;j<calc_threads;++j) cc[j]=CreateThread(NULL,0,thread_function_0,(PVOID)j,0,NULL);//sec.attr., stack, function, param,flags, thread id
		WaitForMultipleObjects(calc_threads,cc,TRUE,INFINITE);// wait for threads to terminate.
		for(j=0;j<calc_threads;++j)  CloseHandle(cc[j]);
	#else
		thread_function_0((LPVOID)&j); //non-threaded call, for profiling
	#endif
	pass=2;
	srand(t1);
	while(1){// beginning of iterations loop****************************************************************************************************************************
		memset(coeff_count,0,coeff_num*sizeof(unsigned int));	
		memset(derivT1,0,coeff_num*sizeof(double));	
		memset(dir,0,coeff_num*sizeof(double));	
		pos_cnt_2=pos_cnt=0;
		sumsq2_2=sumsq2=0.;
	
		// loop over all positions and populate deriv
		// start calculation threads
		lll.release();				// release the lock on split-point
		ii=0;						// start from the beginning
		#if calc_threads>1
			for(j=0;j<calc_threads;++j) cc[j]=CreateThread(NULL,0,thread_function_1,(PVOID)j,0,NULL);//sec.attr., stack, function, param,flags, thread id
			WaitForMultipleObjects(calc_threads,cc,TRUE,INFINITE);// wait for threads to terminate.
			for(j=0;j<calc_threads;++j)  CloseHandle(cc[j]);
		#else
			thread_function_1((LPVOID)&j); //non-threaded call, for profiling
		#endif
		pass++;


		// add alpha*sum(coeff^2) to objective function
		#define alpha 0.00008
		for(j=0;j<coeff_num;++j){
			derivT1[j]+=pos_cnt*2.*(coeffs[j]+adj[j])*alpha;// first, add 2*c*a to derivative vector
			sumsq2+=pos_cnt*alpha*(coeffs[j]+adj[j])*(coeffs[j]+adj[j]);// second, add alpha*sum c^2 to R2
			sumsq2_2+=pos_cnt_2*alpha*(coeffs[j]+adj[j])*(coeffs[j]+adj[j]);// second, add alpha*sum c^2 to R2
		}
		
		pos_count1=pos_cnt;
		sumsq2=sqrt(sumsq2/pos_cnt);sumsq2_2=sqrt(sumsq2_2/pos_cnt_2);
		if( iter==0 ) sumsq20=sumsq2; // save starting point
		sumsq2_h[iter][0]=sumsq2;sumsq2_h[iter][1]=sumsq2_2;
		if( (iter>30 
			&& (sumsq2_h[iter][0]>sumsq2_h[iter-29][0] || sumsq2_h[iter][1]>sumsq2_h[iter-29][1])
			&& (sumsq2_h[iter-10][0]>sumsq2_h[iter-29][0] || sumsq2_h[iter-10][1]>sumsq2_h[iter-29][1])
			&& (sumsq2_h[iter-20][0]>sumsq2_h[iter-29][0] || sumsq2_h[iter-20][1]>sumsq2_h[iter-29][1]))
		|| iter>600 ){// exit condition: R2 worse than X iterations ago.
			f=fopen("c://xde//chess//out//iter2.csv","a");
			fprintf(f,"%d,%.7f,%.7f, ending iterations\n",iter,sumsq2,sumsq2_2);// log*********************************
			fclose(f);
			break;
		}

		// start of "determine coeff change by conjugate gradient method" ***************************************************************************
		// calculate beta;
		double tf1=0.,tf2=0.;
		for(i=0;i<coeff_num;++i){
			if( !coeff_count[i] ) continue;// skip some
			derivT1[i]=derivT1[i]/coeff_count[i];// scale derivative - precondition the matrix, using Jacobi (diagonal) approach
			tf1+=derivT1[i]*derivT1[i];// FR method
			tf2+=derivT1_old[i]*derivT1_old[i];
			derivT1_old[i]=derivT1[i];// save old derivative
		}
		beta=0;
		if( fabs(tf2)>0.0000001 ) beta=tf1/tf2;// denom is always zero on first iteration
		if( (iter%30)==0 ) beta=0; // reset every X iterations
		for(i=0;i<coeff_num;++i){// calculate direction
			if( !coeff_count[i] ) continue;// skip some
			dir[i]=derivT1[i];
			dir[i]=-dir[i]+beta*dir_old[i];
			dir_old[i]=dir[i];// save old direction
		}

		// Newton solve: estimate first and second directional derivatives
		d1=d2=0.;

		// loop over all positions
		// start calculation threads
		lll.release();				// release the lock on split-point
		ii=0;						// start from the beginning
		#if calc_threads>1
			for(j=0;j<calc_threads;++j) cc[j]=CreateThread(NULL,0,thread_function_2,(PVOID)j,0,NULL);//sec.attr., stack, function, param,flags, thread id
			WaitForMultipleObjects(calc_threads,cc,TRUE,INFINITE);// wait for threads to terminate.
			for(j=0;j<calc_threads;++j)  CloseHandle(cc[j]);
		#else
			thread_function_2((LPVOID)&j); //non-threaded call, for profiling
		#endif
		pass++;
		
		double alp=-d1/d2;// step. Positive.
		for(i=0;i<coeff_num;++i) coeffs[i]+=dir[i]*alp;// step forward
		

		// alternative approach
		/*double alp=0;
		static double aa[coeff_num];
		for(i=0;i<coeff_num;++i){
			dir[i]=-derivT1[i]/2.e9;
			coeffs[i]+=dir[i]-0.97*derivT1_old[i]/2.e9;
		}
		memcpy(derivT1_old,derivT1,sizeof(double)*coeff_num); // save
		*/

		// move some coeffs to adj[]
		double max_change2=0;
		for(i=0;i<coeff_num;++i){
			if( i>=O_K_PST && i<O_K_PST+32 && ((i-O_K_PST)&7)>2 ){// K PST mid, rank 4-8: make all adj equal to cell 4
				int cc=int(coeffs[O_K_PST+4]+0.5+20000)-20000;
				adj[i]+=cc;
				if( i==O_K_PST+31 )// last one, now reduce the coeffs
					coeffs[O_K_PST+4]-=cc;
			}else if( i==O_K_SAFETY ){// K safety: smooth it
				// 0-2=[0]
				int cc=int(coeffs[i]+0.5+20000)-20000;
				coeffs[i]-=cc;
				adj[i]+=cc;
				adj[i+1]=adj[i];
				adj[i+2]=adj[i];
				// 7=[1], linear in between
				for(j=0;j<11;++j){
					cc=int(coeffs[i+1+j]+0.5+20000)-20000;
					coeffs[i+j+1]-=cc;
					adj[i+j*5+7]+=cc;
					adj[i+j*5+3]=(adj[i+j*5+2]*4+1*adj[i+j*5+7])/5;
					adj[i+j*5+4]=(adj[i+j*5+2]*3+2*adj[i+j*5+7])/5;
					adj[i+j*5+5]=(adj[i+j*5+2]*2+3*adj[i+j*5+7])/5;
					adj[i+j*5+6]=(adj[i+j*5+2]*1+4*adj[i+j*5+7])/5;
				}
				// last one
				cc=int(coeffs[i+12]+0.5+20000)-20000;
				coeffs[i+12]-=cc;
				adj[i+61]+=cc;
				// extrapolate for 58-60
				adj[i+58]=adj[i+57]+(adj[i+57]-adj[i+52])/5;
				adj[i+59]=adj[i+58]+(adj[i+57]-adj[i+52])/5;
				adj[i+60]=adj[i+59]+(adj[i+57]-adj[i+52])/5;
				i+=61; // skip the rest
			}else{// normal processing - just round to nearest
				int cc=int(coeffs[i]+0.5+20000)-20000;
				adj[i]+=cc;
				coeffs[i]-=cc;
			}
			max_change2=max(max_change2,fabs(coeffs[i]));
		}

		// normalize some adjustments
		// 1. N mob mid
		double s1=0,s2=0;
		int a;
		for(j=0;j<9;j++){
			s1+=adj[O_N_MOB+2*j]*double(coeff_count[O_N_MOB+2*j]);
			s2+=coeff_count[O_N_MOB+2*j];
		}
		a=int(s1/s2+0.5+20000)-20000;		// determine average mob
		for(j=0;j<9;j++) adj[O_N_MOB+2*j]-=a;// reduce mob by average
		adj[1]+=a;							// increase material by average

		// 2. N mob end
		s1=s2=0;
		for(j=0;j<9;j++){
			s1+=adj[O_N_MOB+2*j+1]*double(coeff_count[O_N_MOB+2*j+1]);
			s2+=coeff_count[O_N_MOB+2*j+1];
		}
		a=int(s1/s2+0.5+20000)-20000;		// determine average mob
		for(j=0;j<9;j++) adj[O_N_MOB+2*j+1]-=a;// reduce mob by average
		adj[6+1]+=a;						// increase material by average

		// 3. B mob mid
		s1=s2=0;
		for(j=0;j<14;j++){
			s1+=adj[O_B_MOB+2*j]*double(coeff_count[O_B_MOB+2*j]);
			s2+=coeff_count[O_B_MOB+2*j];
		}
		a=int(s1/s2+0.5+20000)-20000;		// determine average mob
		for(j=0;j<14;j++) adj[O_B_MOB+2*j]-=a;// reduce mob by average
		adj[2]+=a;							// increase material by average

		// 4. B mob end
		s1=s2=0;
		for(j=0;j<14;j++){
			s1+=adj[O_B_MOB+2*j+1]*double(coeff_count[O_B_MOB+2*j+1]);
			s2+=coeff_count[O_B_MOB+2*j+1];
		}
		a=int(s1/s2+0.5+20000)-20000;		// determine average mob
		for(j=0;j<14;j++) adj[O_B_MOB+2*j+1]-=a;// reduce mob by average
		adj[6+2]+=a;						// increase material by average

		// 5. R mob mid
		s1=s2=0;
		for(j=0;j<15;j++){
			s1+=adj[O_R_MOB+2*j]*double(coeff_count[O_R_MOB+2*j]);
			s2+=coeff_count[O_R_MOB+2*j];
		}
		a=int(s1/s2+0.5+20000)-20000;		// determine average mob
		for(j=0;j<15;j++) adj[O_R_MOB+2*j]-=a;// reduce mob by average
		adj[3]+=a;							// increase material by average

		// 6. R mob end
		s1=s2=0;
		for(j=0;j<15;j++){
			s1+=adj[O_R_MOB+2*j+1]*double(coeff_count[O_R_MOB+2*j+1]);
			s2+=coeff_count[O_R_MOB+2*j+1];
		}
		a=int(s1/s2+0.5+20000)-20000;		// determine average mob
		for(j=0;j<15;j++) adj[O_R_MOB+2*j+1]-=a;// reduce mob by average
		adj[6+3]+=a;						// increase material by average

		// 7. Q mob mid
		// populate empty ones with last valid value
		j=27;
		while( coeff_count[O_Q_MOB+2*j]<400 ) j--; // use min of 400 positiobs here, for stability
		j++; // now j is first empty
		for(;j<28;j++) adj[O_Q_MOB+2*j]=adj[O_Q_MOB+2*j-2];
		s1=s2=0;
		for(j=0;j<28;j++){
			s1+=adj[O_Q_MOB+2*j]*double(coeff_count[O_Q_MOB+2*j]);
			s2+=coeff_count[O_Q_MOB+2*j];
		}
		a=int(s1/s2+0.5+20000)-20000;		// determine average mob
		for(j=0;j<28;j++) adj[O_Q_MOB+2*j]-=a;// reduce mob by average
		adj[4]+=a;							// increase material by average

		// 8. Q mob end
		// populate empty ones with last valid value
		j=27;
		while( coeff_count[O_Q_MOB+2*j+1]<400 ) j--; // use min of 400 positiobs here, for stability
		j++; // now j is first empty
		for(;j<28;j++) adj[O_Q_MOB+2*j+1]=adj[O_Q_MOB+2*j-2+1];
		s1=s2=0;
		for(j=0;j<28;j++){
			s1+=adj[O_Q_MOB+2*j+1]*double(coeff_count[O_Q_MOB+2*j+1]);
			s2+=coeff_count[O_Q_MOB+2*j+1];
		}
		a=int(s1/s2+0.5+20000)-20000;		// determine average mob
		for(j=0;j<28;j++) adj[O_Q_MOB+2*j+1]-=a;// reduce mob by average
		adj[6+4]+=a;						// increase material by average

		// 11. K PST mid
		s1=s2=0;
		for(j=0;j<32;j++){
			s1+=adj[O_K_PST+j]*double(coeff_count[O_K_PST+j]);
			s2+=coeff_count[O_K_PST+j];
		}
		a=int(s1/s2+0.5+20000)-20000;	// determine average 
		for(j=0;j<32;j++) adj[O_K_PST+j]-=a;// reduce by average

		// 12. K PST end
		s1=s2=0;
		for(j=0;j<32;j++){
			s1+=adj[O_K_PST+32+j]*double(coeff_count[O_K_PST+32+j]);
			s2+=coeff_count[O_K_PST+32+j];
		}
		a=int(s1/s2+0.5+20000)-20000;	// determine average 
		for(j=0;j<32;j++) adj[O_K_PST+32+j]-=a;// reduce by average

		// 13. Q PST mid
		s1=s2=0;
		for(j=0;j<32;j++){
			s1+=adj[O_Q_PST+j]*double(coeff_count[O_Q_PST+j]);
			s2+=coeff_count[O_Q_PST+j];
		}
		a=int(s1/s2+0.5+20000)-20000;	// determine average 
		for(j=0;j<32;j++) adj[O_Q_PST+j]-=a;// reduce by average
		adj[4]+=a;						// increase material by average

		// 14. Q PST end
		s1=s2=0;
		for(j=0;j<32;j++){
			s1+=adj[O_Q_PST+32+j]*double(coeff_count[O_Q_PST+32+j]);
			s2+=coeff_count[O_Q_PST+32+j];
		}
		a=int(s1/s2+0.5+20000)-20000;	// determine average 
		for(j=0;j<32;j++) adj[O_Q_PST+32+j]-=a;// reduce by average
		adj[6+4]+=a;					// increase material by average

		// 15. R PST mid
		s1=s2=0;
		for(j=0;j<32;j++){
			s1+=adj[O_R_PST+j]*double(coeff_count[O_R_PST+j]);
			s2+=coeff_count[O_R_PST+j];
		}
		a=int(s1/s2+0.5+20000)-20000;	// determine average 
		for(j=0;j<32;j++) adj[O_R_PST+j]-=a;// reduce by average
		adj[3]+=a;						// increase material by average

		// 16. R PST end
		s1=s2=0;
		for(j=0;j<32;j++){
			s1+=adj[O_R_PST+32+j]*double(coeff_count[O_R_PST+32+j]);
			s2+=coeff_count[O_R_PST+32+j];
		}
		a=int(s1/s2+0.5+20000)-20000;	// determine average 
		for(j=0;j<32;j++) adj[O_R_PST+32+j]-=a;// reduce by average
		adj[6+3]+=a;					// increase material by average

		// 17. B PST mid
		s1=s2=0;
		for(j=0;j<32;j++){
			s1+=adj[O_B_PST+j]*double(coeff_count[O_B_PST+j]);
			s2+=coeff_count[O_B_PST+j];
		}
		a=int(s1/s2+0.5+20000)-20000;	// determine average 
		for(j=0;j<32;j++) adj[O_B_PST+j]-=a;// reduce by average
		adj[2]+=a;						// increase material by average

		// 18. B PST end
		s1=s2=0;
		for(j=0;j<32;j++){
			s1+=adj[O_B_PST+32+j]*double(coeff_count[O_B_PST+32+j]);
			s2+=coeff_count[O_B_PST+32+j];
		}
		a=int(s1/s2+0.5+20000)-20000;	// determine average 
		for(j=0;j<32;j++) adj[O_B_PST+32+j]-=a;// reduce by average
		adj[6+2]+=a;					// increase material by average

		// 19. N PST mid
		s1=s2=0;
		for(j=0;j<32;j++){
			s1+=adj[O_N_PST+j]*double(coeff_count[O_N_PST+j]);
			s2+=coeff_count[O_N_PST+j];
		}
		a=int(s1/s2+0.5+20000)-20000;	// determine average 
		for(j=0;j<32;j++) adj[O_N_PST+j]-=a;// reduce by average
		adj[1]+=a;						// increase material by average

		// 20. N PST end
		s1=s2=0;
		for(j=0;j<32;j++){
			s1+=adj[O_N_PST+32+j]*double(coeff_count[O_N_PST+32+j]);
			s2+=coeff_count[O_N_PST+32+j];
		}
		a=int(s1/s2+0.5+20000)-20000;	// determine average 
		for(j=0;j<32;j++) adj[O_N_PST+32+j]-=a;// reduce by average
		adj[6+1]+=a;					// increase material by average

		// 21. P PST mid
		s1=s2=0;
		for(j=0;j<32;j++){
			s1+=adj[O_P_PST+j]*double(coeff_count[O_P_PST+j]);
			s2+=coeff_count[O_P_PST+j];
		}
		a=int(s1/s2+0.5+20000)-20000;	// determine average 
		for(j=0;j<32;j++) adj[O_P_PST+j]-=a;// reduce by average
		adj[0]+=a;						// increase material by average

		// 22. P PST end
		s1=s2=0;
		for(j=0;j<32;j++){
			s1+=adj[O_P_PST+32+j]*double(coeff_count[O_P_PST+32+j]);
			s2+=coeff_count[O_P_PST+32+j];
		}
		a=int(s1/s2+0.5+20000)-20000;	// determine average 
		for(j=0;j<32;j++) adj[O_P_PST+32+j]-=a;// reduce by average
		adj[6+0]+=a;					// increase material by average

		// save current results
		avg_change=max_change=0;
		for(i=0;i<coeff_num;++i){
			*(adj_all+iter*coeff_num+i)=(short int)( coeffs[i]+adj[i] + (coeffs[i]+adj[i]>0?0.5:-0.5) ); // round to nearest
			if( iter ){
				max_change=max(max_change,fabs(*(adj_all+iter*coeff_num+i)-*(adj_all+(iter-1)*coeff_num+i)));
				avg_change+=fabs(*(adj_all+iter*coeff_num+i)-*(adj_all+(iter-1)*coeff_num+i))/double(coeff_num);
			}else{
				max_change=max(max_change,fabs(*(adj_all+iter*coeff_num+i)-adj1[i]));
				avg_change+=fabs(*(adj_all+iter*coeff_num+i)-adj1[i])/double(coeff_num);
			}
		}

		init_all(1);// quiet. Get new coeffs into PST and all other places.

		f=fopen("c://xde//chess//out//iter2.csv","a");
		t2=get_time();
		fprintf(f,"%d,%.7f,%.7f,%.10f,%.10f,%.2f,%.2f,%.2f,%d\n",iter,sumsq2,sumsq2_2,alp*1000,beta,max_change,avg_change,max_change2,(t2-t1)/1000);// log*********************************
		fclose(f);

		iter++;
	}// end of iterations loop
	//skip_all:
	// find lowest validation R2 and use that result
	i=0;
	double x=1000000;
	for(j=0;j<iter;++j)
		if( sumsq2_h[j][1]<x ){
 			x=sumsq2_h[j][1];
			i=j;
		}
	unsigned int iter0=iter;
	iter=max(1,i);
	sumsq2=sumsq2_h[iter][0];

	// write results to file
	f=fopen("c://xde//chess//out//iter2.csv","a");
	fprintf(f,"\n\n\nl,count,old,new\n");
	for(i=0;i<coeff_num;++i){
		fprintf(f,"%d,%d,%d,%d,",i,coeff_count[i],adj0[i],*(adj_all+(iter-1)*coeff_num+i));
		if( iter0<150 ) for(j=0;j<iter0;++j) fprintf(f,"%d,",*(adj_all+j*coeff_num+i)); // only save for low number of iterations
		fprintf(f,"\n");
	}

	// second format - for copy/pasting back into C code
	fprintf(f,"\n\n");
	for(i=0;i<coeff_num;++i){
		if( (i%400)==0 ) fprintf(f,"\n");// new line after this many numbers (400)
		fprintf(f,"%d,",*(adj_all+(iter-1)*coeff_num+i));
	}
	fclose(f);

	t2=get_time();
	char res[200],res2[200];
	sprintf(res,"Diff=%.3f-%.3f=%.3f. Time: %.1f sec",sumsq20,sumsq2,sumsq20-sumsq2,(t2-t1)/1000.);
	sprintf(res2,"Pos Count:%u. Iterations:%d",pos_cnt,iter);
	MessageBox( hWnd_global, res,res2,MB_ICONERROR | MB_OK );
	exit(0);
}

extern void run_nn(void);
extern void run_nn2(void);
void train(void){
	//run_nn2();
	//use_hash=1;run_nn(); // here "use_hash" should be set to 1, to get correct pawn scores
	#if TRAIN
	//eliminate_duplicates();// eliminate duplicate TS entries
	//export_TS();// export TS as plain text

	//analyze();// Perform all sorts of analysis.
	run_over_TS_v2(); // run my eval over all TS positions
	
	//read_games2(); // read and process CCRL games
	//read_games(); // read and process my games
	#endif
}
