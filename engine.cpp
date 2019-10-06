#include "chess.h"
#include <tchar.h>
#include <intrin.h>
#include "threads.h"

// Globals
#if ALLOW_LOG
FILE *f_log; // main log file. Always open. Closed/reopened when solver is called.
#endif

// Statics
static HANDLE calculate_h;				// calculation thread handle
static HANDLE hStdin;					// std in handle
static HANDLE hStdout;					// std out handle
static int time_per_move_base;			// in milliseconds
static int time_per_move;				// incremental time per move, in milliseconds
static unsigned int halfmovemade;		// number of hm's from start of the game
static int moves_remaining;
static int my_time;						// total time remaining, in milliseconds
static int ponder;						// pondering indicator
static int pondering_allowed=0;			// option****
static int GUIscore;					// score last passed to the GUI. Use it when moving instantly.
static int timeout_complete_l,timeout_l,t_min_l; // local timeouts

// Externals
extern unsigned int Threads;
extern unsigned char g_promotion;
extern unsigned int pv10[];
extern unsigned int tb_loaded;
extern board *b_s;
extern int timer_depth;
extern UINT64 tbhits;

void init_board(unsigned int mode,board *b){// init to starting board
	memset(b_m.killer,0,sizeof(b_m.killer));			// reset killers
	memset(b_m.countermove,0,sizeof(b_m.countermove));	// reset countermoves
	memset(b_m.history_count,0,sizeof(b_m.history_count));		// reset history
	init_board_FEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",b);// initial board
	clear_hash(1);										// clear hash
}

void pass_message_to_GUI(char *sss){// pass message to GUI, and log it.
	DWORD c;

	// pass command to GUI
	WriteFile(hStdout,sss,(DWORD)strlen(sss),&c,NULL);

	// and log it
	#if ALLOW_LOG
	fprintf(f_log,"[to GUI] %s",sss);
	#endif
}

static _declspec(noinline) unsigned int legal_move_count(board *b,unsigned char *list){// return legal move count. And move list.
	unsigned int i,mc,mc2;
	int in_check;

	in_check=cell_under_attack(b,b->kp[b->player-1],b->player);
	if( in_check ) mc2=get_out_of_check_moves(b,list,b->kp[b->player-1],in_check-64);
	else mc2=get_all_moves(b,list);

	// make all the moves, exclude the ones that lead into check
	unsigned int player=b->player;
	unsigned int kp0=b->kp[player-1];
	UINT64 KBB=UINT64(1)<<kp0;
	for(mc=i=0;i<mc2;++i){
		if( list[2*i]==kp0 ){// See if i'm in check after the move
			b->colorBB[player-1]^=KBB;					// update occupied BB of player. here i only need to remove the king from its current position on colorBB board.
			unsigned int t=player_moved_into_check(b,list[2*i+1],player);
			b->colorBB[player-1]^=KBB;					// update occupied BB of player.
			if( t ) continue;
		}

		// good move. Put it on the list.
		list[2*mc]=list[2*i];
		list[2*mc+1]=list[2*i+1];
		mc++;
	}
	return(mc);
}

static _declspec(noinline) unsigned int try_ponder(char *sss,board *b,unsigned char from,unsigned char to){// look in the hash for ponder move. Make sure it is legal. It it is, print it
	unmake d;
	hash_data hd;
	unsigned int c=0;
	
	// make the move
	d.promotion=0;//init to Q
	make_move(b,from,to,&d);

	// look in the hash
	if( lookup_hash(0,b,&hd,0) && move_is_legal(b,hd.move[0],hd.move[1]) ){// legal move. Print it
		#if ALLOW_LOG
		fprintf(f_log,"Found ponder move in the hash!\n");
		#endif
		c=sprintf(sss," ponder %c%c%c%c",hd.move[0]/8+'a',hd.move[0]%8+'1',hd.move[1]/8+'a',hd.move[1]%8+'1');
	}
	#if ALLOW_LOG
	else fprintf(f_log,"Did not find ponder move in the hash.\n");
	#endif


	// unmake the move
	unmake_move(b,&d);
	return(c);
}

