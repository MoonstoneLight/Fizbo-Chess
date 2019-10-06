#include "chess.h"
#include "threads.h"
#include <math.h>

#define MAX_LOADSTRING 100
#define OFFSET 25						// margin for labels: top and left.
#define MESS 25							// margin for messages: bottom.
#define MARGIN 10						// right margin: right

// Global vars
HWND hWnd_global;						// current window. Need it for "message box".
#if ALLOW_LOG
FILE *f_log=NULL;						//main log file. Kept open during the s.
#endif

// Static vars
static UINT64 countI;					// counter, for STS runs
static char text1[100]=" ";				// message to the screen
static char winboard_FEN[180];

// Forward declarations of functions included in this code module:
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);
void read_nn_coeffs(void);
void read_nn2_coeffs(void);

void init_board(unsigned int mode,board *b){// init to starting board. 100 takes board as input
	memset(b->killer,0,sizeof(b->killer));			// reset killers
	memset(b->countermove,0,sizeof(b->countermove));	// reset countermoves
	memset(b->history_count,0,sizeof(b->history_count));		// reset history
	b->scoree=b->scorem=0;
	if(mode==100){
		init_board_FEN(winboard_FEN,b);// FEN from winboard
		return;
	}
	switch(mode){
	case 0: init_board_FEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 0",b);// initial board
		break;
	case 1: init_board_FEN("r2qr1kb/p3pb1p/1pp2pp1/4Pn2/2pPNB2/2N5/PPP1QPPP/R2R2K1 w - - 0 0",b);// early game - only down 1 minor piece. Stable.
		break;
	case 2: init_board_FEN("1r2q1k1/ppp2p2/2np3p/3R1Pp1/P1P1r3/2P1B2P/Q3PP2/2K4R b - - 0 0",b);// midgame. Semi-stable. Down 3 pieces each.
		break;
	case 3:	init_board_FEN("5k2/7p/p6P/P2n1p2/3p2p1/2p5/2K5/7R b - - 0 0",b);// endgame pawn promotion
		break;
	case 4:	init_board_FEN("8/k7/3p4/p2P1p2/P2P1P2/8/8/K7 w - -",b); // fine 70 // white wins black loses
		break;
	}
}

int APIENTRY WinMain(_In_ HINSTANCE hInstance,_In_opt_ HINSTANCE hPrevInstance,_In_ LPTSTR lpCmdLine,_In_ int nCmdShow){
 	MSG msg;
	HACCEL hAccelTable;
	WNDCLASSEX wcex;
	HWND hWnd;
	TCHAR szTitle[MAX_LOADSTRING];					// The title bar text
	TCHAR szWindowClass[MAX_LOADSTRING];			// the main window class name

	#if TRAIN
	SetPriorityClass(GetCurrentProcess(),IDLE_PRIORITY_CLASS); // set priority to low for training
	#else
	//SetPriorityClass(GetCurrentProcess(),HIGH_PRIORITY_CLASS); // set priority to high
	SetPriorityClass(GetCurrentProcess(),IDLE_PRIORITY_CLASS); // set priority to low for training
	#endif

	// Initialize global strings
	LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadString(hInstance, IDC_CHESS, szWindowClass, MAX_LOADSTRING);

	wcex.cbSize			= sizeof(WNDCLASSEX);
	wcex.style			= CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc	= WndProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= LoadIcon(hInstance, MAKEINTRESOURCE(IDI_CHESS));
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName	= MAKEINTRESOURCE(IDC_CHESS);
	wcex.lpszClassName	= szWindowClass;
	wcex.hIconSm		= LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));
	RegisterClassEx(&wcex);
	
	// Perform application initialization:
   hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);
   hWnd_global=hWnd;
   if (!hWnd)
      return FALSE;
   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

	hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_CHESS));

	// open log file
	#if ALLOW_LOG
	f_log=fopen(LOG_FILE1,"w");
	#endif

	// perform initializations
	init_all(0);
	read_nn_coeffs();
	read_nn2_coeffs();

	// Main message loop:
	while (GetMessage(&msg, NULL, 0, 0)){
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)){
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return (int) msg.wParam;
}

