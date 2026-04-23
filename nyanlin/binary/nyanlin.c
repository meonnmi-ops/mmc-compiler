#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <time.h>

#define GGUF_MAGIC 0x46554747u
#define MAX_META 600
#define MAX_TENSOR 512
#define MAXLAYERS 80

enum{T_U8=0,T_I8,T_U16,T_I16,T_U32,T_I32,T_F32,T_BOOL,T_STR,T_ARR,T_U64,T_I64,T_F64};
enum{G_F32=0,G_F16,G_Q40,G_Q41,G_Q50,G_Q51,G_Q80,G_Q2K=10,G_Q3K=11,G_Q5K=12,G_Q6K=13,G_Q8K=14};
#define QK_K 256
#define K_SCALE_SIZE 12

static FILE*GF;
static int ALGN=32;

static void die(const char*m){fprintf(stderr,"ERR: %s\n",m);exit(1);}
static void readx(void*buf,size_t sz,size_t n){if(fread(buf,sz,n,GF)!=n)die("Read err");}
static uint16_t ru16(void){uint16_t v;readx(&v,2,1);return v;}
static uint32_t ru32(void){uint32_t v;readx(&v,4,1);return v;}
static uint64_t ru64(void){uint64_t v;readx(&v,8,1);return v;}
static float rf32(void){float v;readx(&v,4,1);return v;}

static float f16f32(uint16_t h){
  int s=(h>>15)&1,e=(h>>10)&0x1F,m=h&0x3FF;
  if(e==0)return m?(s?-1:1)*ldexp(m/1024.0f,-14):0.0f;
  if(e==31)return m?NAN:(s?-INFINITY:INFINITY);
  float v=1.0f+m/1024.0f;v=ldexp(v,(int)e-15);return s?-v:v;
}

static void do_align(void){
  long p=ftell(GF);
  long a=((p+ALGN-1)/ALGN)*ALGN;
  if(a>p)fseek(GF,a-p,SEEK_CUR);
}

static char*rstr(void){
  uint64_t n=ru64();char*s=calloc(1,n+1);
  if(n>0)readx(s,1,n);do_align();return s;
}

typedef struct{char key[256];int type;char*sval;uint64_t ival;int atype;uint64_t alen;void*adata;}meta_t;
typedef struct{char name[256];int nd;uint64_t ne[4];int type;uint64_t off;}tinfo_t;
typedef struct{uint64_t nm,nt;meta_t meta[MAX_META];tinfo_t ten[MAX_TENSOR];void*data;size_t dsize;}gguf_t;

static int64_t gi(gguf_t*g,const char*k,int64_t d){
  for(uint64_t i=0;i<g->nm;i++)
    if(!strcmp(g->meta[i].key,k)){
      int t=g->meta[i].type;
      if(t>=T_U8&&t<=T_I32)return(int64_t)g->meta[i].ival;
      if(t>=T_U64&&t<=T_I64)return g->meta[i].ival;
    }
  return d;
}

static void*ga(gguf_t*g,const char*k,uint64_t*ol,int*ot){
  for(uint64_t i=0;i<g->nm;i++)
    if(!strcmp(g->meta[i].key,k)&&g->meta[i].type==T_ARR){
      if(ol)*ol=g->meta[i].alen;if(ot)*ot=g->meta[i].atype;
      return g->meta[i].adata;
    }
  return NULL;
}

static int ft2(gguf_t*g,const char*n){
  for(uint64_t i=0;i<g->nt;i++)
    if(!strcmp(g->ten[i].name,n))return(int)i;
  return-1;
}

static void*td(gguf_t*g,int i){return(i<0)?NULL:(uint8_t*)g->data+g->ten[i].off;}

