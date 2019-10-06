// transposition table implementation
#include "chess.h"

hash* h; // hash table
UINT64 *mem;
UINT64 hash_index_mask;
unsigned int TTage;// TT aging counter, 0 to 3.
unsigned int HBITS=24; // option**** 16 Mil entries * 8 bytes= 128 Mb. 24 bits. Main hash is 22 bits of 4-way blocks.
//unsigned int HBITS=27; // option**** 128 Mil entries * 8 bytes= 1 Gb. 27 bits. Main hash is 25 bits of 4-way blocks.
static unsigned int HSIZE;

void int_m2(void);
extern UINT64 *eh; // eval hash pointer
extern short int *mh; // material table pointer
void clear_hash(unsigned int i){//0: TT only. >0: Pawn hash also.
	memset(h,0,sizeof(hash)*HSIZE);// TT hash
	// Eval hash is always reset in solve_prep function.
	if(i) memset(ph,0,8*PHSIZE);// pawn hash
}

void init_hash(void){
	static UINT64 *meml=NULL;
	static unsigned int HBL=0,virt=0;
	UINT64 z;
	unsigned int i,j,k;
	
	// init zorbist keys
	if( !HBL ){// only on first call.
		srand(1678579445);
		memset(zorb,0,sizeof(zorb));
		for(j=0;j<2;++j) // player
			for(k=0;k<6;++k) // piece
				for(i=0;i<64;++i){ // square
					z=rand();
					z=(z<<15)^rand();//2
					z=(z<<15)^rand();//3
					z=(z<<15)^rand();//4
					z=(z<<15)^rand();//5
					zorb[k][j][i]=z; // 15 bits each,x5= 75 bit total
				}

		// set pawn zorb to queen for promotion, so that i don't have to change it. But wil have to change pawn hash!
		for(i=0;i<64;i+=8){// square (in steps of 8)
			zorb[0][0][i+7]=zorb[4][0][i+7];
			zorb[0][1][i]=zorb[4][1][i];
		}
	}

	// alloc memory
	if( HBITS!=HBL ){
		if( h ){// release already allocated memory
			if( virt==0 ){
				free(meml);free(h);free(ph);
				free(eh);free(mh);
			}
			#if USE_VIRT_MEM
			else
				VirtualFree(h,0,MEM_RELEASE);
			#endif
			ph=NULL;h=NULL;
		}
		HSIZE=UINT64(1)<<HBITS;
		hash_index_mask=UINT64(0x0fffffffffffffff)>>(64-4+2-HBITS);
		#if USE_VIRT_MEM
		HANDLE token_handle;
		TOKEN_PRIVILEGES tp;
		SIZE_T MinimumPSize = GetLargePageMinimum();
		if( MinimumPSize ){
			#if ALLOW_LOG
			fprintf(f_log,"Large Pages supported\n");
			fprintf(f_log,"Minimum Large Pages size = %i\n", (unsigned int)MinimumPSize);
			#endif

			OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token_handle);
			LookupPrivilegeValue(NULL, TEXT("SeLockMemoryPrivilege"), &tp.Privileges[0].Luid);
			tp.PrivilegeCount = 1;
			tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
			AdjustTokenPrivileges(token_handle, false, &tp, 0, NULL, 0);
			CloseHandle(token_handle);

			SIZE_T size=sizeof(hash)*HSIZE+sizeof(UINT64)*PHSIZE+sizeof(UINT64)*EHSIZE+sizeof(short)*256*1024+2*1024*1024;// raw size(TT, pawns, eval, material)+2Mb
			size=(((size-1)/MinimumPSize)+1)*MinimumPSize; // round up to next large page
			#if ALLOW_LOG
			fprintf(f_log,"Allocating %u Mb\n",(unsigned int)(size/1024/1024));
			#endif
			virt=1; // mark it as virtually allocated
			h=(hash*)VirtualAlloc(NULL, size, MEM_LARGE_PAGES | MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);// use virtual alloc;
			ph=(UINT64*)(h+HSIZE);
			eh=(UINT64*)(ph+PHSIZE);
			mh=(short*)(eh+EHSIZE);
			meml=mem=(UINT64*)(mh+256*1024);// free 2 Mb of memory. Put large arrays here.
			if( h==NULL ){// cannot allocate. Why?
				LPVOID lpMsgBuf;
				DWORD dw = GetLastError(); 
				FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,NULL,dw,MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),(LPTSTR) &lpMsgBuf,0, NULL );
				#if ALLOW_LOG
				fprintf(f_log,"Memory alloc failed with error %d: %s\n",dw,(char*)lpMsgBuf);
				#endif
				LocalFree(lpMsgBuf);
			}else{
				#if ALLOW_LOG
				fprintf(f_log,"Large pages were set successfully\n");
				#endif
			}
		}
		#endif
	
		if( h==NULL ){// cannot allocate. Use regular malloc
			h=(hash*)malloc(sizeof(hash)*HSIZE);// raw size
			ph=(UINT64*)malloc(8*PHSIZE);// raw size
			eh=(UINT64*)malloc(8*EHSIZE);// raw size
			mh=(short int*)malloc(512*1024);// raw size
			meml=mem=(UINT64*)malloc(2*1024*1024);// raw size
			virt=0;
			#if ENGINE
			if( h==NULL || mem==NULL ) pass_message_to_GUI("info string Memory alloc failed\n");
			#endif
			#if ALLOW_LOG
			if( h!=NULL && mem!=NULL ) fprintf(f_log,"Small pages were set successfully\n");
			#endif
		}
		clear_hash(1);
		if( HBL){
			int_m2();		// init magic bitboards, on second call only
			init_material();// init material
		}
		HBL=HBITS; //save
	}
}