int solve(unsigned char *mm,unsigned int *tt,unsigned int dd,board *b){// Return score.
	DWORD t2;
	unsigned int i=1;
	int score=MIN_SCORE,mate_count=0;
	unsigned char last_move[4];
	
	b->max_ply=0;//init
	solve_prep(b);
	b->position_hist[99]=0;//zero out past positions from the game
	
	#if ALLOW_LOG
	if( f_log==NULL ) f_log=fopen(LOG_FILE1,"a");
	char s[180];
	print_position(s,b);
	fprintf(f_log,"%s",s);
	#endif
	time_start=get_time();

	// look in the TT
	hash_data hd;// hash data
	if( lookup_hash(0,b,&hd,0) ){// hash found
		if( hd.alp==hd.be){ // exact score. Start with book depth.
			i=hd.depth;
		}
	}
	
	#if ALLOW_LOG
	fprintf(f_log,"Score,%.2f,Time,0,Depth,0 x 0,Count,0, Move\n",eval(b)/100.);
	#endif
	int last_score=MIN_SCORE;
	t2=get_time();
	for(;i<=dd;++i){
		b->em_break=0;
		depth0=i;

		// get aspiration window
		int a=MIN_SCORE,be=MAX_SCORE,w=80;
		if( abs(score)<2500 ){// previous score is good - use aspiration window
			if( depth0<9 )
				w+=60*(9-depth0); // grade by depth: +0 for d=9, +300 for d=4.
			if( abs(score)>500 )
				w+=(abs(score)-500)/4; // grade by score: +0 for +-500, +100 for +-900
			if( depth0>9 )
				w-=4*min(5,depth0-9); // reduce window by 18 for d=14+.
			a=score-w;be=score+w;
		}
		do{	
			score=Msearch(b,i,0,a,be,1); // type=PV.
			if( score<=a ){// fail low - move is bad, reset it.
				#if ALLOW_LOG
				fprintf(f_log,"   OOB low,%d,%d,%d\n",a,be,score);
				#endif
				b->move_hist[0][0][0]=b->move_hist[0][0][1]=0;
				// increase the window - by a factor of 2.
				w*=2;a=max(MIN_SCORE,score-w);be=min(MAX_SCORE,score+w);
			}else if( score>=be ){// fail high - keep the move.
				#if ALLOW_LOG
				fprintf(f_log,"   OOB high,%d,%d,%d\n",a,be,score);
				#endif
				if( move_is_legal(b,b->move_hist[0][0][0],b->move_hist[0][0][1]) )// check if current move is legal - it is.
					((unsigned int*)&last_move[0])[0]=((unsigned int*)&b->move_hist[0][0][0])[0]; // save the move
				// increase the window - by a factor of 2.
				w*=2;a=max(MIN_SCORE,score-w);be=min(MAX_SCORE,score+w);
			}else
				break;
		}while( !b->em_break );
		#if ALLOW_LOG
		if( b->em_break ) fprintf(f_log,"Break %d!\n",b->em_break);
		#endif
		t2=get_time();
		if( move_is_legal(b,b->move_hist[0][0][0],b->move_hist[0][0][1]) ){
			mm[0]=b->move_hist[0][0][0]; // move from
			mm[1]=b->move_hist[0][0][1]; // move to
		}else{
			((unsigned int*)&b->move_hist[0][0][0])[0]=((unsigned int*)&last_move[0])[0]; // restore last move
			if( move_is_legal(b,b->move_hist[0][0][0],b->move_hist[0][0][1]) ){
				mm[0]=b->move_hist[0][0][0]; // move from
				mm[1]=b->move_hist[0][0][1]; // move to
			}
		}
		#if ALLOW_LOG
		if( i==dd ){// last try - log it, to get final time
			fprintf(f_log,"Score,%.2f,Time,%i,Depth,%d x %d,Count,%I64u, Move",score/100.,t2-time_start,i,b->max_ply,b->node_count);
			fprintf(f_log,",%c%c%c%c",b->move_hist[0][0][0]/8+'a',b->move_hist[0][0][0]%8+'1',b->move_hist[0][0][1]/8+'a',b->move_hist[0][0][1]%8+'1');// always
			for(unsigned int j=1;b->move_hist[0][j-1][0]!=b->move_hist[0][j-1][1] && b->move_hist[0][j][0]+b->move_hist[0][j][1]>0;++j)// display first invalid, unless it is 00.
				fprintf(f_log,",%c%c%c%c",b->move_hist[0][j][0]/8+'a',b->move_hist[0][j][0]%8+'1',b->move_hist[0][j][1]/8+'a',b->move_hist[0][j][1]%8+'1');
			fprintf(f_log,"\n");
		}
		#endif
		// update log file if it took too long (10+sec)
		#if ALLOW_LOG
		if( t2-time_start>10000 ){
			fclose(f_log);f_log=fopen(LOG_FILE1,"a");
		}
		#endif
		if( b->em_break )
			break;// exit on time out
		last_score=score;//save
	}
	#if ALLOW_LOG
	if( f_log ){fclose(f_log);f_log=NULL;}// close and reset
	#endif
	if( score==MIN_SCORE )
		score=last_score;//restore
	*tt=t2-time_start; // time
	return(score);
}

static SRWLOCK L3; // used for STS_threads only
static unsigned int STS_i,STS_i2,STS_dd,STS_j,STS_k;
static char *STS_data;
static FILE *STS_f2;
static unsigned int STS_elapsed_time;
#define STSthreads 10
DWORD WINAPI select_STS_position(PVOID p){
	board b_l;
	unsigned int tt,l,k;
	int score;
	unsigned char mm[4];// move
	while(STS_i<STS_i2){// loop over positions
		l=0;

		AcquireSRWLockExclusive(&L3); // lock the STS*************************
		while( STS_data[STS_k]!='m' )// up to "bm"
			winboard_FEN[l++]=STS_data[STS_k++];

		k=STS_k;
		while( STS_data[STS_k]!='"' ) STS_k++; // get to the end of the move

		// find new line
		while( STS_data[STS_k]!=10 && STS_data[STS_k]!=13 && STS_data[STS_k]!=0 ) STS_k++;
		STS_k++;
		STS_i++;		
		if( l ) winboard_FEN[l-1]=0;//terminator
		init_board(100,&b_l);
		ReleaseSRWLockExclusive(&L3); // unlock it****************************

		mm[0]=mm[1]=0;
		b_l.em_break=b_l.slave_index=b_l.slave_index0=0;
		b_l.pl[0].ch_ext=0;
		timeout_complete=timeout=get_time()+1000000;
		score=solve(mm,&tt,STS_dd,&b_l);// *************************************************************************************************
		DWORD t2=get_time();
		//char sss[180];l=print_position(sss,&b_l);sss[l-1]=0;
		UINT64 nc=b_l.node_count;

		AcquireSRWLockExclusive(&L3); // lock the STS*************************
		STS_elapsed_time+=t2-(timeout-1000000);
		countI+=nc;// count
		//fprintf(STS_f2,"%s,",sss);
		//fprintf(STS_f2,"test %d,i move %d to %d,count is,%d,time is,%d,",STS_i,mm[0],mm[1],nc,t2-(timeout-1000000));
	
		// find list of all value moves. Starts with "c0"
		while( STS_data[k]!='c' || STS_data[k+1]!='0' ) k++;
		k+=4;

		unsigned int found=0;
		do{
			// get move
			decode_move(mm+2,STS_data+k,&b_l);// start from data[k], put result in mm[2]+mm[3]
			while( STS_data[k]!='=' ) k++;
			k++;

			// get score
			unsigned int l=STS_data[k++]-'0';
			if( STS_data[k]>='0' && STS_data[k]<='9' )
				l=l*10+STS_data[k++]-'0';

			if( mm[0]==mm[2] && mm[1]==mm[3] ){
				STS_j+=l;
				found=1;
				//fprintf(STS_f2,"match with score,%d\n",l);
				break;// found a match
			}

			// skip blank and ,
			while( STS_data[k]==' ' || STS_data[k]==',' ) k++;
		}while( STS_data[k]!='"' );
		//if( !found ) fprintf(STS_f2,"no match,0\n");	
		ReleaseSRWLockExclusive(&L3); // unlock it
	}
	return(1);
}

