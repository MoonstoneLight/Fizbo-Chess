// neural network logic
#include "chess.h"
#include <intrin.h>
#if USE_AVX
#include <xmmintrin.h>
#endif
#include <math.h>
#include "threads.h"

typedef struct {
	double mgw;
	board b;
	short int score_deep;
	short int score_shallow;
	unsigned char fullmoveclock;
} board_plus;

typedef struct {
	int h;						// history count
	short int alp;				// alp
	short int eval;				// computed at load
	short int SEE;				// computed at load
	//char FEN[100];				// FEN
	unsigned char pieces[64],player;
	unsigned char LMR;			// LMR
	unsigned char node_type;	// node type 0/1/2
	unsigned char depth;		// depth
	unsigned char ply;			// ply
	unsigned char move_number;	// order of move in the list
	unsigned char from;			// move from
	unsigned char to;			// move to
	unsigned char cut;			// 0/1
} move_data; 

// global data
#define num_inputs (5+15)
#define num_n1	   64					// number of first layer neurons.
#define tot_c_num (num_inputs*num_n1+num_n1+1)
static __declspec(align(64)) float cnn2[num_inputs*num_n1+num_n1+1];	// coeffs for first layer, second layer and bias

static move_data *ts_all;
static double l_rate;
static double RR0,RR1,RR0a,RR1a;
static unsigned int cc,cca;
static unsigned int pos_count0,ii,batch_size,iter;

typedef struct {
	float out_1_nn[num_n1];					// outputs for 1st layer
	float out_last_nn;						// outputs for last layer
	unsigned int inp_nn2[48];				// index of up to 64 pieces on the board. First one is bias - always 1. Terminated by 1000. Max length=32+bias+terminator=34.
} data_nn;

inline float RLU(float s){return(max(0,s));} // rectified linear unit

#define USE_AVX2 1 // here num_n1 must be 64
void apply(__m256 *v,data_nn *d_nnp,unsigned int l){
#	if USE_AVX2
	v[0]=_mm256_add_ps(v[0],((__m256*)&cnn2[l*num_n1])[0]);v[1]=_mm256_add_ps(v[1],((__m256*)&cnn2[l*num_n1])[1]);v[2]=_mm256_add_ps(v[2],((__m256*)&cnn2[l*num_n1])[2]);v[3]=_mm256_add_ps(v[3],((__m256*)&cnn2[l*num_n1])[3]);v[4]=_mm256_add_ps(v[4],((__m256*)&cnn2[l*num_n1])[4]);v[5]=_mm256_add_ps(v[5],((__m256*)&cnn2[l*num_n1])[5]);v[6]=_mm256_add_ps(v[6],((__m256*)&cnn2[l*num_n1])[6]);v[7]=_mm256_add_ps(v[7],((__m256*)&cnn2[l*num_n1])[7]);
	#else
	for(unsigned int n=0;n<num_n1;++n) d_nnp->out_1_nn[n]+=cnn2[l*num_n1+n];
	#endif
}