int see_move(board *,unsigned int,unsigned int);
void delete_hash_entry(board*);
extern unsigned int MaxCardinality;
static DWORD WINAPI calculate_f(LPVOID p_unused){// calculation function
	board *b=&b_m;
	int t2,a,be,t_min,t_max,score,i,last_score=MIN_SCORE,last_score2=MIN_SCORE;// init last score(s) to MIN_SCORE
	unsigned char last_move[4],mm[4];
	char sss[300];

	// see if i have only 1 move - then move instantly
	if( !ponder ){// only if not pondering
		unsigned char list[256];
		if( legal_move_count(b,list)==1 ){// only 1 legal move
			mm[0]=list[0];mm[1]=list[1]; // move to/from

			// log final move
			#if ALLOW_LOG
			fprintf(f_log,"Only 1 legal move - making it immediately. Move:%c%c/%c%c\n",mm[0]/8+'A',mm[0]%8+'1',mm[1]/8+'A',mm[1]%8+'1');
			#endif
					
			// pass final search stats to the GUI
			sprintf(sss,"info depth 1 score cp %i time 0 nodes 1 pv %c%c%c%c\n",GUIscore,mm[0]/8+'a',mm[0]%8+'1',mm[1]/8+'a',mm[1]%8+'1');
			pass_message_to_GUI(sss);

			// pass best move to the GUI. See if promotion - add "q", always
			char prom[]="q";
			if( (b->piece[mm[0]]&7)==1 && ((mm[1]&7)==7 || (mm[1]&7)==0) ) prom[0]='q';
			else prom[0]=0;
			unsigned int c=sprintf(sss,"bestmove %c%c%c%c%s",mm[0]/8+'a',mm[0]%8+'1',mm[1]/8+'a',mm[1]%8+'1',prom);
			c+=try_ponder(sss+c,b,mm[0],mm[1]);
			sprintf(sss+c,"\n");
			pass_message_to_GUI(sss);
			return(0);
		}
	}

	solve_prep(b);
	b->em_break=0;

	// look in the TT
	hash_data hd;// hash data
	unsigned int iTT=100;
	i=1;hd.depth=0;// reset
	if( lookup_hash(0,b,&hd,0) && hd.alp==hd.be && move_is_legal(b,hd.move[0],hd.move[1]) && hd.depth>8 && abs(hd.alp)<9988 ){ // exact score and legal move. And depth is 9 or above. And not a near CM. Allowing this for lower bound is -4 ELO
		iTT=i=hd.depth;
		last_move[0]=hd.move[0];last_move[1]=hd.move[1];// record the move

		// see if this is a good capture with high enough depth - then make this move instantly. Skip promotions - they could be under, and i don't know how to handle them: +7 ELO, happens 2 times per game
		mm[0]=hd.move[0];mm[1]=hd.move[1];
		if( !ponder && !((b->piece[mm[0]]&7)==1 && ((mm[1]&7)==7 || (mm[1]&7)==0)) && hd.depth>10 && see_move(b,mm[0],mm[1])>200 ){
			// pass final search stats to the GUI
			sprintf(sss,"info depth 1 score cp %i time 0 nodes 1 pv %c%c%c%c\n",GUIscore,mm[0]/8+'a',mm[0]%8+'1',mm[1]/8+'a',mm[1]%8+'1');
			pass_message_to_GUI(sss);

			// pass best move to the GUI.
			unsigned int c=sprintf(sss,"bestmove %c%c%c%c",mm[0]/8+'a',mm[0]%8+'1',mm[1]/8+'a',mm[1]%8+'1');
			c+=try_ponder(sss+c,b,mm[0],mm[1]);
			sprintf(sss+c,"\n");
			pass_message_to_GUI(sss);
			return(0);
		}
	}else{// here i need brackets!
		#if ALLOW_LOG
		fprintf(f_log,"TT miss. TT data is: depth=%d,alp=%d,be=%d,move:%c%c%c%c\n",hd.depth,max(hd.alp,MIN_SCORE+1),min(MAX_SCORE-1,hd.be),'a'+hd.move[0]/8,'1'+hd.move[0]%8,'a'+hd.move[1]/8,'1'+hd.move[1]%8);
		#endif
	}

	// check if TT move gives opp possibility to draw by repetition - then do something.
	if( i>10 && hd.alp>80 && b->position_hist[99] ){// TT entry found, with score>+0.8, and some repetitions (position_hist only has repeaters)
		unmake d,d2;
		unsigned int in_check,mc,unmade;
		unsigned char list[256];

		d.promotion=0;//init to Q
		make_move(b,hd.move[0],hd.move[1],&d);// make my best move
		in_check=cell_under_attack(b,b->kp[b->player-1],b->player);
		if( in_check ) mc=get_out_of_check_moves(b,list,b->kp[b->player-1],in_check-64);
		else mc=get_all_moves(b,list);
		for(unsigned int j=0;j<mc;++j){// loop over all opp moves
			d2.promotion=0;//init to Q
			make_move(b,list[2*j],list[2*j+1],&d2);
			UINT64 hl=b->hash_key;
			
			// loop over previous positions from this search and earlier game
			unmade=0;
			for(unsigned int k=99;k>(unsigned int)99-b->halfmoveclock && b->position_hist[k];--k){// last: 99. Total: halfmoveclock. Also stop on first empty.
				if( b->position_hist[k]==(hl^player_zorb) || b->position_hist[k]==hl ){// found a match - reset
					#if ALLOW_LOG
					fprintf(f_log,"clearing some TT entries for repetitions! TT score=%d, TT move=%c%c%c%c\n",hd.alp,'a'+hd.move[0]/8,'1'+hd.move[0]%8,'a'+hd.move[1]/8,'1'+hd.move[1]%8);
					#endif
					delete_hash_entry(b);// delete
					unmake_move(b,&d2);
					unmade=1;
					delete_hash_entry(b);// delete
					iTT=i=0;	// start search from scratch
					break; // need a break here
				}
			}
			if( unmade==0 ) unmake_move(b,&d2);
			else break;
		}
		unmake_move(b,&d);
		if( unmade ) delete_hash_entry(b);// delete
	}
	
	// get time limit(s)
	#if ENGINE
	#define time_margin 200  // min time before stopping, msec. Make it 200 msec for releases, 30 msec for fast games.
	#else
	#define time_margin 30  // min time before stopping, msec. Make it 200 msec for releases, 30 msec for fast games.
	#endif
	float t_min_factor=1.7f; // divide remaining time by this to get t_min.
	if( pondering_allowed ) t_min_factor=1.5f;// increase time if pondering is allowed - to acccount for ponder hits
	if( moves_remaining==0 ){// Incremental time clock - ICS style.
		float max_time_mult=3.0f;// max time is this times min time.
		int mr=45-min(60,halfmovemade)/2;// Assume 45-hm/2, min 15 moves left.
		t_min=time_per_move+int((max(20,my_time-time_per_move-time_margin))/t_min_factor/mr); // X msec margin
		t_max=int(t_min*max_time_mult);
	}else{
		float max_time_mult=5.5f; // max time is this times min time.
		t_min=int(max(20,my_time)/t_min_factor/moves_remaining);
		t_max=(int)min(t_min*max_time_mult,my_time-time_margin-(time_per_move_base*(moves_remaining-1))/4);// timeout: smaller of X*t_min, remaining time-X msec, 1/4 base time for all remaining moves
	}
	t_max=min(t_max,my_time-time_margin);// overall cap at remaining time minus margin
	t_max=max(t_max,10); // but no less than 10 msec
	t_min=min(t_min,t_max); // min canot exceed max
	#if ALLOW_LOG
	fprintf(f_log,"moves remaining:%d. Min time: %d msec. Max time: %d msec. Standard move time: %d msec.\n",moves_remaining,t_min,t_max,time_per_move_base);
	print_position(sss,b);
	fprintf(f_log,"%s",sss);
	#endif

	unsigned char m_h_l[47][2];memset(m_h_l,0,sizeof(m_h_l));//local move history
	t_min_l=time_start+t_min; // init local min time
	b_m.max_ply=0;// init
	tbhits=0;// init
	for(;i<47 && !b->em_break;++i){//*************************************************************************************************************** iterative deepening loop
		depth0=i;
		timeout_complete_l=timeout_l=time_start+t_max;		// starts from beginning of solve!
		t2=get_time();
		timeout_l=t2+max(0,timeout_l-t2)/2;	// cut remaining time by half for searching first move. No impact on ELO.
		
		// only apply timeout if not pondering
		if( !ponder ){
			timeout_complete=timeout_complete_l;
			timeout=timeout_l;
		}else{
			timeout_complete=2000000000;
			timeout=2000000000-1;
		}

		if( i==hd.depth && iTT==i ){// use TT "as is", do not search it. This is faster.
			b->move_hist[0][0][0]=hd.move[0];b->move_hist[0][0][1]=hd.move[1];b->move_hist[0][1][0]=b->move_hist[0][1][1]=8; // TT cut-off
			score=hd.alp;// only works for exact TT score
			b_m.node_count=1; // to avoid zero
			// pass this to GUI
			// second move is always invalid - do not pass it
			sprintf(sss,"info depth %u score cp %i time %u nodes %I64u pv %c%c%c%c\n",i,score,0,b_m.node_count,b->move_hist[0][0][0]/8+'a',b->move_hist[0][0][0]%8+'1',b->move_hist[0][0][1]/8+'a',b->move_hist[0][0][1]%8+'1');
			pass_message_to_GUI(sss);
		}else{
			// get aspiration window
			a=MIN_SCORE;be=MAX_SCORE;
			int w=MAX_SCORE;
			if( abs(last_score)<2500 ){// previous score is good - use aspiration window
				w=80; // base
				if( depth0<9 )
					w+=60*(9-depth0); // grade by depth: +0 for d=9, +300 for d=4.
				if( abs(last_score)>500 )
					w+=(abs(last_score)-500)/4; // grade by score: +0 for +-500, +100 for +-900
				if( depth0>9 )
					w-=4*min(5,depth0-9); // reduce window by 20 for d=14+.
				a=last_score-w;
				be=last_score+w;
			}
			do{	
				score=Msearch(b,i,0,a,be,1); // call search**************************************************
				if( (score<=MIN_SCORE || score>=MAX_SCORE) && !b->em_break ){
					clear_hash(1);// clear hash if bad score
					#if ALLOW_LOG
					fprintf(f_log," bad score - clear hash!\n");
					#endif
				}
				if( score<=a ){// fail low - move is bad, reset it.
					#if ALLOW_LOG
					if( !b->em_break ) fprintf(f_log," asp fail low. depth=%d,alp=%d,be=%d,score=%d\n",i,a,be,score);
					#endif
					b->move_hist[0][0][0]=b->move_hist[0][0][1]=0;
					// increase the window - by a factor of 2.
					w*=2;a=max(MIN_SCORE,score-w);be=min(MAX_SCORE,score+w);
				}else if( score>=be ){// fail high - keep the move.
					// save current move and score. +4 ELO.
					if( move_is_legal(b,b->move_hist[0][0][0],b->move_hist[0][0][1]) ){// check if current move is legal - it is.
						if( abs(score)<10100 ){// good score. Store it
							#if ALLOW_LOG
							fprintf(f_log," asp fail high and move is good. depth=%d,alp=%d,be=%d,score=%d\n",i,a,be,score);
							#endif
							last_score=score;
						}else{// bad score - don't save it.
							#if ALLOW_LOG
							fprintf(f_log," asp fail high and move is bad. depth=%d,alp=%d,be=%d,score=%d\n",i,a,be,score);
							#endif
						}
						((unsigned int*)&last_move[0])[0]=((unsigned int*)&b->move_hist[0][0][0])[0];
					}
					// increase the window - by a factor of 2.
					w*=2;a=max(MIN_SCORE,score-w);be=min(MAX_SCORE,score+w);
				}else
					break;
			}while( !b->em_break );
		}
		m_h_l[i][0]=b->move_hist[0][0][0];m_h_l[i][1]=b->move_hist[0][0][1];// record move history
		t2=get_time();

		// check if current move is legal
		if( !move_is_legal(b,b->move_hist[0][0][0],b->move_hist[0][0][1]) ){// no it isn't. Replace with good one
			score=last_score;																// score
			((unsigned int*)&b->move_hist[0][0][0])[0]=((unsigned int*)&last_move[0])[0];	// move
			depth0--;																		// decr depth, to indicate incomplete search
		}else{// store last good score and moves
			// taking away this logic - exclusion of bad scores - is -10 ELO. Why?
			if( abs(score)<13100 ){// good score. Store it
				last_score2=last_score;
				last_score=score;
			}else// bad score. Use last one
				score=last_score;
			((unsigned int*)&last_move[0])[0]=((unsigned int*)&b->move_hist[0][0][0])[0];
		}
		mm[0]=b->move_hist[0][0][0];mm[1]=b->move_hist[0][0][1]; // move from/to

		#if ALLOW_LOG
		if( ponder==1 ) fprintf(f_log,"pondering,");

		// is score bad?
		if( score==MIN_SCORE ) fprintf(f_log,"score=MIN_SCORE!\n aspiration window:%d,%d\n",a,be);
		if( b->em_break && !ponder )// timeout. Log it
			fprintf(f_log,"Timeout at time %u!\n",t2-time_start);

		// update log file if it took too long (10+sec)
		if( t2-time_start>10000 ){
			fclose(f_log);f_log=fopen(LOG_FILE1,"a");
		}
		#endif

		// timing adjustments
		int t_min2=t_min;
		if( last_score2>-20000 && abs(score-last_score2)>110 )// incr time if score changes by more that 1.1 pawns
			t_min2=(int)(t_min*2.2);
		else if( i>7 && (m_h_l[i-1][0]+m_h_l[i-1][1]) && (m_h_l[i][0]!=m_h_l[i-1][0] || m_h_l[i][1]!=m_h_l[i-1][1]) )// last 2 moves are different. Incr time.
			t_min2=(int)(t_min*1.6);

		// timeout? Only if not pondering.
		t_min_l=time_start+t_min2; // used in ponder hit logic
		if( !ponder && (t2-time_start>t_min2			// more than t_min2 has passed
		|| t2-time_start>(int)(t_max*0.75)) )			// or more than X% of t_max has passed
			break;
	}// end of iterative deepening loop*************************************************************************************************************************

	// if pondering, wait here
	while( ponder && !b->em_break ){// wait till pondering is turned off, or till emergency break.
		#if ALLOW_LOG
		fprintf(f_log,"Sleeping in ponder thread...\n");
		fclose(f_log);f_log=fopen(LOG_FILE1,"a");
		#endif
		Sleep(20);
	}

	// pass final search stats to the GUI
	int te=get_time()-time_start; // elapsed time
	unsigned int c=sprintf(sss,"info depth %u seldepth %u score cp %i time %u nodes %I64u nps %I64u tbhits %I64u",depth0,b_m.max_ply,score,te,b_m.node_count,(b_m.node_count*1000)/max(1,te),tbhits);
	if( te>1000 ) c+=sprintf(sss+c," hashfull %d",hashfull());
	c+=sprintf(sss+c," pv %c%c%c%c",mm[0]/8+'a',mm[0]%8+'1',mm[1]/8+'a',mm[1]%8+'1');
	for(unsigned int lc1=1;lc1<min(depth0,MAX_MOVE_HIST) && b->move_hist[0][lc1][0]!=b->move_hist[0][lc1][1];++lc1)
		c+=sprintf(sss+c," %c%c%c%c",b->move_hist[0][lc1][0]/8+'a',b->move_hist[0][lc1][0]%8+'1',b->move_hist[0][lc1][1]/8+'a',b->move_hist[0][lc1][1]%8+'1');
	sprintf(sss+c,"\n");
	pass_message_to_GUI(sss);

	// pass best move to the GUI. See if promotion - add "q"
	// "bestmove" command must always be sent if the engine stops searching, also in pondering mode if there is a "stop" command, so for every "go" command a "bestmove" command is needed!
	static const char p0[]="qrbn";
	char prom[]="q";
	if( (b->piece[mm[0]]&7)==1 && ((mm[1]&7)==7 || (mm[1]&7)==0) ) prom[0]=p0[g_promotion];
	else prom[0]=0;
	c=sprintf(sss,"bestmove %c%c%c%c%s",mm[0]/8+'a',mm[0]%8+'1',mm[1]/8+'a',mm[1]%8+'1',prom);
	if( b->move_hist[0][1][0]!=b->move_hist[0][1][1] ) c+=sprintf(sss+c," ponder %c%c%c%c",b->move_hist[0][1][0]/8+'a',b->move_hist[0][1][0]%8+'1',b->move_hist[0][1][1]/8+'a',b->move_hist[0][1][1]%8+'1'); // valid ponder move
	else c+=try_ponder(sss+c,b,mm[0],mm[1]);
	sprintf(sss+c,"\n");
	pass_message_to_GUI(sss);
	GUIscore=score; // store it for the future
	return(0);
}

