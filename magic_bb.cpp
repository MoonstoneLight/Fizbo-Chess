// PDEP move generation (used to be magic, but PDEP is better).
#include "chess.h"
#include <intrin.h>

extern UINT64 *mem;

static UINT64* RTable; // Storage space for rook attacks. 800 Kb
static UINT64* BTable;  // Storage space for bishop attacks. 41 Kb
static UINT64 RMasks[64];
static UINT64* RAttacks[64];
static UINT64 BMasks[64];
static UINT64* BAttacks[64];

static UINT64 sliding_attack(int deltas[],int sq,UINT64 occupied){
	UINT64 attack=0,one=1;
	int i,s;

    for(i=0;i<4;i++)// loop over 4 directions
		for(s=sq+deltas[i];s>=0 && s<=63 && dist[s][s-deltas[i]]==1;s+=deltas[i]){
            attack|=(one<<s);
            if( occupied&(one<<s) )
                break;
        }
    return(attack);
}

#if USE_PEXT
UINT64 attacks_bb_R(int s,UINT64 occ){
	return RAttacks[s][_pext_u64(occ,RMasks[s])];
}

UINT64 attacks_bb_B(int s,UINT64 occ){
	return BAttacks[s][_pext_u64(occ,BMasks[s])];
}

static void init_pdep_arrays(UINT64 table[],UINT64* attacks[],UINT64 masks[],int deltas[],unsigned int index){
	UINT64 *occupancy=(UINT64*)malloc(4096*8);
	UINT64 *reference=(UINT64*)malloc(4096*8);
	UINT64 FileBB[8],RankBB[8],edges,b;
    unsigned int s,i,size;

	// init rank/file BBs
	if( occupancy==NULL || reference==NULL ) exit(123);
	FileBB[0]=0x0101010101010101;
	RankBB[0]=0xff;
	for(i=1;i<8;i++){
      FileBB[i]=FileBB[i-1]<<1;
      RankBB[i]=RankBB[i-1]<<8;
	}

    // attacks[s] is a pointer to the beginning of the attacks table for square 's'
    attacks[0]=table;

	for(s=0;s<64;s++){
	    // Board edges are not considered in the relevant occupancies
        edges=(0xff000000000000ff&~RankBB[s/8])|(0x8181818181818181&~FileBB[s%8]);

        masks[s]=sliding_attack(deltas,s,0)&~edges;
       
        // Use Carry-Rippler trick to enumerate all subsets of masks[s] and
        // store the corresponding sliding attack bitboard in reference[].
		b=size=0;
        do{
            occupancy[size]=b;
            reference[size++]=sliding_attack(deltas,s,b);
            b=(b-masks[s])&masks[s];
        }while(b);

        // Set the offset for the table of the next square. We have individual
        // table sizes for each square with "Fancy Magic Bitboards".
        if( s<63 )
            attacks[s+1]=attacks[s]+size;

		// now place all elements of reference[] in attacks[] in right order.
		for(i=0;i<size;++i){
			unsigned int ii;
			if( index==0 )// rook
				ii=(unsigned int)_pext_u64(occupancy[i],RMasks[s]);
			else// bishop
				ii=(unsigned int)_pext_u64(occupancy[i],BMasks[s]);

			UINT64 *a=attacks[s]+ii;
			*a=reference[i];
		}
	}
	free(occupancy);
	free(reference);
}

void int_m2(void){
	int RDeltas[4]={8,1,-8,-1},BDeltas[4]={9,-7,-9,7};

	// alloc tables using large page memory
	RTable=mem;BTable=RTable+102400;mem+=102400+5248;// now meme points to remaining free memory

	// init pdep arrays
	init_pdep_arrays(RTable,RAttacks,RMasks,RDeltas,0);// for rook
	init_pdep_arrays(BTable,BAttacks,BMasks,BDeltas,1);// for bishops
}
#else // use magics
static UINT64 RMagics[64]={
	0x8004218410c000,0x8240044020001000,0x80200080100248,0x80051001480080,0x100100500060800,0x200820010080400,0xa080050006004080,0x8001408000e100,
	0x2106800020804000,0xe048401000402000,0x442808010006000,0x182801001080280,0x402001200042048,0x4201000224010008,0x101000300248200,0x2c10008c2009900,
	0x440908002244000,0x2890004004402000,0x5601110020010640,0x805010020089000,0x60850011009800,0x8821808002008400,0x8108040012011088,0x460020001008044,
	0x280054240002000,0x240600040100048,0x600a402200128600,0x45000d00201000,0x10200c600101820,0x280040080800200,0x100094c00280a10,0x800080084100,
	0x80904000800420,0x2010201000400040,0x200080801000,0x4522808800801000,0x1001005000800,0x10c40080800200,0x4000800a00804100,0x830800040800100,
	0x1000314000808000,0xc502002d0024000,0x85000a0008c8010,0x40900a020020,0x940008008080,0xc00420080110,0x281a00030c420088,0x11de49008b420004,
	0xa14000800480,0x8080200080c00080,0x205401206846200,0x204100080080480,0x4008028008040080,0x1000810200040080,0x2051004442000100,0x7d02110401408200,
	0xd6850140d3800425,0xc20e214002750b7b,0xdfbfffb5871bf78a,0xf33ffde10fff00d9,0x6ca6002050983c92,0x32c600042859102a,0xd418c81d100e0db4,0x8c08630681a2c40e};
