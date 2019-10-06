#include "pgn.h"

static unsigned int decode_move_pgn(unsigned char *m,char *d){// take move from "d", put it in "m". Return # of characters processed.
	unsigned int mc,mc2,mc3,offset,i,l=0;
	unsigned char list[256],list2[256],list3[256];

	// get all legal moves
	mc=get_legal_moves_fast(&b_m,list);

	// see if castling
	if( d[0]=='O' ){
		m[0]=b_m.kp[b_m.player-1];// king moves
		if( d[4]=='O' ){// long castle
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

ts_entry *out; // output buffer
unsigned int pc; // output position count

static unsigned int read_one_game(char *data){// read 1 game from pgn file; return size of input.
	static int gc=0;
	unsigned int o=0,i,j,plycount=0,o1,pc0=pc,F=0;
	int result=2,eval,skip=0;
	unsigned char mm[2];

	// process header
	gc++;
	o+=skip_blanks(data+o);
	while( data[o]=='[' ){
		o++;
		if( str_comp(data+o,"Result") ){
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
			assert(plycount<MAX_PLY_COUNT);
		}else if( str_comp(data+o,"Termination") ){
			o+=13;// termination+space+/+quote
			if( str_comp(data+o,"adjudication") )
				node_count++;
			else if( str_comp(data+o,"time forfeit") )// timeout - skip this game
				skip=1;
			else{
				assert(0);// unknown termiantion - exit
				exit(7);
			}
		}else if( str_comp(data+o,"FEN") ){
			o+=skip_past('"',data+o);
			F=1;
			init_board_FEN(data+o);// FEN from winboard
			/*char sss[100];
			print_position(sss,&b_m);
			node_count++;*/
		}
		// unknown []
		o+=skip_past(']',data+o);
	}

	// skip this game if something is bad
	assert(plycount>0);
	assert(result<2);
	if( skip || result>=2 || plycount==0 ){
		o1=skip_past_long('[',data+o);// here sometimes it goes past 5K - use a different function.
		o+=o1;
		o--;
		return(o);
	}

	// process moves
	if( F==0 ) init_board(0);// set b_m to initial board
	for(i=0;i<plycount;i++){	
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
		eval=0;
		if( data[o]=='{' ){// misc data present - decode it
			o++;

			// if not a book move, get stats
			if( data[o]!='b' && data[o]!='D' && data[o]!='W' && data[o]!='B' ){// book, Draw, White mates, Black mates
				// get eval
				eval=get_int(data+o);

				o1=skip_past1('/',data+o); // now at: 13 0.17s}. Do not skip past }
				o+=o1;

				// get depth
				//depth=get_int(data+o);

				o1=skip_past1(' ',data+o); // now at: 0.17s}. Do not skip past }
				o+=o1;

				// get time
				//time=get_int(data+o);
			}
			o+=skip_past('}',data+o);
		}// else: misc data absent

		// dump the position
		if( abs(eval)<=2000 // skip eval>20
			//&& popcnt64l(b_m.colorBB[0]|b_m.colorBB[1])>6 // stop if 6 or fewer pieces
			&& popcnt64l(b_m.colorBB[0]|b_m.colorBB[1])>4 // stop if 4 or fewer pieces
			&& !(b_m.halfmoveclock>20 && result==0) // stop at halfmoveclock of 20 for draws
			&& i>=12*2 // skip first 12 moves
		){
			ts_entry ts;
			convert_board_to_TS(&b_m,&ts);// convert b_m to ts entry format. This populates board, castle, player, last move and full move clock.
			ts.score=result*((i&1)?-10000:10000); // convert this to result of the player to move now
			ts.remarks=3; // 3=game outcome

			// append position to TS file
			//fwrite(&ts,sizeof(ts_entry),1,fo);
			out[pc]=ts;
			pc++;

			/*static FILE *fl=NULL;
			static unsigned int lc=0;
			if( fl==NULL ){
				fl=fopen("C:\\xde\\chess\\out\\gl.csv","w");
				fprintf(fl,"i,eval,result*,PST,FEN\n");
			}
			lc++;
			char sss[100];print_position(sss,&b_m);
			fprintf(fl,"%d,%d,%d,%d,%s",i,eval,result*((i&1)?-10000:10000),get_scorem(&b_m),sss);
			if(lc>10000){
				fclose(fl);
				exit(0);
			}*/
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
	o1=skip_past('[',data+o);
	o+=o1;
	o--;
	return(o);
}

void read_games(void){// read games from pgn file
	FILE *f=fopen("C:\\xde\\chess\\data\\games.pgn","r");
	char *data=(char*)malloc(1600*1024*1024);
	unsigned int o=0,t_size=(unsigned int)fread(data,1,1600*1024*1024,f);// input buffer
	fclose(f);

	out=(ts_entry*)malloc(sizeof(ts_entry)*50*1024*1024); // output buffer - 50 Mil entries, 2Gb
	
	// read games, 1 at a time
	pc=0;
	while( t_size>o+100 ){
		unsigned int o1=read_one_game(data+o);
		o+=o1;
		assert(o1>10);
	}

	// write results
	f=fopen("c://xde//chess//data//TSnew.bin","wb");
	fwrite(&pc,sizeof(unsigned int),1,f); // write size 
	fwrite(out,sizeof(ts_entry),pc,f); // write data
	fclose(f);
	exit(0);
}