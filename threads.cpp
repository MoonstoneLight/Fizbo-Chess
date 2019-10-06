// multithreading logic
#include "chess.h"
#include <intrin.h>
#include <math.h>
#include "threads.h"

split_point_type *sp_all;				// all split-points
board *b_s;								// slave boards
SRWLOCK L1;								// lock on split-point objects. This lock guards SP vars mlp, c_1, slave_bits, and globals thread_running_mask, sp_all_mask. Also SP selection by slaves.
CONDITION_VARIABLE CV1;					// slaves are allowed to run
UINT64 sp_all_mask;						// mask of all split-points: 1 if occupied, 0 if free.
UINT64 sp_open_mask;					// mask of open (with moves to be searched) SPs
UINT64 thread_running_mask=1;			// init to "master is running"
static HANDLE *s_th;					// slave thread handles
int slave_count;						// count of running slaves
static int slave_terminate=0;			// set to 1 to terminate threads


#if SLOG
int f_timer2(void){// more precise timer
	static LARGE_INTEGER pf;
	LARGE_INTEGER pc;

	if( !pf.QuadPart ) QueryPerformanceFrequency(&pf);// init
	QueryPerformanceCounter(&pc);
	return(int((pc.QuadPart*1000000)/pf.QuadPart));
}
FILE *f_slog;
int t0;
int c_s;
#endif

DWORD WINAPI SlaveThreadProc(PVOID p){// slave function - new approach.
	UINT64 bb;
	unsigned int SlaveId=(unsigned int)p,ply_l,node_type_l,go_to_sleep=0;
	int alp_l,be_l,depth_l;
	DWORD bit;

	#if ALLOW_LOG
	fprintf(f_log,"Slave %d started\n",SlaveId);
	#endif

	InterlockedIncrement((unsigned int*)&slave_count);
	size_t sizeofboard=sizeof(board)-sizeof(b_s[0].move_hist)-100*sizeof(play)-sizeof(b_s[0].slave_index)-sizeof(b_s[0].slave_index0);

	AcquireSRWLockExclusive(&L1); // lock the split-point
	while(1){ // infinite loop. Here L1 is locked
		while( (
				!sp_open_mask // look at open SPs, not all of them
				|| go_to_sleep
				|| popcnt64l(thread_running_mask)>=Threads
			)
			&& !slave_terminate
		){// there is work to be done and no termination signal
			go_to_sleep=0;
			SleepConditionVariableSRW(&CV1,&L1,INFINITE,0); // wait for CV1 and release the lock. When i wake up i have the lock.
		}
		if( slave_terminate ){// termination signal - exit thread
			ReleaseSRWLockExclusive(&L1);
			WakeAllConditionVariable(&CV1); // just in case
			return(0); // terminate thread
		}

		// select a split-point to work on.
		bb=sp_open_mask;// look at open SPs, not all of them
		split_point_type *spl;
		int sp_index=0,sp_value=-1000000000;
		while( bb ){// loop over all split-points
			GET_BIT(bb)
			spl=sp_all+bit;
			if( spl->c_0 ){// only if there are moves remaining in this SP
				int spvl=0;
				spvl-=6400*spl->c_1;						// Don't put too many threads under the same split point. Balance them as equally between split points as you can. 64.
				spvl-=200*spl->b.sp_level;					// Don't create splitpoints under splitpoints unless you really have to.

				spvl+=spl->depth*16;						// depth: higher is better. 16.
				spvl-=spl->ply*2;							// ply: lower is better. 2.
				spvl-=(spl->in_check>0?80:0);				// avoid checks

				// add credit for more moves solved
				if( spl->node_type==2 && spl->mlp->next_move>1+spl->c_1 && spl->mlp->next_move<5+spl->c_1) // at least 1 move have been completed
					spvl+=90000;

				if( spvl>sp_value ){// record best SP
					sp_value=spvl;
					sp_index=bit;
				}
			}
		}
		if( sp_value==-1000000000 || sp_all[sp_index].c_0==0 ){// no SP found
			go_to_sleep=1;
			continue;		// go back to top and keep L1 locked
		}
		spl=sp_all+sp_index;	// SP selected

		// get data from split-point, that is now locked to this thread.
		spl->c_1++;										// incr count of slaves on this split-point
		spl->slave_bits|=(UINT64(1)<<SlaveId);			// mark it as being worked on by this slave
		thread_running_mask|=(UINT64(1)<<SlaveId);		// mark this slave as running	

		#if SLOG
		int tl;
		if( depth0>=SLOG && (SLOG_MASK&SLOG_START) ){
			tl=f_timer2();
			fprintf(f_slog,"%d,%d,%d,%d,%d,",spl->id,tl-t0,int(popcnt64l(thread_running_mask)),int(popcnt64l(sp_all_mask)),int(popcnt64l(sp_open_mask)));
			fprintf(f_slog,"starting,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
				sp_index,SlaveId,spl->c_1,spl->depth,depth0,spl->ply,spl->node_type,spl->mlp->next_move,spl->b.sp_level);
			c_s++;
		}
		#endif
		
		memcpy(&b_s[SlaveId-1],&(spl->b),sizeofboard); // do not copy move history, or some other vars.
		b_s[SlaveId-1].slave_index0=b_s[SlaveId-1].slave_index=SlaveId; // need this - it is zeroed out in search and not restored.
		b_s[SlaveId-1].spp=(void*)spl;			// assign split-point to slave. Do this after "b" is copied!
		assert(b_s[SlaveId-1].slave_index0<100);
		alp_l=spl->bm.alp;
		be_l=spl->be;
		depth_l=spl->depth;
		ply_l=spl->ply;
		node_type_l=spl->node_type;
		b_s[SlaveId-1].node_count=0;			// reset node count for this thread
		b_s[SlaveId-1].max_ply=b_m.max_ply;
		b_s[SlaveId-1].em_break=0;				// reset emergency break
		b_s[SlaveId-1].sps_created_num=0;
		ReleaseSRWLockExclusive(&L1);			// release the lock*************************************************************

		go_to_sleep=Msearch(&b_s[SlaveId-1],depth_l,ply_l,alp_l,be_l,node_type_l); // call the solver***********************************************************************
		thread_running_mask&=~(UINT64(1)<<SlaveId); // mark this slave as not running. Again. Jic.
		if( b_s[SlaveId-1].em_break==1 ) go_to_sleep=1;
		// here L1 is already locked

		#if SLOG
		if( depth0>=SLOG && (SLOG_MASK&SLOG_END) ){
			int tl1=f_timer2();
			fprintf(f_slog,"%d,%d,%d,%d,%d,",spl->id,tl1-t0,int(popcnt64l(thread_running_mask)),int(popcnt64l(sp_all_mask)),int(popcnt64l(sp_open_mask)));
			fprintf(f_slog,"ending,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
				sp_index,SlaveId,spl->c_1,spl->depth,depth0,spl->ply,spl->node_type,spl->mlp->next_move,spl->b.sp_level,tl1-tl);
			c_s++;
		}
		#endif

	}// end of infinite loop
	return(0);
}