static unsigned int STS1_10_thread(unsigned int i1,unsigned int  i2,unsigned int time,unsigned int *elapsed_time){// start, finish, time: 0,1000,500. Return score. If time>1e6, then time-1e6 is depth.
	unsigned int i,k;

	InitializeSRWLock(&L3);						// init lock on STS
	clear_hash(1);								// also clear pawn hash
	STS_data=(char*)malloc(180000);// 18K*10 files
	STS_f2=fopen("c://xde//chess//STS//STS1.epd","r");// file 1
	size_t ss=fread(STS_data,1,18000,STS_f2);fclose(STS_f2);
	STS_f2=fopen("c://xde//chess//STS//STS2.epd","r");// file 2
	ss+=fread(STS_data+ss,1,18000,STS_f2);fclose(STS_f2);
	STS_f2=fopen("c://xde//chess//STS//STS3.epd","r");// file 3
	ss+=fread(STS_data+ss,1,18000,STS_f2);fclose(STS_f2);
	STS_f2=fopen("c://xde//chess//STS//STS4.epd","r");// file 4
	ss+=fread(STS_data+ss,1,18000,STS_f2);fclose(STS_f2);
	STS_f2=fopen("c://xde//chess//STS//STS5.epd","r");// file 5
	ss+=fread(STS_data+ss,1,18000,STS_f2);fclose(STS_f2);
	STS_f2=fopen("c://xde//chess//STS//STS6.epd","r");// file 6
	ss+=fread(STS_data+ss,1,18000,STS_f2);fclose(STS_f2);
	STS_f2=fopen("c://xde//chess//STS//STS7.epd","r");// file 7
	ss+=fread(STS_data+ss,1,18000,STS_f2);fclose(STS_f2);
	STS_f2=fopen("c://xde//chess//STS//STS8.epd","r");// file 8
	ss+=fread(STS_data+ss,1,18000,STS_f2);fclose(STS_f2);
	STS_f2=fopen("c://xde//chess//STS//STS9.epd","r");// file 9
	ss+=fread(STS_data+ss,1,18000,STS_f2);fclose(STS_f2);
	STS_f2=fopen("c://xde//chess//STS//STS10.epd","r");// file 10
	ss+=fread(STS_data+ss,1,18000,STS_f2);fclose(STS_f2);
			
	STS_f2=fopen("c://xde//chess//out//STS.csv","w");// log file
	for(k=i=0;i<i1;++i){// find new line - skip i1 positions
		while( STS_data[k]!=10 && STS_data[k]!=13 && STS_data[k]!=0 ) k++;
		k++;
	}
	STS_dd=time-1000000;// limited depth search only
	countI=0;//init
	STS_elapsed_time=0;
	STS_i=i1;
	STS_i2=i2;
	STS_k=k;
	
	// start calc threads
	STS_j=0;
	HANDLE s[STSthreads];
	DWORD id;
	for(k=0;k<STSthreads;++k) s[k]=CreateThread(NULL,0,select_STS_position,(PVOID)k,0,&id);
	WaitForMultipleObjects(STSthreads,s,TRUE,INFINITE);// wait for slaves to terminate
	*elapsed_time=STS_elapsed_time;
	fclose(STS_f2);
	free(STS_data);
	return(STS_j);
}