float pass_forward_2_float(board *b,data_nn *d_nnp,unsigned int from,unsigned int to,unsigned int node_type,int SEE,int hist,int eval2,int move_number){// compute output of network, taking board as input
	__m256 v[8];
	float s;
	unsigned int k,l,bb=0;// no bias

	#if USE_AVX2	
	static const float v0[8]={0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f};	// zero*8
	v[0]=_mm256_load_ps(&v0[0]);v[1]=_mm256_load_ps(&v0[0]);v[2]=_mm256_load_ps(&v0[0]);v[3]=_mm256_load_ps(&v0[0]);v[4]=_mm256_load_ps(&v0[0]);v[5]=_mm256_load_ps(&v0[0]);v[6]=_mm256_load_ps(&v0[0]);v[7]=_mm256_load_ps(&v0[0]);
	#else
	unsigned int n;
	for(n=0;n<num_n1;++n) d_nnp->out_1_nn[n]=0; // init to zero
	#endif
	k=0;//no bias

	// process board
	/*UINT64 one=1,bbb=(b->colorBB[0]|b->colorBB[1])&(~(one<<from)); // all material, excluding "from"
	do{ unsigned long bit;
		GET_BIT(bbb)
		int q=b->piece[bit];		// unformatted piece
		int p=(q&7)-1;				// 0-5
		if( b->player==2 )			// turn position to "white's move"
			l=bb+p+6*(1-(q>>7))+flips[bit][1]*12;
		else
			l=bb+p+6*(q>>7)+bit*12;
		assert(l<bb+768);
		apply(v,d_nnp,l);
	}while(bbb);
	bb+=768;
	// add move items
	// 1. from, 0-63=64
	assert(from<64);
	unsigned int from1=from;
	if( b->player==2 ) from1=flips[from][1];// from white's POV
	l=bb+from1;
	bb+=64;
	d_nnp->inp_nn2[k++]=l;
	apply(v,d_nnp,l);
	// 2. to, 0-63=64
	assert(to<64);
	unsigned int to1=to;
	if( b->player==2 ) to1=flips[to][1];// from white's POV
	l=bb+to1;
	bb+=64;
	d_nnp->inp_nn2[k++]=l;
	apply(v,d_nnp,l);*/
	// 6. range of history
	int hist1;
	if( hist<-100 ) hist1=0;
	else if( hist<0 ) hist1=1;
	else if( hist==0 ) hist1=2;
	else if( hist<100 ) hist1=3;
	else hist1=4;
	l=bb+hist1; // 0-4
	bb+=5;
	d_nnp->inp_nn2[k++]=l;
	apply(v,d_nnp,l);
	

	//dl[k++]=float(d.SEE>=0?1:0);// SEE range: neg vs 0+.
	if( SEE>=0 ) {
		l=bb;
		d_nnp->inp_nn2[k++]=l;
		apply(v,d_nnp,l);
	}
	bb++;

	if( eval2>=0 ) l=bb; // 0+
	else if( eval2>=-100 ) l=bb+1; // -100 to 0
	else if( eval2>=-200 ) l=bb+2; // -200 to -100
	else l=bb+3; // <-200
	bb+=4;
	d_nnp->inp_nn2[k++]=l;
	apply(v,d_nnp,l);

	if( move_number<5 ) l=bb;// move number
	else if( move_number<12 ) l=bb+1;
	else if( move_number<29 ) l=bb+2;
	else l=bb+3;
	bb+=4;
	d_nnp->inp_nn2[k++]=l;
	apply(v,d_nnp,l);

	unsigned int mp=(b->piece[from]&7)-1; // type of moving piece, 0-5=6. No empties.
	l=bb+mp;
	d_nnp->inp_nn2[k++]=l;
	apply(v,d_nnp,l);
	bb+=6;


	d_nnp->inp_nn2[k]=1000;  // terminator
	assert(k<=48);
	assert(bb<=num_inputs);

	//apply activation to layer 1
	#if USE_AVX2
	static const __m256 v0a=_mm256_set_ps(0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f);
	v[0]=_mm256_max_ps(v[0],v0a);v[1]=_mm256_max_ps(v[1],v0a);v[2]=_mm256_max_ps(v[2],v0a);v[3]=_mm256_max_ps(v[3],v0a);v[4]=_mm256_max_ps(v[4],v0a);v[5]=_mm256_max_ps(v[5],v0a);v[6]=_mm256_max_ps(v[6],v0a);v[7]=_mm256_max_ps(v[7],v0a);
	memcpy(d_nnp->out_1_nn,v,sizeof(float)*num_n1); // copy into results
	#else
	for(n=0;n<num_n1;++n) d_nnp->out_1_nn[n]=RLU(d_nnp->out_1_nn[n]);
	#endif

	// process last layer
	#if USE_AVX2
	v[0]=_mm256_mul_ps(v[0],((__m256*)&cnn2[num_inputs*num_n1+0*8])[0]);v[1]=_mm256_mul_ps(v[1],((__m256*)&cnn2[num_inputs*num_n1+1*8])[0]);
	v[2]=_mm256_mul_ps(v[2],((__m256*)&cnn2[num_inputs*num_n1+2*8])[0]);v[3]=_mm256_mul_ps(v[3],((__m256*)&cnn2[num_inputs*num_n1+3*8])[0]);
	v[4]=_mm256_mul_ps(v[4],((__m256*)&cnn2[num_inputs*num_n1+4*8])[0]);v[5]=_mm256_mul_ps(v[5],((__m256*)&cnn2[num_inputs*num_n1+5*8])[0]);
	v[6]=_mm256_mul_ps(v[6],((__m256*)&cnn2[num_inputs*num_n1+6*8])[0]);v[7]=_mm256_mul_ps(v[7],((__m256*)&cnn2[num_inputs*num_n1+7*8])[0]);
	v[0]=_mm256_add_ps(v[0],v[1]);v[2]=_mm256_add_ps(v[2],v[3]);
	v[4]=_mm256_add_ps(v[4],v[5]);v[6]=_mm256_add_ps(v[6],v[7]);
	v[0]=_mm256_add_ps(v[0],v[2]);v[4]=_mm256_add_ps(v[4],v[6]);
	v[0]=_mm256_add_ps(v[0],v[4]);
	s=cnn2[num_inputs*num_n1+num_n1]+v[0].m256_f32[0]+v[0].m256_f32[1]+v[0].m256_f32[2]+v[0].m256_f32[3]+v[0].m256_f32[4]+v[0].m256_f32[5]+v[0].m256_f32[6]+v[0].m256_f32[7];
	#else
	s=cnn2[num_inputs*num_n1+num_n1];// bias in
	for(k=0;k<num_n1;++k) s+=d_nnp->out_1_nn[k]*cnn2[num_inputs*num_n1+k];
	#endif
	//no activation function - use "as is".
	d_nnp->out_last_nn=s;
	return(s);
}

