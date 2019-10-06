#include "pgn.h"

static unsigned int decode_move_pgn(unsigned char *m,char *d){// take move from "d", put it in "m". Return # of characters processed.
	unsigned int mc,mc2,mc3,offset,i,l=0;
	unsigned char list[256],list2[256],list3[256];

	// get all legal moves
	mc=get_legal_moves(&b_m,list);

	// see if castling
	if( d[0]=='O' ){
		m[0]=b_m.kp[b_m.player-1];// king moves
		if( d[3]=='-' && d[4]=='O' ){// long castle
			m[1]=m[0]-16;
			l=5;
			if( d[5]=='+' ) l++;
		}else{// short castle
			m[1]=m[0]+16;
			l=3;
			if( d[3]=='+' ) l++;
		}
		check_move(m,list,mc);// check that this move is on the list.
		assert(m[0]!=m[1]);
		return(l);
	}

	// find "to"
	offset=0;
	while ( d[offset]!=' ' && d[offset]!='=' && d[offset]!=13 && d[offset]!=10 ) offset++;// find " " or "=" or "\n", back up 1. Or 2, if "+" or "#"
	offset--;
	l+=offset+1;
	if( d[offset+1]=='=' ){// promotion
		l+=2;
		if( d[offset+2]=='N' )
			l+=16;// +16 for underpromotion to N.
		else if( d[offset+2]=='B' )
			l+=32;// +32 for underpromotion to B.
		else if( d[offset+2]=='R' )
			l+=48;// +48 for underpromotion to R.
	}
	if( d[offset+3]=='+' || d[offset+3]=='#' ) l++;
	if( d[offset]=='+' || d[offset]=='#' ) offset--;
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
		return(l);
	}
	assert(mc2>0);

	// find "from"
	// first see if notation is like "a1"
	// only if there are 2 chars before "to" move
	if( offset-1==2 ){
		if(d[0]>='a' && d[0]<='h' && d[1]>='1' && d[1]<='9'){// it is. Wrap it up now.
			m[0]=d[1]-'1';
			m[0]+=(d[0]-'a')*8;
			assert(m[0]!=m[1]);
			return(l);
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
	from_piece+=1<<(5+b_m.player);// add player bit

	// find all "from_piece" on list2
	list3[0]=list3[1]=0;// init to nothing
	for(mc3=i=0;i<mc2;++i){
		if( b_m.piece[list2[2*i]]==from_piece ){
			list3[2*mc3]=list2[2*i];
			list3[2*mc3+1]=list2[2*i+1];
			mc3++;
		}
	}
	if( mc3==1 ){// only one "from piece"
		m[0]=list3[0];
		assert(m[0]!=m[1]);
		return(l);
	}

	// multiple from pieces. File is coded by second letter of input. Or by first letter, if pawn moves
	if( file==8 ){
		if( d[1]>='a' && d[1]<='h' ){// file
			file=d[1]-'a';

			if( mc3==3 ){
				// how many files match?
				if( list3[0]/8==file && list3[2]/8!=file && list3[4]/8!=file){// only first matches
					m[0]=list3[0];
					assert(m[0]!=m[1]);
					return(l);
				}else if( list3[0]/8!=file && list3[2]/8==file && list3[4]/8!=file){// only second matches
					m[0]=list3[2];
					assert(m[0]!=m[1]);
					return(l);
				}else if( list3[0]/8!=file && list3[2]/8!=file && list3[4]/8==file){// only third matches
					m[0]=list3[4];
					assert(m[0]!=m[1]);
					return(l);
				}else{// multiple matches.
					if( d[2]>='1' && d[2]<='8' ){// rank
						unsigned int rank=d[2]-'1';
						file=file*8+rank;//cell
						if( list3[0]==file ){
							m[0]=list3[0];
							assert(m[0]!=m[1]);
							return(l);
						}else if( list3[2]==file ){
							m[0]=list3[2];
							assert(m[0]!=m[1]);
							return(l);
						}else{
							m[0]=list3[4];
							assert(m[0]!=m[1]);
							return(l);
						}
					}
					assert(0);
				}
			}
			if( mc3==2 && list3[0]/8==file ){
				m[0]=list3[0];
				assert(m[0]!=m[1]);
				return(l);
			}
			if( mc3==2 && list3[2]/8==file ){
				m[0]=list3[2];
				assert(m[0]!=m[1]);
				return(l);
			}
			assert(0);// mc3>3 - problem.
		}else{// rank
			file=d[1]-'1';// rank

			if( mc3==2 && list3[0]%8==file){
				m[0]=list3[0];
				assert(m[0]!=m[1]);
				return(l);
			}
			if( mc3==2 && list3[2]%8==file){
				m[0]=list3[2];
				assert(m[0]!=m[1]);
				return(l);
			}
			assert(0);// mc3>2 - problem.
		}
	}else{// file is already defined, use it
		if( mc3==2 && list3[0]/8==file){
			m[0]=list3[0];
			assert(m[0]!=m[1]);
			return(l);
		}
		if( mc3==2 && list3[2]/8==file){
			m[0]=list3[2];
			assert(m[0]!=m[1]);
			return(l);
		}
		assert(0);// mc3>2 - problem.
	}
	m[0]=list3[2];
	assert(m[0]!=m[1]);
	return(l);
}

unsigned int gc,pc,o,j,plycount;
int w_elo,b_elo,result;
FILE *fo=NULL;
unsigned int sss=16496579; // update this as needed.
static unsigned int read_one_game(char *data){// read 1 game from pgn file; return size of input. Dump some positions as needed.
	o=plycount=w_elo=b_elo=result=0; // init
	
	// process header
	gc++; // increment game counter
	o+=skip_blanks(data+o);
	while( data[o]=='[' ){
		o++;
		if( str_comp(data+o,"WhiteElo") ){
			o+=10;// whiteelo+space+quote
			w_elo=get_int(data+o);
		}else if( str_comp(data+o,"BlackElo") ){
			o+=10;// blackelo+space+quote
			b_elo=get_int(data+o);
		}else if( str_comp(data+o,"Result") ){
			o+=8;// result+space+quote
			if( str_comp(data+o,"1-0") )
				result=1;
			else if( str_comp(data+o,"0-1") )
				result=-1;
			else if( str_comp(data+o,"1/2-1/2") )
				result=0;
			else{
				assert(0);
				exit(6);// unknown result
			}
		}else if( str_comp(data+o,"PlyCount") ){
			o+=10;// plycount+space+quote
			plycount=get_int(data+o);
		}
		// unknown []
		o+=skip_past(']',data+o);
	}

	// skip some games***************************************************************************************
	if( w_elo<2600 || b_elo<2600 || abs(w_elo-b_elo)>140 || plycount<60 || plycount>250 )
		plycount=0;// 0 cuts this game


	// do things
	if( plycount ){
		static unsigned int gcl=0;
		if( gcl++<1000 ){// log first 1000 games
			FILE *f1;
			if( gcl<=1 ){
				f1=fopen("c://xde//chess//out//gc_log.csv","w");// init
				fprintf(f1,"gc,w_elo,b_elo,result,plycount,pc\n");
			}else
				f1=fopen("c://xde//chess//out//gc_log.csv","a");// append
			if( f1==NULL ) exit(7);
			fprintf(f1,"%d,%d,%d,%d,%d,%d\n",gc,w_elo,b_elo,result,plycount,pc);
			fclose(f1);
		}
	
		if( pc==0 ){
			fo=fopen("c://xde//chess//data//TSnew2.bin","wb");// init
			fwrite(&sss,sizeof(unsigned int),1,fo); // write size
		}else
			if( fo==NULL )
				fo=fopen("c://xde//chess//data//TSnew2.bin","ab");// append
		if( fo==NULL )exit(7);

		init_board(0);// set b_m to initial board
		b_m.scorem=get_scorem(&b_m);b_m.scoree=get_scoree(&b_m);
	}
	if( plycount ) for(unsigned int i=0;i<plycount;i++){// loop over moves
		int eval;
		unsigned char mm[2];
		if( !(i&1) ){// skip move counter - for whites only
			o+=skip_blanks(data+o);
			j=get_int(data+o);
			assert(j==1+i/2);
			o+=skip_past('.',data+o);
		}else
			o+=skip_blanks(data+o);
		// now at: Bxe4 {+0.07/13 0.17s}

		// get move
		j=decode_move_pgn(mm,data+o);// start from data[o], put result in mm[0]+mm[1] . Return length+16*underpromotion: n=1
		o+=j&15;
		o+=skip_blanks(data+o); // now at: {+0.07/13 0.17s}
		eval=1;
		if( data[o]=='{' ){// misc data present - decode it
			o++;

			// if not a book move, get stats
			if( data[o]!='0' && data[o+1]!='s' ){// book is {0s}
				// get eval
				if( data[o]=='(' )
					o+=skip_past(')',data+o);
				eval=get_int(data+o);
			}
			o+=skip_past('}',data+o);
		}// else: misc data absent


		// dump the position
		//if( abs(eval)>2000 ) break;// stop on eval over 20.
		//if( abs(b_m.scorem)>2000 ) break;// stop on mid PST over 20.
		if( popcnt64l(b_m.colorBB[0]|b_m.colorBB[1])<=6 )// stop if 6 or fewer pieces
			break;
		if( i>plycount-8 && result==0 )// drop last 4 moves for draws
			break;
		if( b_m.halfmoveclock>20 && result==0 )// stop at halfmoveclock of 20 for draws
			break;
		if( b_m.halfmoveclock>40 && result!=0 )// stop at halfmoveclock of 40 for not draws
			break;
		if( i>15*2 ){// skip first 15 moves - assume they are book.
			ts_entry ts;
			convert_board_to_TS(&b_m,&ts);// convert b_m to ts entry format. This populates board, castle, player, last move and full move clock.
			ts.score=result*((i&1)?-10000:10000); // convert this to result of the player to move now
			ts.remarks=3; // 3=game outcome
			pc++;

			// save TS file
			fwrite(&ts,sizeof(ts_entry),1,fo);

			if( pc<1000 ){// log first 1000 positions
				FILE *f1;
				if( pc<=1 ){
					f1=fopen("c://xde//chess//out//pc_log.csv","w");// init
					fprintf(f1,"gc,plycount,result,i,pieces,eval,PST,FEN\n");
				}else
					f1=fopen("c://xde//chess//out//pc_log.csv","a");// append
				if( f1==NULL ) exit(7);
				char sss[100];print_position(sss,&b_m);
				fprintf(f1,"%d,%d,%d,%d,%d,%d,%d,%s",gc,plycount,result,i,popcnt64l(b_m.colorBB[0]|b_m.colorBB[1]),eval,b_m.scorem,sss);
				fclose(f1);
			}
		}
	

		// make the move
		unmake d;
		d.promotion=0;
		make_move(&b_m,mm[0],mm[1],&d);
		if(j>=16){// underpromotion.
			UINT64 one=1;
			UINT64 BB=one<<mm[1];// to
			int new_value=j/16;// 1=N,2=B,3=R

			b_m.piece[mm[1]]-=(4-new_value);					// turn queen into knight
			b_m.piececolorBB[4][(b_m.player-1)^1]^=BB;			// delete player queen
			b_m.piececolorBB[new_value][(b_m.player-1)^1]^=BB;	// add player knight
		}
	}// end of loop over moves

	// get to beginning of next game
	j=skip_past('[',data+o);
	while( j==0 ){
		o+=5000;
		j=skip_past('[',data+o);
	}
	o+=j;
	o--;
	//if( fo!=NULL ){ fclose(fo);fo=NULL;}
	return(o);
}

void read_games2(void){// read games from CCRL pgn file
	FILE *f=fopen("C:\\xde\\chess\\data\\CCRL.pgn","r");
	size_t ss;
	ss=1024*1024;
	ss=ss*2200;
	char *data=(char*)malloc(ss);
	unsigned int o=0,t_size=(unsigned int)fread(data,1,ss,f);// input buffer - 2.2Gb
	fclose(f);
	assert(t_size<ss);
	
	// process games, 1 at a time
	while( t_size>o+100 ){
		unsigned int o1=read_one_game(data+o);
		o+=o1;
		assert(o1>10);

		//if( pc>500 ) break; // stop after X games
	}
	if( fo!=NULL ) fclose(fo);
	free(data);


	// load training set
	/*f=fopen("c://xde//chess//data//TS.bin","rb");// main TS file
	unsigned int pos_count0;
	fread(&pos_count0,sizeof(unsigned int),1,f);
	pos_count0=1000;
	ts_entry *ts_all=(ts_entry*)malloc(sizeof(ts_entry)*pos_count0); // storage of ts entries - 40 bytes each. 240 Mb.
	fread(ts_all,sizeof(ts_entry),pos_count0,f);
	fclose(f);
	*/


	exit(0);
}