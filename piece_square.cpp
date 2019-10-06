// piece-square tables
#include "chess.h"
#include "coeffs.h"
static short int piece_square0[2][6][2][64];						// mid/end, 6 pieces, 2 colors, 64 squares
short int piece_square00[2][6][64];									// midgame+endgame, 6 pieces, 64 squares. For white. Black gets opposite score for flipped board.

void init_piece_square(void){// set "piece_square0" variable - transpose piece_square00 and add piece value. Called only on start-up, after eval coeffs are produced
	unsigned int i,j,k,jj,jj2;

	memset(piece_square0,0,sizeof(piece_square0));// init to blanks
	for(i=0;i<6;i++)// piece
		for(j=0;j<64;++j){// square
			jj=((j&7)<<3)+(j>>3);// transpose
			jj2=(j&56)+(7-j&7);// for black, flip rank
			// midgame
			piece_square0[0][i][0][j]=piece_square00[0][i][jj]+adj[O_P+i];// white
			piece_square0[0][i][1][jj2]=-piece_square0[0][i][0][j];// black
			// endgame
			piece_square0[1][i][0][j]=piece_square00[1][i][jj]+adj[O_P_E+i];// white
			piece_square0[1][i][1][jj2]=-piece_square0[1][i][0][j];// black
		}

	// copy rank 8 queen into pawn - for promotion logic. And zero out rank 1 - for ep logic
	for(j=7;j<64;j+=8){// square
		jj2=(j&56)+(7-j&7);// for black, flip rank
		// midgame
		piece_square0[0][0][0][j]=piece_square0[0][4][0][j];// white
		piece_square0[0][0][1][jj2]=piece_square0[0][4][1][jj2];// black
		// endgame
		piece_square0[1][0][0][j]=piece_square0[1][4][0][j];// white
		piece_square0[1][0][1][jj2]=piece_square0[1][4][1][jj2];// black

		// midgame
		piece_square0[0][0][0][jj2]=0;// white
		piece_square0[0][0][1][j]=0;// black
		// endgame
		piece_square0[1][0][0][jj2]=0;// white
		piece_square0[1][0][1][j]=0;// black
	}

	// populate final PST
	for(j=0;j<6;++j)// piece
		for(k=0;k<2;++k)// color
			for(i=0;i<64;++i){// squares
				piece_square[j][k][i][0]=piece_square0[0][j][k][i];// midgame
				piece_square[j][k][i][1]=piece_square0[1][j][k][i];// endgame
			}
}