static gguf_t*gguf_open(const char*path){
  gguf_t*g=calloc(1,sizeof(gguf_t));GF=fopen(path,"rb");if(!GF)die("open");
  fseek(GF,0,SEEK_END);size_t fsz=ftell(GF);fseek(GF,0,SEEK_SET);
  if(ru32()!=GGUF_MAGIC)die("Not GGUF");
  uint32_t ver=ru32();fprintf(stderr,"GGUF v%u\n",ver);
  g->nt=ru64();g->nm=ru64();
  for(uint64_t i=0;i<g->nm&&i<MAX_META;i++){
    meta_t*m=&g->meta[i];
    uint64_t kl=ru64();uint64_t kr=kl<255?kl:255;
    readx(m->key,1,kr);m->key[kr]=0;
    if(kl>=255)fseek(GF,kl-255,SEEK_CUR);
    do_align();m->type=ru32();
    switch(m->type){
    case T_U32:case T_I32:m->ival=ru32();break;
    case T_U16:case T_I16:m->ival=ru16();break;
    case T_U8:case T_I8:case T_BOOL:{uint8_t v;readx(&v,1,1);m->ival=v;break;}
    case T_F32:{float v=rf32();memcpy(&m->ival,&v,4);break;}
    case T_U64:case T_I64:m->ival=ru64();break;
    case T_STR:m->sval=rstr();break;
    case T_ARR:{
      m->atype=ru32();m->alen=ru64();
      if(m->atype==T_STR){
        char**sa=calloc(m->alen+1,sizeof(char*));
        for(uint64_t j=0;j<m->alen;j++)sa[j]=rstr();m->adata=sa;
      }else{
        size_t esz=0;switch(m->atype){
        case T_U32:case T_I32:case T_F32:esz=4;break;
        case T_U16:case T_I16:esz=2;break;
        case T_U8:case T_I8:case T_BOOL:esz=1;break;
        case T_U64:case T_I64:case T_F64:esz=8;break;}
        if(esz>0&&m->alen<1e8){
          m->adata=calloc(m->alen+1,esz);
          readx(m->adata,esz,m->alen);do_align();
        }else{fseek(GF,(long)esz*(long)m->alen,SEEK_CUR);do_align();}
      }break;}
    default:break;}
  }
  for(uint64_t i=0;i<g->nt&&i<MAX_TENSOR;i++){
    tinfo_t*t=&g->ten[i];uint64_t nl=ru64();uint64_t nr=nl<255?nl:255;
    readx(t->name,1,nr);t->name[nr]=0;
    if(nl>=255)fseek(GF,nl-255,SEEK_CUR);
    do_align();t->nd=ru32();
    for(int d=0;d<t->nd;d++)t->ne[d]=ru64();
    t->type=ru32();t->off=ru64();
  }
  ALGN=(int)gi(g,"general.alignment",32);
  fprintf(stderr,"Align:%d\n",ALGN);
  long pos=ftell(GF);long doff=((pos+ALGN-1)/ALGN)*ALGN;
  if(doff>pos)fseek(GF,doff-pos,SEEK_CUR);
  long dp=ftell(GF);size_t dsz=fsz-dp;
  g->data=malloc(dsz);readx(g->data,1,dsz);g->dsize=dsz;
  fclose(GF);return g;
}

static void dequant(uint8_t*p,float*o,int t,int n){int i=0;
  if(t==G_Q40){for(;i+31<n;i+=32){float d=f16f32(*(uint16_t*)p);p+=2;
    for(int j=0;j<16;j++){uint8_t b=p[j];
      o[i+j]=(((int8_t)((b&0x0F)<<4))>>4)*d;
      o[i+j+16]=(((int8_t)(b&0xF0))>>4)*d;}p+=16;}}
  else if(t==G_Q41){for(;i+31<n;i+=32){float d=f16f32(*(uint16_t*)p);p+=2;
    float mm=f16f32(*(uint16_t*)p);p+=2;
    for(int j=0;j<16;j++){uint8_t b=p[j];
      o[i+j]=mm+(((int8_t)((b&0x0F)<<4))>>4)*d;
      o[i+j+16]=mm+(((int8_t)(b&0xF0))>>4)*d;}p+=16;}}
  else if(t==G_Q80){for(;i+31<n;i+=32){float d=f16f32(*(uint16_t*)p);p+=2;
    for(int j=0;j<32;j++)o[i+j]=((int8_t)p[j])*d;p+=32;}}
  else if(t==G_Q5K){
    for(;i+255<n;i+=256){
      float d=f16f32(*(uint16_t*)p);p+=2;
      float dmin=f16f32(*(uint16_t*)p);p+=2;
      uint8_t sc[12];memcpy(sc,p,12);p+=12;
      uint32_t qh=*(uint32_t*)p;p+=4;
      uint8_t ql[128];memcpy(ql,p,128);p+=128;
      float scales[8];int s=0;
      for(int j=0;j<4;j++)scales[j]=((sc[j]>>4)&0xF)*d;
      for(int j=4;j<8;j++)scales[j]=(sc[j]&0xF)*d;
      for(int j=0;j<4;j++)scales[j]+=scales[j+4]*dmin/d;
      for(int j=0;j<4;j++)scales[j+4]+=scales[j]*dmin/d;
      for(int sb=0;sb<8;sb++)for(int j=0;j<32;j++){
        int idx=i+sb*32+j;
        int qlv=ql[sb*32+j];
        int qhv=(qh>>(j/2))&1;
        int q=(((qlv&0xF)|qhv<<4)-16)*2;
        if(j%2==0)q=((qlv>>4)|((qh>>(j/2))&1)<<4)-16;
        o[idx]=dmin+scales[sb]*q;}}
    if(i<n)memcpy(o+i,p,(n-i)*4);}
  else if(t==G_F16){for(;i<n;i++)o[i]=f16f32(((uint16_t*)p)[i]);}
  else{memcpy(o,p,n*sizeof(float));}
}