static unsigned int str_comp(char *input,char * command){
	unsigned int i;
	for(i=0;i<strlen(command);++i)
		if( input[i]!=command[i] )
			return(0);
	return(1);
}

static _declspec(noinline) void stop_th(void){// stop pondering thread
	if( calculate_h!=NULL ){
		int tol=get_time()-1000;
		DWORD ret1;
		do{
			timer_depth=0;
			timeout_complete=timeout=tol; // set timeout to "in the past"
			// break all slaves. And master.
			b_m.em_break=1;
			for(unsigned int i=0;i<(unsigned int)slave_count;++i) b_s[i].em_break=1;
			ret1=WaitForSingleObject(calculate_h,100);// wait for thread to terminate. Put a time limit here - 0.1 seconds
			#if ALLOW_LOG
			if( ret1==WAIT_TIMEOUT ) fprintf(f_log,"Problem: closing calculation thread...\n");
			#endif
		}while( ret1==WAIT_TIMEOUT );
		CloseHandle(calculate_h);
		calculate_h=NULL;
		#if ALLOW_LOG
		fprintf(f_log,"  Calculation thread stopped.\n");
		#endif
	}
}

unsigned int init_tablebases(char*);
int _cdecl _tmain(int argc, _TCHAR* argv[]){
	char *input=(char *)malloc(1024*16);			// input buffer
	UINT64 *hash_history=(UINT64*)malloc(1200*8);	// history of all hashes in the current game.
	board b_l;										// local board
	unsigned int i,j,k,l,offset;
	int base_moves=0;
	DWORD read_counter;
	char sss[300];
	
	setbuf(stdout, NULL);
    setbuf(stdin, NULL);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
	hStdin=GetStdHandle(STD_INPUT_HANDLE);	// get stdin handle
	hStdout=GetStdHandle(STD_OUTPUT_HANDLE);// get stdout handle
	#if ALLOW_LOG
	f_log=fopen(LOG_FILE1,"a");// open main log file. Keep it open. Append.
	#endif
	init_all(0);								// perform initializations
	while(1){// start of main - infinite - loop ***************************************************************************
		unsigned int rr=ReadFile(hStdin,input,1024*16-1,&read_counter,NULL);
		if( input[read_counter]==13 || input[read_counter]==10 ) // skip one new line
			read_counter--;
		input[read_counter]=0; // terminate the string

		// parse input string
		offset=0;// start parsing from the start
		while( input[offset]==10 || input[offset]==13 || input[offset]==' ' )// skip blanks
			offset++;
		if(read_counter<=offset)// skip if only input is "\n"
			continue;

		#if ALLOW_LOG
		fprintf(f_log,"[IN]%s\n",input);// copy input to log file
		#endif
		do{ // loop over input commands
			/*if( str_comp(input+offset,"option") ){// input options. Like in option param=0.5
				offset+=7;// now point to beginning of "pX"

				int value;
				sscanf(input+offset+3,"%d",&value);

				if( str_comp(input+offset,"p1=") ){
					param1=value;
					futility_margin[1]=value;
					#if ALLOW_LOG
					fprintf(f_log,"    [do] param 1 is set to %d\n",param1);
					#endif
				}else if( str_comp(input+offset,"p2=") ){
					param2=value;
					#if ALLOW_LOG
					fprintf(f_log,"    [do] param 2 is set to %d\n",param2);
					#endif
				}

				while( input[offset]!=10 && input[offset]!=13 && input[offset]!=' ' && input[offset]!=0 ) // skip the value
					offset++;
			}else*/
			if( str_comp(input+offset,"ucinewgame") ){// ********************************************************************************* ucinewgame
				// logic for parameter tuning*********************************************************************************************
				/*#define num_par 1 // number of parameters to be tuned
				int c0[num_par];
				
				// read base coeffs
				FILE *fc=fopen("c://xde//chess//out//coeffs.bin","rb");fread(c0,sizeof(c0[0]),num_par,fc);fclose(fc);	
				param1=c0[0];
				printf("info param1 %d\n",param1);
				// end of logic for parameter tuning**************************************************************************************
				*/
				stop_th();// stop pondering thread
				offset+=11;
				init_board(0,&b_m);
				//init_board_FEN("rn3rk1/pp1bppbp/1q1p1np1/2pP4/4PB2/1NN5/PPP1BPPP/R2QR1K1 b - - 8 11");// test board
				halfmovemade=base_moves=0;
				#if ALLOW_LOG
				fprintf(f_log,"    [DO]new\n");
				#endif
			}else if( str_comp(input+offset,"quit") ){// ********************************************************************************* quit
				#if ALLOW_LOG
				fprintf(f_log,"    [DO]quit\n");
				fclose(f_log);
				#endif
				exit(0);
			}else if( str_comp(input+offset,"uci") && (input[offset+3]==0 || input[offset+3]==10 || input[offset+3]==13) ){// ************ uci
				static const char oo[2][6]={"false","true"};
				offset+=3;
				sprintf(sss,"id name Fizbo 2\n");
				pass_message_to_GUI(sss);
				sprintf(sss,"id author Youri Matiounine\n");
				pass_message_to_GUI(sss);
				sprintf(sss,"option name Threads type spin default %d min 1 max 56\n",Threads);
				pass_message_to_GUI(sss);
				sprintf(sss,"option name Hash type spin default %d min 1 max 65536\n",int((UINT64(1)<<(HBITS-20))*sizeof(hash)));
				pass_message_to_GUI(sss);
				sprintf(sss,"option name Ponder type check default %s\n",oo[pondering_allowed]);
				pass_message_to_GUI(sss);
				sprintf(sss,"option name SyzygyPath type string default <empty>\n");
				pass_message_to_GUI(sss);
				sprintf(sss,"option name UseEGTBInsideSearch type check default %s\n",oo[UseEGTBInsideSearch]);
				pass_message_to_GUI(sss);
				sprintf(sss,"option name EGTBProbeLimit type spin default %d min 0 max 6\n",EGTBProbeLimit);
				pass_message_to_GUI(sss);
				sprintf(sss,"uciok\n");
				pass_message_to_GUI(sss);
			}else if( str_comp(input+offset,"isready") ){// ****************************************************************************** isready
				offset+=7;
				sprintf(sss,"readyok\n");
				pass_message_to_GUI(sss);
			}else if( str_comp(input+offset,"setoption name") ){// *********************************************************************** setoption
				offset+=15;// blank
				if( str_comp(input+offset,"Threads value") ){// ************************************************************************** setoption Threads
					offset+=14;
					i=input[offset]-'0';
					offset++;
					if( input[offset]>='0' && input[offset]<='9' ){// second digit?
						i=i*10+input[offset]-'0';
						offset++;
					}
					Threads=min(56,max(1,i)); // apply min and max
					init_threads(Threads-1);
					#if ALLOW_LOG
					fprintf(f_log,"    [DO]use %u cores\n",i);
					#endif
				}else if( str_comp(input+offset,"Hash value") ){// *********************************************************************** setoption Hash
					offset+=11;
					i=input[offset]-'0';
					offset++;
					while( input[offset]>='0' && input[offset]<='9' ){// second digit?
						i=i*10+input[offset]-'0';
						offset++;
					}
					i=i+(i>>2);// increase by 25%, just in case
					i=min(65536,max(1,i)); // apply min and max
					UINT64 ii=i;
					ii=ii*1024;
					ii=ii*1024;
					ii=ii/sizeof(hash); // this approach (step by step) allows 4Gb or more to be used
					DWORD bit;
					BSR64l(&bit,ii);
					HBITS=bit;
					init_hash();
					#if ALLOW_LOG
					fprintf(f_log,"    [DO]use %d Mb of memory\n",int((UINT64(1)<<(HBITS-20))*sizeof(hash)));
					#endif
				}else if( str_comp(input+offset,"SyzygyPath value") ){// **************************************************************** setoption SyzygyPath
					offset+=17;// skip blank
					i=0;
					if( input[offset]==';' ) offset++;
					while( input[offset]!=10 && input[offset]!=13 && input[offset]!=0 ) sss[i++]=input[offset++];// skip to next non-blank (allow spaces for path!)
					sss[i]=0;// terminator
					if( i ){
						#if USE_EGTB
						tb_loaded=init_tablebases(sss);// try to load EGTB from new path
						#endif
						#if ALLOW_LOG
						fprintf(f_log,"    [DO]incoming EGTB path:%s. Now have %d tables\n",sss,tb_loaded);
						#endif
					}
				}else if( str_comp(input+offset,"Ponder value") ){// ********************************************************************* setoption Ponder
					offset+=13;// skip blank
					if( str_comp(input+offset+1,"rue") ) // True, bot T and t.
						pondering_allowed=1;
					else// assume false
						pondering_allowed=0;
					#if ALLOW_LOG
					fprintf(f_log,"    [DO]pondering allowed: %d\n",pondering_allowed);
					#endif
					while( input[offset]!=10 && input[offset]!=13 && input[offset]!=' ' && input[offset]!=0 ) offset++;// skip to next non-blank
				}else if( str_comp(input+offset,"UseEGTBInsideSearch value") ){// ********************************************************************* setoption UseEGTBInsideSearch
					offset+=26;// skip blank
					if( str_comp(input+offset+1,"rue") ) // True, bot T and t.
						UseEGTBInsideSearch=1;
					else// assume false
						UseEGTBInsideSearch=0;
					#if ALLOW_LOG
					fprintf(f_log,"    [DO]UseEGTBInsideSearch: %d\n",UseEGTBInsideSearch);
					#endif
					while( input[offset]!=10 && input[offset]!=13 && input[offset]!=' ' && input[offset]!=0 ) offset++;// skip to next non-blank
				}else if( str_comp(input+offset,"EGTBProbeLimit value") ){// ********************************************************************* setoption EGTBProbeLimit
					offset+=21;
					EGTBProbeLimit=input[offset]-'0';
					offset++;
					#if ALLOW_LOG
					fprintf(f_log,"    [DO]EGTBProbeLimit: %d\n",EGTBProbeLimit);
					#endif
				}
			}else if( str_comp(input+offset,"position") ){// ***************************************************************************** position
				offset+=9;// skip blank
				halfmovemade=0;
				if( str_comp(input+offset,"startpos") ){
					init_board_FEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",&b_m);// initial board
					offset+=9;// skip "startpos "
				}else{
					offset+=4;// skip "fen "
					init_board_FEN(input+offset,&b_m);// FEN from winboard
					while( input[offset]!=10 && input[offset]!=13  && input[offset]!='m') offset++;// skip FEN: it does not end on space, go to the new line! Or to "moves"
					if( input[offset]!='m' ) offset++;
				}
				
				// add new hash
				UINT64 hl=b_m.hash_key;
				if( b_m.player==2 ) hl^=player_zorb;
				hash_history[halfmovemade++]=hl;

				// process moves
				if( str_comp(input+offset,"moves") ){// ********************************************************************************* position moves
					offset+=6;// blank
					while( input[offset]>='a' && input[offset]<='h' && input[offset+1]>='1' && input[offset+1]<='8' && input[offset+2]>='a' && input[offset+2]<='h' && input[offset+3]>='1' && input[offset+3]<='8' ){// move made - process it
						i=input[offset++]-'a';j=input[offset++]-'1';// from
						k=input[offset++]-'a';l=input[offset++]-'1';// to
						unsigned char m=input[offset++];// promotion to piece "m"
						unmake d;
						d.promotion=0;
						make_move(&b_m,i*8+j,k*8+l,&d); // this updates player and sign of score.
						if( m!=0 && m!=' ' && m!=10 && m!=13 ){// promotion to other than queen. Change the piece. And update TT hash and slider bitboards.
							if( m=='n') b_m.piece[k*8+l]=2+(b_m.piece[k*8+l]&(64+128));// to knight
							else if( m=='b') b_m.piece[k*8+l]=3+(b_m.piece[k*8+l]&(64+128));// to bishop
							else if( m=='r') b_m.piece[k*8+l]=4+(b_m.piece[k*8+l]&(64+128));// to rook
							b_m.hash_key=get_TT_hash_key(&b_m); // update TT hash
							set_bitboards(&b_m); // set bitboards
							b_m.mat_key=get_mat_key(&b_m);// set material key
							offset++;
						}
			
						// add new hash after each move
						hl=b_m.hash_key;
						if( b_m.player==2 ) hl^=player_zorb;
						hash_history[halfmovemade++]=hl;
					}
				}
				#if ALLOW_LOG
				print_position(sss,&b_m);
				fprintf(f_log,"    [DO]incoming FEN board:%s",sss);
				#endif
				// save board - need to look at it while pondering
				b_l=b_m;
			}else if( str_comp(input+offset,"stop") ){// ********************************************************************************* stop
				offset+=5;
				#if ALLOW_LOG
				fprintf(f_log,"    [DO]stopping calculation thread...\n");
				#endif
				stop_th();// stop pondering thread
			}else if( str_comp(input+offset,"ponderhit") ){// ***************************************************************************** ponderhit
				offset+=10;
				ponder=0;			// turn pondering off
				hash_data hd;// hash data
				int tn=get_time();
				unsigned char list[256];

				// stop the calc! I already spent 1/0 of min and max time. Or only 1 move. Or good capture.
				if( tn>t_min_l
					|| legal_move_count(&b_l,list)==1
					|| (lookup_hash(0,&b_l,&hd,0) && hd.alp==hd.be && move_is_legal(&b_l,hd.move[0],hd.move[1]) && hd.depth>10 && abs(hd.alp)<9988 && !((b_l.piece[hd.move[0]]&7)==1 && ((hd.move[1]&7)==7 || (hd.move[1]&7)==0)) && see_move(&b_l,hd.move[0],hd.move[1])>200) 
				){
					#if ALLOW_LOG
					if( tn>t_min_l ) i=1;
					else if( legal_move_count(&b_l,list)==1 ) i=2;
					else i=3;
					fprintf(f_log,"    [DO]ponderhit - terminate the search right now for reason %d. Time elapsed %d. Time for search is min/max/max1 %d/%d/%d\n",i,tn-time_start,t_min_l-time_start,timeout_complete_l-time_start,timeout_l-time_start);
					#endif
					timer_depth=0;
					my_time+=tn-time_start; // increase apparent time limit, so that i don't see this as a timeout.
					timeout=timeout_complete=tn-1000; // in the past - break now.
				}else{// here i could increase my time by elapsed time and recompute min/max. Not doing that is conservative.
					if( timeout_complete==timeout ){
						timeout=timeout_complete=timeout_complete_l;			// restore timeouts - both the same (long)
					}else{
						timeout_complete=timeout_complete_l;timeout=timeout_l;	// restore timeouts - different ones
					}
					#if ALLOW_LOG
					fprintf(f_log,"    [DO]ponderhit - switch to normal search. Time elapsed %d. Time for search is min/max/max1 %d/%d/%d\n",tn-time_start,t_min_l-time_start,timeout_complete_l-time_start,timeout_l-time_start);
					#endif
				}
			}else if( str_comp(input+offset,"go") ){// *********************************************************************************** go
				offset+=3;
				ponder=0;
				moves_remaining=0;
				while(1){// infinite loop over go parameters
					if( str_comp(input+offset,"wtime") ){// ********************************************************************************** go wtime
						offset+=6;
						// get TIME
						i=input[offset]-'0';offset++;
						while( input[offset]>='0' && input[offset]<='9'){
							i=i*10+input[offset]-'0';offset++;
						}
						offset++;
						if( b_m.player==1 ){// i am white, so set my time
							my_time=i;
							#if ALLOW_LOG
							fprintf(f_log,"    [DO]my total clock set to %u msec\n",i);
							#endif
						}// else - opp time
					}else if( str_comp(input+offset,"btime") ){// ********************************************************************************** go btime
						offset+=6;
						// get TIME
						i=input[offset]-'0';offset++;
						while( input[offset]>='0' && input[offset]<='9'){
							i=i*10+input[offset]-'0';offset++;
						}
						offset++;
						if( b_m.player==2 ){// i am black, so set my time
							my_time=i;
							#if ALLOW_LOG
							fprintf(f_log,"    [DO]my total clock set to %u msec\n",i);
							#endif
						}// else - opp time
					}else if( str_comp(input+offset,"winc") ){// *********************************************************************************** go winc
						offset+=5;
						// get TIME
						i=input[offset]-'0';offset++;
						while( input[offset]>='0' && input[offset]<='9'){
							i=i*10+input[offset]-'0';offset++;
						}
						offset++;
						if( b_m.player==1 ){// i am white, so set my time
							time_per_move=i;
							#if ALLOW_LOG
							fprintf(f_log,"    [DO]my incremental clock set to %u msec\n",i);
							#endif
						}// else - opp time
					}else if( str_comp(input+offset,"binc") ){// *********************************************************************************** go binc
						offset+=5;
						// get TIME
						i=input[offset]-'0';offset++;
						while( input[offset]>='0' && input[offset]<='9'){
							i=i*10+input[offset]-'0';offset++;
						}
						offset++;
						if( b_m.player==2 ){// i am black, so set my time
							time_per_move=i;
							#if ALLOW_LOG
							fprintf(f_log,"    [DO]my incremental clock set to %u msec\n",i);
							#endif
						}// else - opp time
					}else if( str_comp(input+offset,"movestogo") ){// ****************************************************************************** go movestogo
						offset+=10;
						// get value
						i=input[offset]-'0';offset++;
						while( input[offset]>='0' && input[offset]<='9'){
							i=i*10+input[offset]-'0';offset++;
						}
						offset++;
						moves_remaining=i; // assign to "base moves". Here 0 means that it is ICS incremental time control.
						#if	ALLOW_LOG
						fprintf(f_log,"    [DO]moves to go is set to %u\n",moves_remaining);
						#endif
					}else if( str_comp(input+offset,"ponder") ){// ********************************************************************************* go ponder
						offset+=7;
						ponder=1;
						#if ALLOW_LOG
						fprintf(f_log,"    [DO]ponder\n");
						#endif
					}else if( str_comp(input+offset,"infinite") ){// ******************************************************************************* go infinite
						offset+=9;
						my_time=2000000000;// 2B msec
						#if ALLOW_LOG
						fprintf(f_log,"    [DO]unlimited search\n");
						#endif
					/*}else if( str_comp(input+offset,"searchmoves") || str_comp(input+offset,"depth") || str_comp(input+offset,"nodes")
						|| str_comp(input+offset,"mate") || str_comp(input+offset,"movetime")){// ************************************************** go searchmoves/depth/nodes/mate/movetime
						// to do: something*/
					}else break;// unknown "go" parameter - end of go command.
				}// end of loop over "go" parameters
				time_start=get_time(); // start counting time as soon as "go" parameters are processed.
				if( moves_remaining>=base_moves && moves_remaining ){// do this oputside the loop, so that both time and moves to go are set
					base_moves=moves_remaining;
					time_per_move_base=my_time/base_moves; // update base time per move
					#if ALLOW_LOG
					fprintf(f_log,"    [DO]set standard time per move to %u msec.\n",time_per_move_base);
					#endif
				}
				// now, go!

				// put previous game positions on position_hist, starting with 99. Only include repeated positions!
				if( halfmovemade ){
					for(k=i=0;i<=min(halfmovemade-1,b_m.halfmoveclock);++i){
						for(j=i+1;j<=min(halfmovemade-1,b_m.halfmoveclock);++j)
							if( hash_history[halfmovemade-1-j]==hash_history[halfmovemade-1-i] ){
								b_m.position_hist[99-k]=hash_history[halfmovemade-1-i];
								k++;
								break;
							}
					}
					b_m.position_hist[99-k]=0;//terminator
					#if ALLOW_LOG
					if( k ) fprintf(f_log,"Found %d repeaters.\n",k);
					#endif
				}else
					b_m.position_hist[99]=0;//terminator
			
				// stop pondering thread
				stop_th();

				// call the solver
				TTage=(TTage+1)&3; // increment TT age before the solve.
				if( calculate_h!=NULL ){
					CloseHandle(calculate_h);
					calculate_h=NULL;
				}
				calculate_h=CreateThread(NULL,0,calculate_f,0,0,NULL);//sec.attr., stack, function, param,flags, thread id
				// end of "go" logic
			}else{
				#if ALLOW_LOG
				fprintf(f_log,"unrecognized command - ignore. Command is:%s\n",input+offset);
				#endif
				while( input[offset]!=10 && input[offset]!=13 && input[offset]!=' ' && input[offset]!=0 ) // skip unrecognized command
					offset++;
			}

			// skip CR and new line
			while( input[offset]==10 || input[offset]==13 || input[offset]==' ' )
					offset++;

			// close/reopen log file. This is the only place where it is closed.
			#if ALLOW_LOG
			fclose(f_log);
			f_log=fopen(LOG_FILE1,"a");
			#endif
		}while(read_counter>offset);
	}// end of infinite loop
	return(0);
}