static Spinlock l2;		// spinlock
double lrmax,lravg,chmax,chavg;
static void update_coeffs(double *ci,unsigned int cc,unsigned int type){// type: 0 - accumulate only, 1 - apply and reset
	unsigned int j;

	static double grad[tot_c_num];// current deriv
	static double m[tot_c_num];
	static double v[tot_c_num];
	static unsigned int ccb;

	double b1=0.9,b2=0.999,e=1e-8;

	l2.acquire();
	if( type==0 ){// accumulate gradient only, without dividing by cc
		for(j=0;j<tot_c_num;++j) grad[j]+=ci[j]; 
		ccb+=cc;

		// reset current portion
		memset(ci,0,sizeof(cnn2));
	}else{// apply
		chavg=chmax=lrmax=lravg=0;
		double ch;
		for(j=0;j<tot_c_num;++j){
			grad[j]/=ccb;// scale

			m[j]=m[j]*b1+grad[j]*(1-b1);
			v[j]=v[j]*b2+grad[j]*grad[j]*(1-b2);
			double m1=m[j]/(1.-pow(b1,iter+1));
			double v1=v[j]/(1.-pow(b2,iter+1));

			ch=l_rate*m1/(e+sqrt(v1));

			cnn2[j]=float(cnn2[j]+ch);// apply


			// record
			lrmax=max(lrmax,fabs(grad[j]));
			lravg+=grad[j]*grad[j];
			chmax=max(chmax,fabs(ch));
			chavg+=ch;
		}
		lravg=sqrt(lravg/tot_c_num);
		chavg/=tot_c_num;

		// reset deriv accum
		memset(grad,0,sizeof(grad));// reset
		ccb=0;
	}
	l2.release();	
}

