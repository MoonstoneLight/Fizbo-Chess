typedef struct{
	char FEN[80];
	int eval;				// 100x
	int depth;
	int time;				// 100x
	unsigned char move[2];	// from, to
} b2;

#define MAX_PLY_COUNT 700

typedef struct{
	unsigned int me;		// 0-unknown,1-i am white, 2-i am black
	unsigned int result;	// 1+2*result for white: 1,2,3
	unsigned int plycount;
	b2 board[MAX_PLY_COUNT];
} game;

void convert_board_to_TS(board*,ts_entry*);

unsigned int str_comp(char *input,char * command){
	unsigned int i;
	for(i=0;i<strlen(command);++i)
		if( input[i]!=command[i] )
			return(0);
	return(1);
}

unsigned int skip_blanks(char * data){
	unsigned int o=0;
	while( data[o]==' ' || data[o]==10 || data[o]==13 )
		o++;
	return(o);
}

unsigned int skip_past(char c,char * data){
	unsigned int o=0;
	while( data[o]!=c && o<5000 )
		o++;
	if( o>=5000 ) return(0); // end of file?
	o++;
	o+=skip_blanks(data+o);
	return(o);
}

unsigned int skip_past_long(char c,char * data){
	unsigned int o=0;
	while( data[o]!=c && o<500000 )
		o++;
	if( o>=500000 ) return(0); // end of file?
	o++;
	o+=skip_blanks(data+o);
	return(o);
}

unsigned int skip_past1(char c,char * data){//Do not skip past }
	unsigned int o=0;
	while( data[o]!=c && o<5000 && data[o]!='}' )
		o++;
	if( o>=5000 ) return(0); // end of file?
	if( data[o]!='}' ) o++;
	o+=skip_blanks(data+o);
	return(o);
}

int get_int(char* data){
	int r=0,mult=1,decimal_marker=-100,cm=0;

	// process sign
	if( data[0]=='+' )
		data++;
	else if( data[0]=='-' ){
		data++;
		mult=-1;
	}

	// process checkmate
	if( data[0]=='M' ){
		cm=1;
		data++;
	}

	// base loop
	while( data[0]>='0' && data[0]<='9' && decimal_marker<2 ){// only capture 2 digits after decimal
		r=r*10+data[0]-'0';
		decimal_marker++;
		data++;
		if( data[0]=='.' ){// skip decimal
			data++;
			decimal_marker=0;
		}
	}
	if( decimal_marker==1 )// only 1 digit after decimal - multiply by 10.
		r=r*10;
	if( cm )
		r=10000-r;
	return(r*mult);
}


unsigned int get_legal_moves_fast(board *b,unsigned char *list){
	unsigned int i,mc1,mc;
	unsigned char list2[256];

	// See if i'm in check.
	unsigned int in_check=cell_under_attack(b,b->kp[b->player-1],b->player); // from this point on in_check is defined.
	if( in_check )
		mc1=get_out_of_check_moves(b,list2,b->kp[b->player-1],in_check-64);
	else
		mc1=get_all_moves(b,list2);


	// make all the moves, exclude the ones that lead to check
	unsigned char player=b->player;
	unsigned char kp0=b->kp[player-1];
	UINT64 KBB=UINT64(1)<<kp0;
	for(mc=i=0;i<mc1;++i){

		// See if i'm in check after the move
		if( list2[2*i]==kp0 ){
			b->colorBB[player-1]^=KBB;					// update occupied BB of player. here i only need to remove the king from its current position on colorBB board.
			int j=player_moved_into_check(b,list2[2*i+1],player);
			b->colorBB[player-1]^=KBB;					// update occupied BB of player.
			if( j ) continue;
		}
		

		// good move. Put it on the list.
		list[2*mc]=list2[2*i];
		list[2*mc+1]=list2[2*i+1];
		mc++;
	}

	return(mc);
}