// init all thread objects
void init_threads(unsigned int TC){// thread count - slaves only.
	unsigned int i,EXTRA_THREADS=max(5,TC/2);
	DWORD id;
	
	// return if good
	if( TC+EXTRA_THREADS==slave_count || (TC==0 && slave_count==0) )
		return;

	// stop existing threads
	if( slave_count ){
		slave_terminate=1;
		WakeAllConditionVariable(&CV1);// wake the slaves
		WaitForMultipleObjects(slave_count,s_th,TRUE,INFINITE);// wait for slaves to terminate
		for(i=0;i<(unsigned int)slave_count;i++) CloseHandle(s_th[i]); // need this one
		slave_terminate=slave_count=0; // reset vars
	}

	// return if good
	if( TC==0 ) return;

	// start threads
	TC=min(64,TC+EXTRA_THREADS); // start several more threads than allowed - to cover sleeping ones.
	// allocate memory
	if( s_th!=NULL ) free(s_th);
	s_th=(HANDLE*)malloc((TC+2)*(sizeof(HANDLE)+sizeof(board))); // 4,516(outdated) if MAX_MOVE_HIST=8. For 20 threads this is 25*4,516=111 Kb.
	if( s_th==NULL ) exit(123);
	b_s=(board*)&s_th[TC+2];

	for(i=1;i<=TC;++i) s_th[i-1]=CreateThread(NULL,0,SlaveThreadProc,(PVOID)i,0,&id);
	Sleep(50);
return;
}