static unsigned int STS1_10(unsigned int i1,unsigned int  i2,unsigned int time,unsigned int *elapsed_time,unsigned int *ddd,unsigned int var_d){// start, finish, time: 0,1000,500. Return score. If time>1e6, then time-1e6 is depth.
	unsigned int tt,i,j,k,l;
	int score;
	unsigned char mm[4];// move

	char *data=(char*)malloc(180000);// 18K*10 files
	FILE *f2=fopen("c://xde//chess//STS//STS1.epd","r");// file 1
	size_t ss=fread(data,1,18000,f2);fclose(f2);
	f2=fopen("c://xde//chess//STS//STS2.epd","r");// file 2
	ss+=fread(data+ss,1,18000,f2);fclose(f2);
	f2=fopen("c://xde//chess//STS//STS3.epd","r");// file 3
	ss+=fread(data+ss,1,18000,f2);fclose(f2);
	f2=fopen("c://xde//chess//STS//STS4.epd","r");// file 4
	ss+=fread(data+ss,1,18000,f2);fclose(f2);
	f2=fopen("c://xde//chess//STS//STS5.epd","r");// file 5
	ss+=fread(data+ss,1,18000,f2);fclose(f2);
	f2=fopen("c://xde//chess//STS//STS6.epd","r");// file 6
	ss+=fread(data+ss,1,18000,f2);fclose(f2);
	f2=fopen("c://xde//chess//STS//STS7.epd","r");// file 7
	ss+=fread(data+ss,1,18000,f2);fclose(f2);
	f2=fopen("c://xde//chess//STS//STS8.epd","r");// file 8
	ss+=fread(data+ss,1,18000,f2);fclose(f2);
	f2=fopen("c://xde//chess//STS//STS9.epd","r");// file 9
	ss+=fread(data+ss,1,18000,f2);fclose(f2);
	f2=fopen("c://xde//chess//STS//STS10.epd","r");// file 10
	ss+=fread(data+ss,1,18000,f2);fclose(f2);
			
	f2=fopen("c://xde//chess//out//STS.csv","w");// log file
	for(k=j=i=0;i<i1;++i){// find new line
		while( data[k]!=10 && data[k]!=13 && data[k]!=0 ) k++;
		k++;
	}
	unsigned int dd=31;
	if(time>1000000) dd=time-1000000;// limit depth
	countI=0;//init
	*elapsed_time=0;
	for(i=i1;i<i2;++i){// loop over positions
		l=0;
		while( data[k]!='m' )// up to "bm"
			winboard_FEN[l++]=data[k++];
		if( l ) winboard_FEN[l-1]=0;//terminator

		init_board(100,&b_m);
		clear_hash(1); // also clear pawn hash
		mm[0]=mm[1]=0;
		timeout_complete=timeout=get_time()+time;
		if( var_d ) dd=ddd[i];
		score=solve(mm,&tt,dd,&b_m);// score
		DWORD t2=get_time();
		*elapsed_time+=t2-(timeout-time);
		countI+=b_m.node_count;// count
		char sss[180];l=print_position(sss,&b_m);sss[l-1]=0;
		fprintf(f2,"%s,",sss);
		fprintf(f2,"test %d,i move %d to %d,count is,%I64d,time is,%d,",i,mm[0],mm[1],b_m.node_count,t2-(timeout-time));
	
		// find list of all value moves. Starts with "c0"
		while( data[k]!='c' || data[k+1]!='0' ) k++;
		k+=4;

		unsigned int found=0;
		do{
			// get move
			decode_move(mm+2,data+k,&b_m);// start from data[k], put result in mm[2]+mm[3]
			while( data[k]!='=' ) k++;
			k++;

			// get score
			unsigned int l=data[k++]-'0';
			if( data[k]>='0' && data[k]<='9' )
				l=l*10+data[k++]-'0';

			if( mm[0]==mm[2] && mm[1]==mm[3] ){
				j+=l;
				found=1;
				fprintf(f2,"match with score,%d\n",l);
				break;// found a match
			}

			// skip blank and ,
			while( data[k]==' ' || data[k]==',' ) k++;
		}while( data[k]!='"' );
		if( !found ) fprintf(f2,"no match,0\n");
		
		// find new line
		while( data[k]!=10 && data[k]!=13 && data[k]!=0 ) k++;
		k++;
	}
	fclose(f2);
	free(data);
	return(j);
}

static unsigned int ECE3(unsigned int i1,unsigned int  i2,unsigned int time,unsigned int *elapsed_time){// start, finish, time: 0,1000,500. Return score. If time>1e6, then time-1e6 is depth.
	unsigned int tt,i,j,k,l;
	int score;
	unsigned char mm[4];// move

	char *data=(char*)malloc(180000);// 180K
	FILE *f2=fopen("c://xde//chess//STS//ECE3.epd","r");
	size_t ss=fread(data,1,180000,f2);
	fclose(f2);
			
	f2=fopen("c://xde//chess//out//ECE.csv","w");// log file
	fprintf(f2,"test,pieces,FEN,my_score,depth,my_move_from,my_move_to,best_move_from,best_move_to,count,time,match\n");
	k=j=0;
	for(i=0;i<i1;++i){
		// find new line
		while( data[k]!=10 && data[k]!=13 && data[k]!=0 ) k++;
		k++;
	}
	unsigned int dd=41;
	if( time>1000000 ) dd=time-1000000;// limit depth
	countI=0;//init
	*elapsed_time=0;
	for(i=i1;i<i2;++i){// loop over all 1797 positions
		l=0;
		while( data[k]!='m' )// up to "bm"
			winboard_FEN[l++]=data[k++];
		if( l ) winboard_FEN[l-1]=0;//terminator
		init_board_FEN(winboard_FEN,&b_m);// FEN from winboard
		if( popcnt64l(b_m.colorBB[0]|b_m.colorBB[1])>5 ){// skip 5 or fewer pieces - solved by EGTB.
			init_board(100,&b_m);
			clear_hash(1); // also clear pawn hash
			mm[0]=mm[1]=0;
			timeout=get_time()+time;
			score=solve(mm,&tt,dd,&b_m);// score
			DWORD t2=get_time();
			*elapsed_time+=t2-(timeout-time);
			countI+=b_m.node_count;// count
			char sss[180];l=print_position(sss,&b_m);sss[l-1]=0;
			k+=2;
			decode_move(mm+2,data+k,&b_m);// start from data[k], put result in mm[2]+mm[3]
			fprintf(f2,"%d,%I64d,%s,%.2f,%d,",i,popcnt64l(b_m.colorBB[0]|b_m.colorBB[1]),sss,score/100.0f,depth0);
			fprintf(f2,"%d,%d,%d,%d,%I64d,%d,",mm[0],mm[1],mm[2],mm[3],b_m.node_count,t2-(timeout-time));		
			if( mm[0]==mm[2] && mm[1]==mm[3] ){
				j+=1; // correct move - count as a win
				fprintf(f2,"1\n");
			}else
				fprintf(f2,"0\n");
		}
		
		// find new line
		while( data[k]!=10 && data[k]!=13 && data[k]!=0 ) k++;
		k++;
	}
	fclose(f2);
	free(data);
	return(j);
}