#define excl_res 1
static double pred_mult=1;
static DWORD WINAPI train_nn(PVOID ppp){// 1 thread loops over positions.
	move_data *md;
	board bo;
	data_nn d_nn;
	double prob,RR1_l=0.,RR1a_l=0.,v0,b;
	double cl[num_inputs*num_n1+num_n1+1],af;
	unsigned int iil,j,k,l,counter=0,cc_l=0,cca_l=0,tt,pi;
	
	memset(cl,0,sizeof(cl));
	while(1){// infinite loop
		iil=InterlockedExchangeAdd((LONG*)&ii,batch_size); // this is equivalent to locked "i=ii; ii+=X;", but is a lot faster with many threads.
		if( iil>=pos_count0 ) break;	// exit
		counter=0;

		// loop over a set of positions
		for(tt=0;tt<batch_size;++tt){
			pi=iil+tt;
			if( pi>=pos_count0 ) break;	// exit

			// prepare board
			md=&ts_all[pi];
			memcpy(bo.piece,md->pieces,64);bo.player=md->player;
			set_bitboards(&bo);// set bitboards
			//init_board_FEN(md->FEN,&bo); // this also sets all bitboards
		
		
			v0=md->cut;
			prob=pred_mult*pass_forward_2_float(&bo,&d_nn,md->from,md->to,md->node_type,md->SEE,md->h,md->eval-md->alp,md->move_number);
			
			// evaluate R2
			if( l_rate<=1.1e-24 ){
				b=(prob-v0)*(prob-v0);
				// second set
				if( (pi%5)==excl_res ){
					RR1a_l+=b;
					cca_l++;
				}else{// first set
					RR1_l+=b;
					cc_l++;
				}
				continue;
			}
			// second set - drop it from training
			if( (pi%5)==excl_res )continue;

			// get derivative
			b=2.*(v0-prob);
			counter++;
			// adjust last layer coeffs
	
			// adjust last layer coeffs - deriv is always 1.
			cl[num_inputs*num_n1+num_n1]+=b;// no activation function here, deriv=1.
			for(j=0;j<num_n1;++j){
				if( fabs(d_nn.out_1_nn[j])>1e-12 ){// deriv of first layer: 0/1 for RLU
					cl[num_inputs*num_n1+j]+=b*d_nn.out_1_nn[j];// no activation function here, deriv=1.
					if( fabs(cnn2[num_inputs*num_n1+j])>1e-12 ){// deriv of first layer: 0/1 for RLU
						// adjust first layer coeffs
						k=0;
						af=cnn2[num_inputs*num_n1+j]*b;
						while( (l=d_nn.inp_nn2[k++])<1000 ) cl[l*num_n1+j]+=af; // here weight is always 1. This is the slowest part.
					}
				}
			}

			// check deriv for second layer only
			/*double prob2,d,r;
			for(j=num_inputs*num_n1;j<num_inputs*num_n1+num_n1+1;++j){
				prob=pass_forward_2_float(&bo,&d_nn,md->from,md->to,md->node_type,md->SEE,md->h,md->eval-md->alp,md->move_number);
				cnn2[j]+=0.01;
				prob2=pass_forward_2_float(&bo,&d_nn,md->from,md->to,md->node_type,md->SEE,md->h,md->eval-md->alp,md->move_number);
				cnn2[j]-=0.01;
				d=(prob2-prob)/0.01;
				r=cl[j]/b;
				if( fabs(r-d)>.1 )
					b_m.node_count++;
			}
			b_m.node_count++;
			*/
		} // close loop over tt

		// update global coeffs?
		if( counter){
			update_coeffs(cl,counter,0);
			counter=0; // reset
		}
	}// end of the loop over TS
	if( l_rate>1.1e-24  && counter ) update_coeffs(cl,counter,0); // update coeffs on exit
	l2.acquire();
	RR1+=RR1_l;RR1a+=RR1a_l;
	cc+=cc_l;cca+=cca_l;
	l2.release();
	return(0);
}