unsigned int hashfull(void){// cound hash entries in the first 1000 spots, for current age only
	unsigned int i,s;
	for(i=s=0;i<1000;++i)
		if( h[i].age==TTage )
			s++;
	return(s);
}

_declspec(noinline) unsigned int lookup_hash(unsigned int depth,board *b,hash_data *hd,unsigned int ply){// returns indicator only.
	volatile hash *h1=&h[get_hash_index];// always start at the beginiing of block of 4 - 4-way set-associative structure.
	hash h_read;
	unsigned int i,lock4a=(((unsigned int *)&b->hash_key)[1])<<6;// shift out top 6 bits - they are depth.

	// check if it matches all pieces and player
	for(i=0;i<4;++i,++h1){
		h_read=((hash*)h1)[0]; // atomic read
		if( ((((unsigned int*)&h_read.lock2)[0])<<6)==lock4a ){// match. Shift out top 6 bits.
			hd->depth=h_read.depth;								// return depth
			hd->bound_type=h_read.type;
			hd->tt_score=h_read.score;
			((unsigned short int*)&hd->move[0])[0]=((unsigned short int*)&h_read.score)[1]&0x3f3f;// move
			hd->alp=MIN_SCORE;hd->be=MAX_SCORE;					// init score
			h1->age=TTage;										// not stale anymore

			if( depth<=h_read.depth ){			// only use bounds for search of the same OR GREATER depth.
				int s1=h_read.score;			// score to be returned
				if( abs(s1)>=5000 ){	    	// mate - adjust by ply
					if( s1<0 )// make score smaller
						s1+=ply;
					else
						s1-=ply;
				}
				if( h_read.type==0 )			//exact score
					hd->be=hd->alp=s1;			// assign both bounds
				else if( h_read.type==1 )		//lower bound
					hd->alp=s1;					// assign lower bound
				else							// upper bound
					hd->be=s1;					// assign upper bound
			}
			return(1);// return match
		}
	}
	return(0); // not a match
}

_declspec(noinline) void add_hash(int alp,int be,int score,unsigned char *move,unsigned int depth,board *b,unsigned int ply){
	static const unsigned int TTage_convert[4][4]={{0,3,2,1},{1,0,3,2},{2,1,0,3},{3,2,1,0}};// converts current (first arg) and old (last arg) TT age into age difference
	volatile hash *h2,*h1=&h[get_hash_index];// always start at the beginiing of block of 4 - 4-way set-associative structure.
	hash h_read;
	unsigned int i,lock4a=(((unsigned int *)&b->hash_key)[1])<<6;// shift out top 6 bits - they are depth.
	int s_replace=1000,s1=score; // this is score that i store in TT

	if( abs(s1)>=5000 ){// mate - adjust by ply
		if( s1>0 )// make score larger - undo the effect of reducing loss by ply.
			s1+=ply;
		else
			s1-=ply;
	}

	// check if it matches all pieces and player
	for(i=0;i<4;++i,++h1){
		h_read=((hash*)h1)[0]; // atomic read
		if( ((((unsigned int*)&h_read.lock2)[0])<<6)==lock4a ){// match. Shift out top 6 bits.
			//if( depth>=h_read.depth ){// replace old values if new search is of same or bigger depth. Here new depth is higher 61%, same 36%, lower 4%.
			// always replacing is +5 ELO. 6/2017.
				h_read.score=s1;		//score;
				if( ((unsigned short int*)move)[0] )// only replace the move if it is valid
					((unsigned short int*)&h_read.score)[1]=((unsigned short int*)move)[0];// move.
				h_read.age=TTage;		// not stale anymore
				if(score<be){
					if(score>alp)
						h_read.type=0;
					else
						h_read.type=2;	// score<=alp - upper bound. Score is in the range -inf, score.
				}else
					h_read.type=1;		// score>=be - lower bound. Score is in the range score, +inf.
				h_read.depth=depth;
				*(hash*)h1=h_read;// atomic write
			//}else// current depth is larger - retain the values.
			//	h1->age=TTage; // not stale anymore
			return; // match found and updated - return
		}else{// no match. Consider it for replacement
			int s=int(h_read.depth)-(int(TTage_convert[TTage][h_read.age])<<8)+(h_read.type==0?2:0);  // order: age(decr), then depth(incr), then node type. Here 2 for PV seems best
			//int s=int(h_read.depth)-(int(TTage_convert[TTage][h_read.age])<<3)+(h_read.type==0?1:0);  // order: age(decr), then depth(incr), then node type: a wash, do not use. 6/2017.
			if( s<s_replace ){// lower depth (or stale, or both) found, record it as best. Here i is already a tie-breaker.
				s_replace=s;
				h2=h1;
			}
		}
	}
	
	// Always replace - should be better in long games.
	(((unsigned int*)&h_read.lock2)[0])=lock4a>>6;
	h_read.score=s1;
	((unsigned short int*)&h_read.score)[1]=((unsigned short int*)move)[0];// move
	h_read.age=TTage;	// not stale anymore
	if(score<be){
		if(score>alp)
			h_read.type=0;
		else
			h_read.type=2;
	}else
		h_read.type=1;
	h_read.depth=depth;
	*(hash*)h2=h_read;// atomic write
 }

void delete_hash_entry(board *b){// find this position and delete it from TT
	volatile hash *h1=&h[get_hash_index];// always start at the beginiing of block of 4 - 4-way set-associative structure.
	for(unsigned int i=0;i<4;++i,++h1) memset((void*)h1,0,sizeof(hash));// delete it
 }