static unsigned int WAC(unsigned int i1,unsigned int  i2,unsigned int time,unsigned int *elapsed_time){// start, finish, time: 0,1000,500. Return score. If time>1e6, then time-1e6 is depth.
	unsigned int tt,i,j,k,l;
	int score;
	unsigned char mm[4];// move

	char *data=(char*)malloc(180000);// 180K
	FILE *f2=fopen("c://xde//chess//STS//WAC.epd","r");
	size_t ss=fread(data,1,180000,f2);
	fclose(f2);
			
	f2=fopen("c://xde//chess//out//WAC.csv","w");// log file
	fprintf(f2,"test,pieces,FEN,my_score,depth,my_move_from,my_move_to,best_move_from,best_move_to,count,time,match\n");
	k=j=0;
	for(i=0;i<i1;++i){
		// find new line
		while( data[k]!=10 && data[k]!=13 && data[k]!=0 ) k++;
		k++;
	}
	unsigned int dd=41;
	if( time>1000000 ) dd=time-1000000;// limit depth
	countI=0;//init
	*elapsed_time=0;
	for(i=i1;i<i2;++i){// loop over all positions
		l=0;
		while( data[k]!='m' )// up to "bm"
			winboard_FEN[l++]=data[k++];
		if( l ) winboard_FEN[l-1]=0;//terminator
		init_board(100,&b_m);
		clear_hash(1); // also clear pawn hash
		mm[0]=mm[1]=0;
		timeout=get_time()+time;
		score=solve(mm,&tt,dd,&b_m);// score
		DWORD t2=get_time();
		*elapsed_time+=t2-(timeout-time);
		countI+=b_m.node_count;// count
		char sss[180];l=print_position(sss,&b_m);sss[l-1]=0;
		k+=2; // after "bm "
		do{
			decode_move(mm+2,data+k,&b_m);// start from data[k], put result in mm[2]+mm[3]

			// find next move
			while( data[k]!=' ' && data[k]!=';' ) k++;
			if(data[k]==' ') k++; // skip blank
		}while(data[k]!=';' && !(mm[0]==mm[2] && mm[1]==mm[3]));// loop over best moves until they are over or a match
		fprintf(f2,"%d,%I64d,%s,%.2f,%d,",i,popcnt64l(b_m.colorBB[0]|b_m.colorBB[1]),sss,score/100.0f,depth0);
		fprintf(f2,"%d,%d,%d,%d,%I64d,%d,",mm[0],mm[1],mm[2],mm[3],b_m.node_count,t2-(timeout-time));		
		if( mm[0]==mm[2] && mm[1]==mm[3] ){
			j+=1; // correct move - count as a win
			fprintf(f2,"1\n");
		}else
			fprintf(f2,"0\n");
		
		// find new line
		while( data[k]!=10 && data[k]!=13 && data[k]!=0 ) k++;
		k++;
	}
	fclose(f2);free(data);return(j);
}