static void matmul(float*a,float*b,float*c,float*o,int ic,int oc){
  for(int r=0;r<oc;r++){float s=c?c[r]:0;
    for(int j=0;j<ic;j++)s+=a[j]*b[r*ic+j];o[r]=s;}}

static void rmsnorm(float*o,float*x,float*g,int n){
  float ss=0;for(int i=0;i<n;i++)ss+=x[i]*x[i];
  float s=1/sqrtf(ss/n+1e-5f);
  for(int i=0;i<n;i++)o[i]=x[i]*s*g[i];}

static void silu(float*x,int n){for(int i=0;i<n;i++)x[i]=x[i]/(1+expf(-x[i]));}

static void rope(float*q,float*k,int pos,int hd,int nh){
  for(int i=0;i<nh;i++)for(int j=0;j<hd/2;j++){
    float f=1/powf(10000,2.0*j/hd),t=pos*f,c=cosf(t),s=sinf(t);
    int a=i*hd+j,b=a+hd/2;
    float qa=q[a],qb=q[b],ka=k[a],kb=k[b];
    q[a]=qa*c-qb*s;q[b]=qa*s+qb*c;
    k[a]=ka*c-kb*s;k[b]=ka*s+kb*c;}}

typedef struct{int dim,nl,nh,nkv,hd,ffn,mseq;float*embd,*onorm,*outw;
  float*an[MAXLAYERS],*wq[MAXLAYERS],*wk[MAXLAYERS],*wv[MAXLAYERS],*wo[MAXLAYERS];
  float*fn[MAXLAYERS],*wg[MAXLAYERS],*wu[MAXLAYERS],*wd[MAXLAYERS];
  char**vocab;int vsz,bos,eos;
  float*kc[MAXLAYERS],*vc[MAXLAYERS];int cp;}M;

static char tb[512];
static const char*tn(const char*f,int i){snprintf(tb,512,f,i);return tb;}

static float*ld(gguf_t*g,const char*n){
  int ti=ft2(g,n);if(ti<0)return NULL;
  tinfo_t*t=&g->ten[ti];int nn=1;
  for(int d=0;d<t->nd;d++)nn*=t->ne[d];
  float*o=malloc(nn*sizeof(float));
  dequant(td(g,ti),o,t->type,nn);return o;}