int see_move(board *,unsigned int,unsigned int);
static void format_data(void){// load input data and format it
	move_data d;
	unsigned int j,rc=0;
	int k;
	char F[100];

	// load data
	pos_count0=0;
	FILE *f=fopen("c://xde//chess//out//moves.csv","r"); // input
	FILE *g=fopen("c://xde//chess//out//moves.bin","wb"); // output
	do{// loop over input records
		// get FEN
		char c;
		j=0;
		do{
			if( !fscanf(f,"%c",&c) || c=='\n' ) goto end_read;
			if( c==',' ) break;
			F[j++]=c;
		}while(1);
		F[j++]=0; // terminator

		init_board_FEN(F,&b_m); // this also sets all bitboards
		memcpy(d.pieces,b_m.piece,64);d.player=b_m.player; // copy into position.

		
		j=fscanf(f,"%i,",&k);d.LMR=(unsigned char)k;			// LMR=1+
		j=fscanf(f,"%i,",&k);d.SEE=(short int)k;				// SEE
		j=fscanf(f,"%i,",&k);d.node_type=(unsigned char)k;	// node type-1 (0/1/2)
		j=fscanf(f,"%i,",&k);d.depth=(unsigned char)k;		// depth
		j=fscanf(f,"%i,",&k);d.ply=(unsigned char)k;			// ply
		j=fscanf(f,"%i,",&k);d.alp=(short int)k;				// alp
		j=fscanf(f,"%i,",&k);								// be
		if( j!=1 ) goto end_read;
		j=fscanf(f,"%i,",&k);d.move_number=(unsigned char)k;	// move number=i
		j=fscanf(f,"%i,",&k);d.from=(unsigned char)k;			// to
		j=fscanf(f,"%i,",&k);d.to=(unsigned char)k;			// from
		j=fscanf(f,"%i,",&k);d.h=k;							// history
		j=fscanf(f,"%i\n",&k);d.cut=(unsigned char)k;			// cut
		
		// populate eval
		d.eval=eval(&b_m); // FEN is before the move

		// skip captures: some bad captures make it here.
		if( b_m.piece[d.to] )
			continue;

		// skip cut>1
		//if( d.cut>1 ) continue;
	
		//save item
		fwrite(&d,sizeof(d),1,g);








		// save it to a different file for external analysis*******************************************************
		static FILE *fe1=NULL,*fe2=NULL,*fe3=NULL;
		if( fe1==NULL ){
			fe1=fopen("c://xde//chess//out//dd1.bin","wb"); // external output - position
			fe2=fopen("c://xde//chess//out//dd2.bin","wb"); // external output - result
			fe3=fopen("c://xde//chess//out//dd3.csv","w"); // external output - result
			fprintf(fe3,"cut,hist,SEE,eval_alp,move_number,depth,LMR,ply,node_type\n");
		}
		float dl[12];
		float dl2;
		board *b=&b_m;
		memset(dl,0,sizeof(dl));
		unsigned int bb=0;
		unsigned int k=0;
		unsigned int from1=d.from;
		if( b->player==2 ) from1=flips[d.from][1];// from white's POV
		unsigned int to1=d.to;
		if( b->player==2 ) to1=flips[d.to][1];// from white's POV
		


		dl[k++]=float(d.SEE>=0?1:0);// SEE range: neg vs 0+.

		if( d.eval-d.alp>=0 ) dl[k]=1; // 0+
		else if( d.eval-d.alp>=-100 ) dl[k+1]=1; // -100 to 0
		else if( d.eval-d.alp>=-200 ) dl[k+2]=1; // -200 to -100
		//else dl[k+3]=1; // <-200
		k+=3;

		//dl[k++]=float(d.h);// history

		if( d.move_number<5 ) dl[k]=1;// move number
		else if( d.move_number<12 ) dl[k+1]=1;
		else if( d.move_number<29 ) dl[k+2]=1;
		//else dl[k+3]=1;
		k+=3;

		unsigned int mp=(b_m.piece[d.from]&7)-1; // type of moving piece, 0-5=6. No empties.
		if( mp<5) dl[k+mp]=1;
		k+=5;

		//if( d.node_type<2 ) dl[k+d.node_type]=1;
		//k+=2; // node type: 3. This is covering all, so don't need bias.


		// try again
		short int dli[10];
		dli[0]=d.SEE;
		dli[1]=d.eval-d.alp;
		dli[2]=d.h;
		dli[3]=d.move_number;
		dli[4]=mp;
		dli[5]=d.depth;
		dli[6]=d.LMR;
		dli[7]=d.ply;
		dli[8]=d.node_type;
		dli[9]=d.cut;

		fwrite(dli,sizeof(dli),1,fe1);
		//fwrite(dl,sizeof(dl),1,fe1);
		dl2=d.cut;
		fwrite(&dl2,sizeof(dl2),1,fe2);
		fprintf(fe3,"%d,%d,%d,%d,%d,%d,%d,%d,%d\n",d.cut,d.h,d.SEE,d.eval-d.alp,d.move_number,d.depth,d.LMR,d.ply,d.node_type);
		rc++;

		

		pos_count0++;
	}while(1);// end of loop over records
	end_read:
	fclose(f);fclose(g);

	
	//char res[200];
	//sprintf(res,"%u/%u records",pos_count0,rc);
	//MessageBox( hWnd_global, res,res,MB_ICONERROR | MB_OK );

	exit(0);
}

static float rr(void){
	static const float v[10]={-1.644853f,-1.036432877f,-0.674490366f,-0.385321073f,-0.125661472f,0.125661472f,0.385321073f,0.674490366f,1.036432877f,1.644853f};// normal
	unsigned int index=unsigned int(rand()*10./(1.+RAND_MAX));
	if( index>9 )
		exit(7);
	float s=v[index];
	return(s);
}