int param1,param2;
int probe_wdl(board*,int*);
int probe_dtz(board *,int*);
void clear_perft_TT(void);
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam){
	int wmId, wmEvent;
	switch (message){
	case WM_MOUSEMOVE:break;
	case WM_LBUTTONDOWN:break;
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		// Parse the menu selections:
		switch (wmId){
		case IDM_EXIT:
			DestroyWindow(hWnd);
			break;
		case IDM_PERFT:{
			char *FENs[]={"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 0",				// 0 initial position
						  "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -",		// 1 position 2 - early game
						  "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -",									// 2 position 3 - endgame=2 rooks+6 pawns
						  "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",		// 3 position 4 - early game
						  "rnbqkb1r/pp1p1ppp/2p5/4P3/2B5/8/PPP1NnPP/RNBQK2R w KQkq - 0 6",			// 4 position 5 - very early game
						  "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 0",// 5 position 6 - early game
						  "3k4/3p4/8/K1P4r/8/8/8/8 b - - 0 1",										// 6 www.talkchess.com/forum/viewtopic.php?t=47318 p1 avoid illegal ep
						  "8/8/8/8/k1p4R/8/3P4/3K4 w - - 0 1",										// 7 www.talkchess.com/forum/viewtopic.php?t=47318 p1a
						  "8/8/4k3/8/2p5/8/B2P2K1/8 w - - 0 1",										// 8 www.talkchess.com/forum/viewtopic.php?t=47318 p2 avoid illegal ep #2
						  "8/b2p2k1/8/2P5/8/4K3/8/8 b - - 0 1",										// 9 www.talkchess.com/forum/viewtopic.php?t=47318 p2a
						  "8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1",									// 10 www.talkchess.com/forum/viewtopic.php?t=47318 p3 en passant capture checks opponent
						  "8/5k2/8/2Pp4/2B5/1K6/8/8 w - d6 0 1",									// 11 www.talkchess.com/forum/viewtopic.php?t=47318 p3a
						  "5k2/8/8/8/8/8/8/4K2R w K - 0 1",											// 12 www.talkchess.com/forum/viewtopic.php?t=47318 p4 short castling gives check
						  "4k2r/8/8/8/8/8/8/5K2 b k - 0 1",											// 13 www.talkchess.com/forum/viewtopic.php?t=47318 p4a
						  "3k4/8/8/8/8/8/8/R3K3 w Q - 0 1",											// 14 www.talkchess.com/forum/viewtopic.php?t=47318 p5 long castling gives check
						  "r3k3/8/8/8/8/8/8/3K4 b q - 0 1",											// 15 www.talkchess.com/forum/viewtopic.php?t=47318 p5a
						  "r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1",								// 16 www.talkchess.com/forum/viewtopic.php?t=47318 p6 castling (including losing cr due to rook capture)
						  "r3k2r/7b/8/8/8/8/1B4BQ/R3K2R b KQkq - 0 1",								// 17 www.talkchess.com/forum/viewtopic.php?t=47318 p6a
						  "r3k2r/8/3Q4/8/8/5q2/8/R3K2R b KQkq - 0 1",								// 18 www.talkchess.com/forum/viewtopic.php?t=47318 p7 castling prevented
						  "r3k2r/8/5Q2/8/8/3q4/8/R3K2R w KQkq - 0 1",								// 19 www.talkchess.com/forum/viewtopic.php?t=47318 p7a
						  "2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1",										// 18 www.talkchess.com/forum/viewtopic.php?t=47318 p8 promote out of check
						  "3K4/8/8/8/8/8/4p3/2k2R2 b - - 0 1",										// 21 www.talkchess.com/forum/viewtopic.php?t=47318 p8a
						  "8/8/1P2K3/8/2n5/1q6/8/5k2 b - - 0 1",									// 22 www.talkchess.com/forum/viewtopic.php?t=47318 p9 discovered check
						  "5K2/8/1Q6/2N5/8/1p2k3/8/8 w - - 0 1",									// 23 www.talkchess.com/forum/viewtopic.php?t=47318 p9a
						  "4k3/1P6/8/8/8/8/K7/8 w - - 0 1",											// 24 www.talkchess.com/forum/viewtopic.php?t=47318 p10 promote to give check
						  "8/k7/8/8/8/8/1p6/4K3 b - - 0 1",											// 25 www.talkchess.com/forum/viewtopic.php?t=47318 p10a
						  "8/P1k5/K7/8/8/8/8/8 w - - 0 1",											// 26 www.talkchess.com/forum/viewtopic.php?t=47318 p11 underpromote to check
						  "8/8/8/8/8/k7/p1K5/8 b - - 0 1",											// 27 www.talkchess.com/forum/viewtopic.php?t=47318 p11a
						  "K1k5/8/P7/8/8/8/8/8 w - - 0 1",											// 28 www.talkchess.com/forum/viewtopic.php?t=47318 p12 self stalemate
						  "8/8/8/8/8/p7/8/k1K5 b - - 0 1",											// 29 www.talkchess.com/forum/viewtopic.php?t=47318 p12a
						  "8/k1P5/8/1K6/8/8/8/8 w - - 0 1",											// 30 www.talkchess.com/forum/viewtopic.php?t=47318 p13 stalemate/checkmate
						  "8/8/8/8/1k6/8/K1p5/8 b - - 0 1",											// 31 www.talkchess.com/forum/viewtopic.php?t=47318 p13a
						  "8/8/2k5/5q2/5n2/8/5K2/8 b - - 0 1",										// 32 www.talkchess.com/forum/viewtopic.php?t=47318 p14 double check
						  "8/5k2/8/5N2/5Q2/2K5/8/8 w - - 0 1"										// 33 www.talkchess.com/forum/viewtopic.php?t=47318 p14a
			};
			UINT64 cc0[][9]={{	 18,400, 8902, 197281, 4865609,  119060324, 3195901860,  84998978956,0}, // 0: 8
								{48,1839,97862,4085603,193690690,0,         0,           0,          0}, // 1: 5
								{14,191, 2812, 43238,  674624,   11030083,  178633661,   0,          0}, // 2: 7
								{6, 264, 9467,422333,  15833292, 706045033, 0,           0,          0}, // 3: 6
								{42,1352,53392,0,      0,        0,         0,           0,          0}, // 4: 3
								{46,1879,89890,3894594,164075551,6923051137,287188994746,0,          0}, // 5: 7
								{18,92,  1670, 10138,  185429,   1134888,   0,           0,          0}, // 6
								{18,92,  1670, 10138,  185429,   1134888,   0,           0,          0}, // 7
								{13,102, 1266, 10276,  135655,   1015133,   0,           0,          0}, // 8
								{13,102, 1266, 10276,  135655,   1015133,   0,           0,          0}, // 9
								{15,126, 1928, 13931,  186379,   1440467,   0,           0,          0}, // 10
								{15,126, 1928, 13931,  186379,   1440467,   0,           0,          0}, // 11
								{15,66,  1198, 6399,   118330,   661072,    0,           0,          0}, // 12
								{15,66,  1198, 6399,   118330,   661072,    0,           0,          0}, // 13
								{16,71,  1286, 7418,   141077,   803711,    0,           0,          0}, // 14
								{16,71,  1286, 7418,   141077,   803711,    0,           0,          0}, // 15
								{0, 0,   0,    1274186,0,        0,         0,           0,          0}, // 16
								{0, 0,   0,    1274186,0,        0,         0,           0,          0}, // 17
								{0, 0,   0,    1718476,0,        0,         0,           0,          0}, // 18
								{0, 0,   0,    1718476,0,        0,         0,           0,          0}, // 19
								{0, 0,   0,    0,      0,        3821001,   0,           0,          0}, // 18
								{0, 0,   0,    0,      0,        3821001,   0,           0,          0}, // 21
								{0, 0,   0,    0,      1004658,  0,         0,           0,          0}, // 22
								{0, 0,   0,    0,      1004658,  0,         0,           0,          0}, // 23
								{0, 0,   0,    0,      0,        217342,    0,           0,          0}, // 24
								{0, 0,   0,    0,      0,        217342,    0,           0,          0}, // 25
								{0, 0,   0,    0,      0,        92683,     0,           0,          0}, // 26
								{0, 0,   0,    0,      0,        92683,     0,           0,          0}, // 27
								{0, 0,   0,    0,      0,        2217,      0,           0,          0}, // 28
								{0, 0,   0,    0,      0,        2217,      0,           0,          0}, // 29
								{0, 0,   0,    0,      0,        0,         567584,      0,          0}, // 30
								{0, 0,   0,    0,      0,        0,         567584,      0,          0}, // 31
								{0, 0,   0,    23527,  0,        0,         0,           0,          0}, // 32
								{0, 0,   0,    23527,  0,        0,         0,           0,          0}  // 33
			};
			UINT64 k;
			unsigned int i,j;
			DWORD t1=get_time(),t2;
			char s[100];
			b_m.node_count=0;
			FILE *f=fopen("c://xde//chess//out//perft_log.csv","w");
			for(j=0;j<34;++j){// loop over FENs
				init_board_FEN(FENs[j],&b_m);
				fprintf(f,"starting test %d. FEN=%s\n",j,FENs[j]);
				clear_perft_TT();
				for(i=1;i<=8;++i)// loop over depth
					if( cc0[j][i-1] ){
						DWORD t1a=get_time();
						k=perft(&b_m,i);
						DWORD t2a=get_time();
						fprintf(f,"    depth,%d,count,%I64u,expected count,%I64u,time,%u\n",i,k,cc0[j][i-1],t2a-t1a);
						if( k!=cc0[j][i-1] ){
							fclose(f);
							sprintf(s,"Perft: problem! Test %d, depth %d, count %I64u vs %I64u",j,i,k,cc0[j][i-1]);
							MessageBox( hWnd, TEXT(s),TEXT(s), MB_ICONERROR | MB_OK );
							exit(6);
						}
					}
			}
			fclose(f);
			t2=get_time();
			sprintf(s,"Perft: all %d tests match! Time:%i ms. NC=%I64u",j,t2-t1,b_m.node_count);
			MessageBox( hWnd, TEXT(s),TEXT(s), MB_ICONERROR | MB_OK );
			exit(0);}
		case IDM_TEST1:{
			timeout=get_time()+36000000;// now+10 hours
			unsigned char mm[2];// move
			unsigned int tt;	// time
			unsigned int d=5;//15;	// depth was 15
			init_board(1,&b_m);
			clear_hash(1);
			int score=solve(mm,&tt,d,&b_m);// score
			
			// display it.
			char s[180],s2[180];
			sprintf(s,"Test 1. Time:%i ms. Count:%I64u",tt,b_m.node_count);
			sprintf(s2,"Score:%.2f. Depth:%i/%i. Move:%c%c/%c%c",score/100.,d,b_m.max_ply,mm[0]/8+'A',mm[0]%8+'1',mm[1]/8+'A',mm[1]%8+'1');
			MessageBox( hWnd, TEXT(s),TEXT(s2), MB_ICONERROR | MB_OK );
			break;}
		case IDM_TEST2:{
			timeout=get_time()+36000000;// now+10 hours
			unsigned char mm[2];// move
			unsigned int tt;	// time
			unsigned int d=15;	// depth was 15
			init_board(2,&b_m);
			clear_hash(1);
			int score=solve(mm,&tt,d,&b_m);// score
	
			// display it.
			char s[180],s2[180];
			sprintf(s,"Test 2. Time:%i ms. Count:%I64u",tt,b_m.node_count);
			sprintf(s2,"Score:%.2f. Depth:%i/%i. Move:%c%c/%c%c",score/100.,d,b_m.max_ply,mm[0]/8+'A',mm[0]%8+'1',mm[1]/8+'A',mm[1]%8+'1');
			MessageBox( hWnd, TEXT(s),TEXT(s2), MB_ICONERROR | MB_OK );
			break;}
		case IDM_TEST3:{
			timeout=get_time()+36000000;// now+10 hours
			unsigned char mm[2];// move
			unsigned int tt;	// time
			unsigned int d=21;	// depth was 21
			init_board(3,&b_m);
			clear_hash(1);
			int score=solve(mm,&tt,d,&b_m);// score
	
			// display it.
			char s[180],s2[180];
			sprintf(s,"Test 3. Time:%i ms. Count:%I64u",tt,b_m.node_count);
			sprintf(s2,"Score:%.2f. Depth:%i/%i. Move:%c%c/%c%c",score/100.,d,b_m.max_ply,mm[0]/8+'A',mm[0]%8+'1',mm[1]/8+'A',mm[1]%8+'1');
			MessageBox( hWnd, TEXT(s),TEXT(s2), MB_ICONERROR | MB_OK );
			break;}
		case IDM_TEST13:{// run tests 1-3 together
			timeout=get_time()+36000000;// now+10 hours
			UINT64 nct=0;
			unsigned int tttot=0,tt,d;
			unsigned char mm[2];

			d=12;init_board(2,&b_m);// warm-up - fast test 2
			clear_hash(1);solve(mm,&tt,d,&b_m);

			d=15;init_board(1,&b_m);// test 1
			clear_hash(1);solve(mm,&tt,d,&b_m);
			tttot+=tt;nct+=b_m.node_count;

			d=15;init_board(2,&b_m);// test 2
			clear_hash(1);solve(mm,&tt,d,&b_m);
			tttot+=tt;nct+=b_m.node_count;

			d=21;init_board(3,&b_m);// test 3
			clear_hash(1);solve(mm,&tt,d,&b_m);
			tttot+=tt;nct+=b_m.node_count;

			
			// display it.
			char s[180];
			sprintf(s,"Tests 1-3. Time:%i ms. Count:%I64u",tttot,nct);
			MessageBox( hWnd, TEXT(s),TEXT(s), MB_ICONERROR | MB_OK );
			break;}
		case IDM_TEST4:{
			//read_games2();// test***********************************************
			timeout=get_time()+36000000;// now+10 hours
			unsigned char mm[4];// move
			unsigned int tt;	// time
			unsigned int d=52;	// depth was 52 for "fine 70"
			int score;
			init_board(4,&b_m);

			// increash hash size when few positions are searched
			EGTBProbeLimit=6;
			HBITS=28; // 2Gb hash
			init_hash();

			//init_board_FEN("qn6/qn4k1/pp2R3/4R3/8/8/8/K7 w - - 0 1",&b_m);d=28;// perpetual draw - even even easier
			//init_board_FEN("qn6/qn6/pp4k1/4R3/4R3/8/8/K7 w - - 0 1",&b_m);d=30;// perpetual draw - even easier
			//init_board_FEN("qn6/qn6/pp6/6k1/4R3/4R3/8/K7 w - - 0 1",&b_m);d=34;// perpetual draw - easier
			//init_board_FEN("qn6/qn6/pp6/6k1/4R3/8/4R3/K7 w - - 0 1",&b_m);d=36;// perpetual draw
			//init_board_FEN("3k4/5Q2/8/3p4/1p1P4/1P6/P7/1K6 w - - 0 0",&b_m);EGTBProbeLimit=6;
			//init_board_FEN("4k3/8/pppppppp/8/8/PPPPPPPP/8/4K3 w - - 0 0",&b_m);d=18;// pawns only

			clear_hash(1);
			score=solve(mm,&tt,d,&b_m);	// solve
	
			// display it. Then make it.
			char s[180],s2[180];
			sprintf(s,"Test 4. Time:%i ms. Count:%I64u",tt,b_m.node_count);
			sprintf(s2,"Score:%.2f. Depth:%i/%i. Move:%c%c/%c%c",score/100.,d,b_m.max_ply,mm[0]/8+'A',mm[0]%8+'1',mm[1]/8+'A',mm[1]%8+'1');
			MessageBox( hWnd, TEXT(s),TEXT(s2), MB_ICONERROR | MB_OK );
			exit(0);}
		case IDM_TEST_SUITE:{// run STS/ECE3/WAC test suite
			unsigned int var_d=0,dd[1800],dd1[30]={19,19,22,19,20,21,21,19,20,20,20,20,21,19,20,20,19,22,23,19,19,21,20,20,21,20,22,21,21,20};

			//unsigned int t2,j,i1=0,i2=1000,time=1000011;// STS: 1000 positions, depth 11;	
			//unsigned int t2,j,i1=0,i2=1797,time=1000012; // ECE3: 1797 positions, depth 12
			//unsigned int t2,j,i1=0,i2=300,time=1000015; // WAC: 300 positions, depth 15
			var_d=1;unsigned int t2,j,i1=0,i2=30,time=1000023; // 30 positions to depth XX, for testing parallel search.
			//var_d=1;unsigned int t2,j,i1=0,i2=1,time=1000023; // 30 positions to depth XX, for testing parallel search.

			// increash hash size when few positions are searched
			if( i2<100 ){
				HBITS=27; // 1Gb hash
				init_hash();
			}


			// loop over some parameters and log the results
			FILE *f3=fopen("c://xde//chess//out//STS_loop.csv","w");
			fprintf(f3,"param1,param2,count,NPS,time,score\n");
			fclose(f3);
			if( time>=1000000 ) timeout=1000000000+get_time(); // way in the future
			for(int k=0;k<1;k++){
				// 1. assign param(s).
				//param1=0-(k/10);param2=0;
				f3=fopen("c://xde//chess//out//STS_loop.csv","a");
				fprintf(f3,"%d,%d,",param1,param2);// print input
				fclose(f3);
				
				// 2. call STS
				if( var_d ) j=STS1_10(i1,i2,time,&t2,dd1,1);// start, finish, time in, time out
				else j=STS1_10(i1,i2,time,&t2,dd,0);// start, finish, time in, time out
				//j=ECE3(i1,i2,time,&t2);// start, finish, time.
				//j=WAC(i1,i2,time,&t2);// start, finish, time.	
				
				// 3. log results
				f3=fopen("c://xde//chess//out//STS_loop.csv","a");
				fprintf(f3,"%I64u,%I64u,%.2f,%d\n",countI,countI/t2,t2/1000.,j);
				fclose(f3);
			}

			/*f3=fopen("c://xde//chess//out//ccc.csv","w");
			for(unsigned int i=0;i<2500;++i)
				if( ccc[i] )
					fprintf(f3,"%d,%I64u\n",i,ccc[i]);
			fclose(f3);*/

			// display results.
			char s[100],s2[100];
			sprintf(s,"Count:%I64u. NPS:%I64u.",countI,countI/t2);
			sprintf(s2,"Time:%.2f sec. Score:%d out of %d",t2/1000.,j,(i2-i1)*10);
			//sprintf(s2,"%d/%d",param1,param2);
			MessageBox( hWnd, TEXT(s2),TEXT(s), MB_ICONERROR | MB_OK );
			exit(0);}
		case IDM_TRAIN:{// training set
			train();
			break;}
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}