static unsigned int RShifts[64];
static UINT64 BMagics[64]={
	0x402240816040020,0x800c044084210088,0x408021a400060,0x438c410020800000,0x405202c009000,0xa080404801000,0x12b48200a0020,0x510901202080,
	0x84404190020a0040,0x6a200801010220,0x4908040812044080,0x42014040082a080,0x1000040504704400,0x8100420104600000,0x4000c2088080800,0x2000002888041100,
	0x28048c30900200,0x5001050060448,0x110020110420040,0x201800802810080,0x40c000094200020,0x4012000022102a00,0x46000042105400,0x8000270504022200,
	0x9008148a60200280,0x522a10001204c800,0x2052a0004080200,0x2c080121202040,0x409010000104000,0x80802d006000,0x201004141080800,0x24850240210800,
	0x1181a00800200800,0x2013028041000,0xa8c0100100040,0x3002004140040100,0x40004050010100,0x8481010600090800,0x200c0c0402008080,0x41204080021208,
	0x2012051040001800,0x8a41080803054420,0x1221908401011008,0x2104208800800,0x80280101410400,0x20202148800840,0xa20030c20841320,0x84c080489040220,
	0x4022038208c00000,0x825030110024000,0x38081e004a280810,0x9008098c040000,0x3000105042022060,0x8004820a8018080,0xc009080128220260,0x8202080803084000,
	0x20304808081820,0x10501012000,0x2001080142009000,0x86380440420220,0x1000000020624400,0x4a080940500400c0,0x80902118051440,0x4040020800c50040};
static unsigned int BShifts[64];
static UINT64 FileBB[8];
static UINT64 RankBB[8];

inline unsigned int magic_index_R(int s,UINT64 occ){
  return unsigned(((occ&RMasks[s])*RMagics[s])>>RShifts[s]);
}

inline unsigned int magic_index_B(int s,UINT64 occ){
  return unsigned(((occ&BMasks[s])*BMagics[s])>>BShifts[s]);
}

UINT64 attacks_bb_R(int s,UINT64 occ){
  return RAttacks[s][magic_index_R(s,occ)];
}

UINT64 attacks_bb_B(int s,UINT64 occ){
  return BAttacks[s][magic_index_B(s,occ)];
}

static inline UINT64 r64(void){// random number generator
	static UINT64 z=0x231df0e27ac6903b;
	z=z*6364136223846793005+1442695040888963407;
	return(z);
}

static inline UINT64 r64_fewbits(void){
	return(r64()&r64()&r64());// here i needed single r64 to produce last 8 rook results.
}

static void init_magics(UINT64 table[],UINT64* attacks[],UINT64 magics[],UINT64 masks[],unsigned int shifts[],int deltas[],unsigned int index){
	UINT64 *occupancy=(UINT64*)malloc(4096*8);
	UINT64 *reference=(UINT64*)malloc(4096*8);
    UINT64 edges,b;
    unsigned int s,i,size,attempts;

    // attacks[s] is a pointer to the beginning of the attacks table for square 's'
    attacks[0]=table;

	for(s=0;s<64;s++){
	    // Board edges are not considered in the relevant occupancies
        edges=(0xff000000000000ff&~RankBB[s/8])|(0x8181818181818181&~FileBB[s%8]);

        masks[s]=sliding_attack(deltas,s,0)&~edges;
        shifts[s]=64-(unsigned int)popcnt64l(masks[s]);

        // Use Carry-Rippler trick to enumerate all subsets of masks[s] and
        // store the corresponding sliding attack bitboard in reference[].
		b=size=0;
        do{
            occupancy[size]=b;
            reference[size++]=sliding_attack(deltas,s,b);
            b=(b-masks[s])&masks[s];
        }while(b);

        // Set the offset for the table of the next square. We have individual
        // table sizes for each square with "Fancy Magic Bitboards".
        if( s<63 )
            attacks[s+1]=attacks[s]+size;

        // Find a magic for square 's' picking up an (almost) random number
        // until we find the one that passes the verification test.
		attempts=0;
        do{// here i always start with existing magic, in case it is good.
			memset(attacks[s],0,size*sizeof(UINT64));
			attempts++;
            for(i=0;i<size;i++){
				unsigned int ii;
				if( index==0 )// rook
					ii=magic_index_R(s,occupancy[i]);
				else// bishop
					ii=magic_index_B(s,occupancy[i]);

				UINT64 *a=attacks[s]+ii;
				if ( *a && *a!=reference[i] )
                    break;
                *a=reference[i];
            }
			if( i!=size )// failed - produce new magic
				magics[s]=r64_fewbits();
        }while( i!=size );
    }
	free(occupancy);
	free(reference);
}

void int_m2(void){
	unsigned int i;
	int RDeltas[4]={8,1,-8,-1},BDeltas[4]={9,-7,-9,7};

	FileBB[0]=0x0101010101010101;
	RankBB[0]=0xff;
	for(i=1;i<8;i++){
      FileBB[i]=FileBB[i-1]<<1;
      RankBB[i]=RankBB[i-1]<<8;
	}

	// alloc tables using large page memory
	RTable=mem;BTable=RTable+102400;mem+=102400+5248;// now mem points to remaining free memory

	init_magics(RTable,RAttacks,RMagics,RMasks,RShifts,RDeltas,0);// for rook
	init_magics(BTable,BAttacks,BMagics,BMasks,BShifts,BDeltas,1);// for bishop
}
#endif