static void write_coeffs(int d){// write coeffs to file
	unsigned int i,j;
	char format[]="%.2f,";
	format[2]=(d%10)+'0'; // number of digits to print

	// first, text
	FILE *f=fopen("c://xde//chess//out//nn2_log1.csv","w");
	for(i=0;i<num_inputs;++i){ // here first row is bias (num_n1)
		for(j=0;j<num_n1;++j)
			fprintf(f,format,cnn2[i*num_n1+j]);
		fprintf(f,"\n");
	}
	fprintf(f,"\n");
	fprintf(f,"\n");
	fprintf(f,format,cnn2[num_inputs*num_n1+num_n1]);// here first column is bias (num_n2)
	for(j=0;j<num_n1;++j)
		fprintf(f,format,cnn2[num_inputs*num_n1+j]);
	fprintf(f,"\n");
	fclose(f);

	// second, binary
	if( d<10) f=fopen("c://xde//chess//out//nn2_log1.bin","wb");
	else f=fopen("c://xde//chess//out//nn2_log2.bin","wb"); // test - unreal file
	fwrite(cnn2,sizeof(cnn2),1,f);
	fclose(f);
}

void read_nn2_coeffs(void){// read NN coeffs from file
	unsigned int j;
	
	// init NN coeffs to small random values
	for(j=0;j<num_inputs*num_n1;++j) cnn2[j]=float(0.01*rr());

	// final layer coeffs
	for(j=0;j<num_n1;++j) cnn2[num_inputs*num_n1+j]=float(0.01*rr());
	//for(j=0;j<num_n1/2;++j) cnn2[num_inputs*num_n1+j]=0.01f;//float(0.01*rr());
	//for(;j<num_n1;++j) cnn2[num_inputs*num_n1+j]=-0.01f;//float(0.01*rr());
	// init bias to something
	cnn2[num_inputs*num_n1+num_n1]=0.0019352254f;
	

	
	FILE *f=fopen("c://xde//chess//out//nn2_log1.bin","rb");
	if( f!=NULL ){
		fread(cnn2,sizeof(cnn2),1,f);
		fclose(f);
	}

	//f=fopen("c://xde//chess//out//w1.bin","rb");fread(cnn2,sizeof(cnn2),1,f);fclose(f);
	//for(j=0;j<num_inputs*num_n1;++j)cnn2[j]=cnn2[j]*0.9f;
	//f=fopen("c://xde//chess//out//wf.bin","rb");fread(cnn2+num_inputs*num_n1,sizeof(cnn2),1,f);fclose(f);
	//f=fopen("c://xde//chess//out//bf.bin","rb");fread(cnn2+num_inputs*num_n1+num_n1,sizeof(cnn2),1,f);fclose(f);
}