static M*load(gguf_t*g){
  M*m=calloc(1,sizeof(M));
  m->dim=gi(g,"llama.embedding_length",0);
  if(!m->dim)m->dim=gi(g,"qwen2.embedding_length",0);
  m->nl=gi(g,"llama.block_count",0);
  if(!m->nl)m->nl=gi(g,"qwen2.block_count",0);
  m->nh=gi(g,"llama.attention.head_count",0);
  if(!m->nh)m->nh=gi(g,"qwen2.attention.head_count",0);
  m->nkv=gi(g,"llama.attention.head_count_kv",m->nh);
  m->hd=m->dim/m->nh;m->ffn=gi(g,"llama.feed_forward_length",0);
  if(!m->ffn)m->ffn=gi(g,"qwen2.feed_forward_length",0);
  m->mseq=gi(g,"llama.context_length",2048);
  m->bos=gi(g,"tokenizer.ggml.bos_token_id",1);
  m->eos=gi(g,"tokenizer.ggml.eos_token_id",2);
  uint64_t vl=0;int vt=0;
  ga(g,"tokenizer.ggml.tokens",&vl,&vt);
  m->vsz=vl;m->vocab=calloc(m->vsz+1,sizeof(char*));
  for(uint64_t i=0;i<g->nm&&i<MAX_META;i++){
    meta_t*mt=&g->meta[i];
    if(!strcmp(mt->key,"tokenizer.ggml.tokens")&&mt->type==T_ARR&&mt->atype==T_STR){
      char**sa=mt->adata;
      for(int j=0;j<m->vsz&&j<(int)mt->alen;j++)m->vocab[j]=sa[j];}}
  m->embd=ld(g,"token_embd.weight");
  if(!m->embd)m->embd=ld(g,"model.embed_tokens.weight");
  m->onorm=ld(g,"output_norm.weight");
  if(!m->onorm)m->onorm=ld(g,"model.norm.weight");
  m->outw=ld(g,"output.weight");
  if(!m->outw)m->outw=ld(g,"lm_head.weight");
  for(int i=0;i<m->nl;i++){
    m->an[i]=ld(g,tn("blk.%d.attn_norm.weight",i));
    m->wq[i]=ld(g,tn("blk.%d.attn_q.weight",i));
    m->wk[i]=ld(g,tn("blk.%d.attn_k.weight",i));
    m->wv[i]=ld(g,tn("blk.%d.attn_v.weight",i));
    m->wo[i]=ld(g,tn("blk.%d.attn_output.weight",i));
    m->fn[i]=ld(g,tn("blk.%d.ffn_norm.weight",i));
    m->wg[i]=ld(g,tn("blk.%d.ffn_gate.weight",i));
    if(!m->wg[i])m->wg[i]=ld(g,tn("blk.%d.feed_forward.w1.weight",i));
    m->wu[i]=ld(g,tn("blk.%d.ffn_up.weight",i));
    if(!m->wu[i])m->wu[i]=ld(g,tn("blk.%d.feed_forward.w3.weight",i));
    m->wd[i]=ld(g,tn("blk.%d.ffn_down.weight",i));
    if(!m->wd[i])m->wd[i]=ld(g,tn("blk.%d.feed_forward.w2.weight",i));
    m->kc[i]=calloc(m->mseq*m->nkv*m->hd,sizeof(float));
    m->vc[i]=calloc(m->mseq*m->nkv*m->hd,sizeof(float));}
  fprintf(stderr,"dim=%d L=%d H=%d V=%d\n",m->dim,m->nl,m->nh,m->vsz);
  return m;}

static int tok(M*m,const char*t,int*o,int mx){
  int n=0;const char*s=t;int sl=strlen(t);
  for(int i=0;i<sl&&n<mx;){
    int best=-1,bl=0;
    for(int tl=1;tl<=4&&i+tl<=sl;tl++)
      for(int v=0;v<m->vsz;v++)
        if(m->vocab[v]&&strlen(m->vocab[v])==(size_t)tl
          &&!memcmp(m->vocab[v],s+i,tl))
          if(tl>bl){bl=tl;best=v;}
    if(best>=0){o[n++]=best;i+=bl;}
    else{o[n++]=s[i]%94+33;i++;}}
  return n;}

static void dtok(M*m,int t,char*b,int bs){
  if(t<0||t>=m->vsz||!m->vocab[t]){b[0]=0;return;}
  snprintf(b,bs,"%s",m->vocab[t]);}

static void fwd(M*m,int tk,float*logits){
  int d=m->dim,hd=m->hd,nh=m->nh,nkv=m->nkv,ffn=m->ffn,pos=m->cp;
  float*x=calloc(d,sizeof(float)),*xb=calloc(d,sizeof(float));
  float*q=calloc(d,sizeof(float)),*k=calloc(nkv*hd,sizeof(float));
  float*v=calloc(nkv*hd,sizeof(float)),*at=calloc(nh*2048,sizeof(float));
  float*xb2=calloc(d,sizeof(float)),*hb=calloc(ffn,sizeof(float));
  float*hb2=calloc(ffn,sizeof(float));
  if(m->embd)memcpy(x,m->embd+tk*d,d*sizeof(float));
  for(int L=0;L<m->nl;L++){
    rmsnorm(xb,x,m->an[L],d);
    matmul(xb,m->wq[L],NULL,q,d,d);
    matmul(xb,m->wk[L],NULL,k,d,nkv*hd);
    matmul(xb,m->wv[L],NULL,v,d,nkv*hd);
    rope(q,k,pos,hd,nh);
    if(pos<2048){
      memcpy(m->kc[L]+pos*nkv*hd,k,nkv*hd*sizeof(float));
      memcpy(m->vc[L]+pos*nkv*hd,v,nkv*hd*sizeof(float));}
    for(int h=0;h<nh;h++){
      float*qh=q+h*hd,*ao=at+h*2048,mx=-1e30f;
      for(int t=0;t<=pos;t++){
        float*kt=m->kc[L]+t*nkv*hd+(h%nkv)*hd;
        float s=0;for(int j=0;j<hd;j++)s+=qh[j]*kt[j];
        s/=sqrtf(hd);ao[t]=s;if(s>mx)mx=s;}
      float ss=0;for(int t=0;t<=pos;t++){ao[t]=expf(ao[t]-mx);ss+=ao[t];}
      for(int t=0;t<=pos;t++)ao[t]/=ss;
      float*oh=q+h*hd;
      for(int j=0;j<hd;j++)oh[j]=0;
      for(int t=0;t<=pos;t++){
        float*vt=m->vc[L]+t*nkv*hd+(h%nkv)*hd;
        for(int j=0;j<hd;j++)oh[j]+=ao[t]*vt[j];}}
    matmul(q,m->wo[L],NULL,xb2,d,d);
    for(int i=0;i<d;i++)x[i]+=xb2[i];
    rmsnorm(xb,x,m->fn[L],d);
    if(m->wg[L]){
      matmul(xb,m->wg[L],NULL,hb,d,ffn);silu(hb,ffn);
      matmul(xb,m->wu[L],NULL,hb2,d,ffn);
      for(int i=0;i<ffn;i++)hb[i]*=hb2[i];
      matmul(hb,m->wd[L],NULL,xb2,ffn,d);
    }else{
      matmul(xb,m->wu[L],NULL,hb,d,ffn);silu(hb,ffn);
      matmul(hb,m->wd[L],NULL,xb2,ffn,d);}
    for(int i=0;i<d;i++)x[i]+=xb2[i];}
  rmsnorm(x,x,m->onorm,d);
  if(m->outw)matmul(x,m->outw,NULL,logits,d,m->vsz);
  free(x);free(xb);free(q);free(k);free(v);
  free(at);free(xb2);free(hb);free(hb2);
  m->cp=pos+1;}

int main(int ac,char**av){
  if(ac<2){fprintf(stderr,"NYANLIN v3.0\nUsage: %s <model.gguf> [prompt]\n",av[0]);return 1;}
  fprintf(stderr,"[N] Loading %s\n",av[1]);
  clock_t t0=clock();
  gguf_t*g=gguf_open(av[1]);
  fprintf(stderr,"[N] %llu tensors %.1fMB\n",(unsigned long long)g->nt,g->dsize/1048576.0);
  M*m=load(g);
  fprintf(stderr,"[N] %.2fs\n",(double)(clock()-t0)/CLOCKS_PER_SEC);
  if(m->vsz<=0){fprintf(stderr,"No vocab\n");return 1;}
  if(m->nl==0){fprintf(stderr,"No layers\n");return 1;}
  float*logits=calloc(m->vsz,sizeof(float));
  int*toks=calloc(2048,sizeof(int));
  if(ac>=3){
    int n=tok(m,av[2],toks,2048);fprintf(stderr,"[N] %d tokens\n",n);
    for(int i=0;i<n;i++)fwd(m,toks[i],logits);
    m->cp=n;int tk=toks[n-1];char buf[64];
    printf("\n---\n");
    for(int i=0;i<2048-n;i++){
      fwd(m,tk,logits);int b=0;
      for(int j=1;j<m->vsz;j++)if(logits[j]>logits[b])b=j;
      tk=b;if(tk==m->eos)break;
      dtok(m,tk,buf,64);printf("%s",buf);fflush(stdout);}
    printf("\n---\n");
  }else{
    printf("NYANLIN v3.0 Interactive\n");char ln[4096];
    while(printf("> ")&&fgets(ln,4096,stdin)){
      if(!strncmp(ln,"quit",4))break;
      ln[strcspn(ln,"\n")]=0;if(!ln[0])continue;
      m->cp=0;int n=tok(m,ln,toks,2048);
      for(int i=0;i<n;i++)fwd(m,toks[i],logits);
      int tk=toks[n-1];char buf[64];
      for(int i=0;i<256;i++){
        fwd(m,tk,logits);int b=0;
        for(int j=1;j<m->vsz;j++)if(logits[j]>logits[b])b=j;
        tk=b;if(tk==m->eos)break;
        dtok(m,tk,buf,64);printf("%s",buf);fflush(stdout);}
      printf("\n");}}
  fprintf(stderr,"[N] Done\n");return 0;}