void run_nn2(void){// main function
	unsigned int j;
	DWORD t1=get_time();
	srand(t1);
	//srand(78457);

	// read coeffs from file (or init to random if file does not exist)
	read_nn2_coeffs();
	
	// try load updated file
	FILE *f=fopen("c://xde//chess//out//moves.bin","rb");
	if( f!=NULL ){
		// load new TS
		pos_count0=11500000; // prelim size, positions (not moves!)
		ts_all=(move_data*)malloc(sizeof(move_data)*pos_count0); // storage of ts entries - XX bytes each.
		pos_count0=(unsigned int)fread(ts_all,sizeof(move_data),pos_count0,f);
		fclose(f);
	}else
		format_data(); // create new TS and exit


	// train the NN
	#define nn_threads 12				// 12: total number of calc threads
	l_rate=0.001;						// learning rate: 1e-2
	batch_size=100000;					// batch size: 10K
	unsigned int itermax=200;			// number of training iterations

	
	// evaluate R2 - initial
	HANDLE h[nn_threads+1];	// calculation thread handle
	double l_ratel=l_rate;l_rate=1e-25; // save and zero out
	ii=0;cc=cca=0;RR1=RR1a=0.; // reset
	pred_mult=1;
	l2.release();
	for(j=0;j<nn_threads-1;++j) h[j]=CreateThread(NULL,0,train_nn,(PVOID)j,0,NULL);//sec.attr., stack, function, param,flags, thread id
	train_nn((PVOID)j);
	WaitForMultipleObjects(nn_threads-1,h,TRUE,INFINITE);// wait for threads to terminate.
	for(j=0;j<nn_threads-1;++j)  CloseHandle(h[j]);
	double RR1l=1000*sqrt(RR1/cc);double RR1la=1000*sqrt(RR1a/max(cca,1));

	ii=0;cc=cca=0;RR1=RR1a=0.; // reset
	pred_mult=0;
	l2.release();
	for(j=0;j<nn_threads-1;++j) h[j]=CreateThread(NULL,0,train_nn,(PVOID)j,0,NULL);//sec.attr., stack, function, param,flags, thread id
	train_nn((PVOID)j);
	WaitForMultipleObjects(nn_threads-1,h,TRUE,INFINITE);// wait for threads to terminate.
	for(j=0;j<nn_threads-1;++j)  CloseHandle(h[j]);
	RR0=1000*sqrt(RR1/cc);RR0a=1000*sqrt(RR1a/max(cca,1));
	pred_mult=1;RR1=RR1l;RR1a=RR1la;l_rate=l_ratel; // restore

	f=fopen("c://xde//chess//out//nn_log.csv","w");
	fprintf(f,"num_n1=,%d\n",num_n1);
	fprintf(f,"i,RR0,RR1,RR0a,RR1a,dRR,dRRa,l_rate,batch_size,time\n");
	fprintf(f,"%d,%.5f,%.5f,%.5f,%.5f,%.9f,%.9f,%.2g,%d,%d\n",iter,RR0,RR1,RR0a,RR1a,RR1-RR0,RR1a-RR0a,l_rate,batch_size,(get_time()-t1)/1000);
	fclose(f);
	if( l_rate<1.1e-24) exit(0); // skip the rest if no training
	double RR1old[5000];
	for(iter=0;iter<itermax;iter++){
		// run training
		l2.release();
		ii=0;
		for(j=0;j<nn_threads-1;++j) h[j]=CreateThread(NULL,0,train_nn,(PVOID)j,0,NULL);//sec.attr., stack, function, param,flags, thread id
		train_nn((PVOID)j);
		WaitForMultipleObjects(nn_threads-1,h,TRUE,INFINITE);// wait for threads to terminate.
		for(j=0;j<nn_threads-1;++j)  CloseHandle(h[j]);
		update_coeffs((double*)&iter,0,1); // apply coeffs
		
		// evaluate R2
		l_ratel=l_rate; l_rate=1e-25; // save and zero out
		cc=cca=0;RR1=RR1a=0.; // reset
		ii=0;
		for(j=0;j<nn_threads-1;++j) h[j]=CreateThread(NULL,0,train_nn,(PVOID)j,0,NULL);//sec.attr., stack, function, param,flags, thread id
		train_nn((PVOID)j);
		WaitForMultipleObjects(nn_threads-1,h,TRUE,INFINITE);// wait for threads to terminate.
		for(j=0;j<nn_threads-1;++j)  if( h[j] ) CloseHandle(h[j]);
		l_rate=l_ratel; // restore
		RR1=1000*sqrt(RR1/cc);RR1a=1000*sqrt(RR1a/max(1,cca));

		f=fopen("c://xde//chess//out//nn_log.csv","a");
		fprintf(f,"%d,%.5f,%.5f,%.5f,%.5f,%.9f,%.9f,%.2g,%d,%d,%g,%g,%g,%g\n",iter,RR0,RR1,RR0a,RR1a,RR1-RR0,RR1a-RR0a,l_rate,batch_size,(get_time()-t1)/1000,lrmax,lravg,chmax,chavg);
		fclose(f);

		/*if( iter>20 && RR1>RR1old[iter-5] ){
			write_coeffs(18); // write coeffs to file
			exit(0);
		}*/

		if( !(RR1>1. || RR1<=1.) || isnan(RR1) )
			break; // break if nan

		/*double r=RR1;
		if( iter>15
			&& r>RR1old[iter-1] && r>RR1old[iter-2] && r>RR1old[iter-3] && r>RR1old[iter-4]
			&& RR1old[iter-1]>RR1old[iter-2] )
		break; // break if worse than previous 10. And two increases i na row.
		*/
		RR1old[iter]=RR1;
		

		// adjust parameters
		//if( iter>itermax/2 ) l_rate*=.95;
		if( (iter%100)==99 ) write_coeffs(18); // write coeffs to file every 100 iters
	}
	if( iter>3 ) write_coeffs(18); // write coeffs to file
	free(ts_all);
	exit(0);
}