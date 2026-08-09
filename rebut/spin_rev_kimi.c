#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_WINNT 0x0600
#include <windows.h>
#define _USE_MATH_DEFINES
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_BOLHAS 256
#define MAX_PONTOS 500
#define L 100
#define R_FUGA 15.0f
#define FASE_EXPANSAO_FRAMES 10
#define MAX_HIST 200

float *hist_cmx=NULL,*hist_cmy=NULL,*hist_cmz=NULL;
int hist_count=0,hist_capacity=0;

float *hist_c0x=NULL,*hist_c0y=NULL,*hist_c0z=NULL;
float *hist_c1x=NULL,*hist_c1y=NULL,*hist_c1z=NULL;
int hist_c0_count=0,hist_c0_cap=0;
int hist_c1_count=0,hist_c1_cap=0;

int window_width=800,window_height=800;

typedef struct{float dx,dy,dz;}PontoSuperficie;
typedef struct{
    float cx,cy,cz,r,mx,my,mz,sx,sy,sz;
    PontoSuperficie pontos[MAX_PONTOS];
    int tipo,modo_m;
    float dir_x,dir_y,dir_z;
    int timeout,cacique_idx;
}Bolha;

typedef struct{
    int a,b,info_alvo,consome_timeout,toggle_modo;
    float ax,ay,az,bx,by,bz,info_x,info_y,info_z;
}Interacao;

enum Vista{VISTA_XY,VISTA_XZ,VISTA_YZ,VISTA_ISO};
enum Vista vista_atual=VISTA_XY;
float zoom=1.0f;
bool zoom_out_ativado=false;
bool mostrar_bolhas=true;
bool modo_somente_cm=false;
bool mostrar_somente_caciques=false;
float pan_x=0.0f,pan_y=0.0f,pan_z=0.0f,pan_speed=2.0f;

void normalizar3D(float*x,float*y,float*z){
    float n=sqrtf((*x)*(*x)+(*y)*(*y)+(*z)*(*z));
    if(n>0.0001f){*x/=n;*y/=n;*z/=n;}
    else{*x=1.0f;*y=0.0f;*z=0.0f;}
}

void inicializar_pontos(Bolha*b){
    for(int i=0;i<MAX_PONTOS;i++){
        float u=((float)rand()/RAND_MAX)*2.0f-1.0f;
        float theta=((float)rand()/RAND_MAX)*2.0f*(float)M_PI;
        float phi=asinf(u);
        b->pontos[i].dx=cosf(phi)*cosf(theta);
        b->pontos[i].dy=cosf(phi)*sinf(theta);
        b->pontos[i].dz=sinf(phi);
    }
}

void inicializar_bolha(Bolha*b,float cx,float cy,float cz,float raio,
                       float mx,float my,float mz,
                       float ax,float ay,float az,int tipo){
    b->cx=cx;b->cy=cy;b->cz=cz;b->r=raio;b->tipo=tipo;
    b->mx=mx;b->my=my;b->mz=mz;
    normalizar3D(&b->mx,&b->my,&b->mz);
    b->sx=b->my*az-b->mz*ay;
    b->sy=b->mz*ax-b->mx*az;
    b->sz=b->mx*ay-b->my*ax;
    if(sqrtf(b->sx*b->sx+b->sy*b->sy+b->sz*b->sz)<0.0001f){
        b->sx=0.0f;b->sy=b->mz;b->sz=-b->my;
    }
    normalizar3D(&b->sx,&b->sy,&b->sz);
    b->modo_m=0;b->cacique_idx=-1;
    inicializar_pontos(b);
}

void reemitir_bolha(Bolha*b,float cx,float cy,float cz){
    b->cx=cx;b->cy=cy;b->cz=cz;b->r=0.0001f;
}

void dir_para_cacique_mais_proximo(Bolha*b,Bolha*bolhas,int num_bolhas,
                                   float*dx,float*dy,float*dz){
    int caciques[2]={0,128};
    float best_d=1e20f;
    int best=-1;
    for(int k=0;k<2;k++){
        int idx=caciques[k];
        if(idx>=num_bolhas)continue;
        float tx=bolhas[idx].cx-b->cx;
        float ty=bolhas[idx].cy-b->cy;
        float tz=bolhas[idx].cz-b->cz;
        float d=tx*tx+ty*ty+tz*tz;
        if(d<best_d){best_d=d;best=idx;}
    }
    if(best>=0){
        *dx=bolhas[best].cx-b->cx;
        *dy=bolhas[best].cy-b->cy;
        *dz=bolhas[best].cz-b->cz;
        normalizar3D(dx,dy,dz);
    }else{*dx=*dy=*dz=0.0f;}
}

int detectar_interacao(Bolha*a,Bolha*b,int idx_a,int idx_b,
                       Bolha*bolhas,int num_bolhas,Interacao*h){
    (void)bolhas;(void)num_bolhas;
    h->a=idx_a;h->b=idx_b;
    h->info_alvo=-1;h->consome_timeout=0;h->toggle_modo=0;
    if(b->tipo!=0)return 0;
    int a_cacique=(idx_a%128==0);
    int b_cacique=(idx_b%128==0);
    if(b_cacique&&!a_cacique&&a->tipo!=1)return 0;
    if(a_cacique&&b->timeout>0&&b->cacique_idx!=-1&&b->cacique_idx!=idx_a)return 0;
    if(a->timeout>0&&b->timeout>0&&
       a->cacique_idx!=-1&&b->cacique_idx!=-1&&
       a->cacique_idx!=b->cacique_idx)return 0;
    float vx,vy,vz;
    int s=0;
    if(a->timeout>0){vx=a->dir_x;vy=a->dir_y;vz=a->dir_z;}
    else if(a->tipo==1){vx=a->mx;vy=a->my;vz=a->mz;}
    else if(a_cacique){
        s=a->modo_m&3;
        if(s==0){vx=a->mx;vy=a->my;vz=a->mz;}
        else if(s==1){vx=a->sx;vy=a->sy;vz=a->sz;}
        else if(s==2){vx=-a->mx;vy=-a->my;vz=-a->mz;}
        else{vx=-a->sx;vy=-a->sy;vz=-a->sz;}
    }else{
        if(a->modo_m){vx=a->sx;vy=a->sy;vz=a->sz;}
        else{vx=a->mx;vy=a->my;vz=a->mz;}
    }
    float tip_x=a->cx+a->r*vx;
    float tip_y=a->cy+a->r*vy;
    float tip_z=a->cz+a->r*vz;
    float dx=tip_x-b->cx;
    float dy=tip_y-b->cy;
    float dz=tip_z-b->cz;
    if(dx*dx+dy*dy+dz*dz>=(b->r+0.001f)*(b->r+0.001f))return 0;
    float vbx,vby,vbz;
    int sb=0;
    if(b->timeout>0){vbx=b->dir_x;vby=b->dir_y;vbz=b->dir_z;}
    else if(b_cacique){
        sb=b->modo_m&3;
        if(sb==0){vbx=b->mx;vby=b->my;vbz=b->mz;}
        else if(sb==1){vbx=b->sx;vby=b->sy;vbz=b->sz;}
        else if(sb==2){vbx=-b->mx;vby=-b->my;vbz=-b->mz;}
        else{vbx=-b->sx;vby=-b->sy;vbz=-b->sz;}
    }else if(b->tipo==1){vbx=b->mx;vby=b->my;vbz=b->mz;}
    else{
        if(b->modo_m){vbx=b->sx;vby=b->sy;vbz=b->sz;}
        else{vbx=b->mx;vby=b->my;vbz=b->mz;}
    }
    float bx_=b->cx+b->r*vbx;
    float by_=b->cy+b->r*vby;
    float bz_=b->cz+b->r*vbz;
    if(a_cacique&&b_cacique){
        h->ax=a->cx-a->r*a->mx;
        h->ay=a->cy-a->r*a->my;
        h->az=a->cz-a->r*a->mz;
        h->bx=b->cx+b->r*vx;
        h->by=b->cy+b->r*vy;
        h->bz=b->cz+b->r*vz;
    }else if(a_cacique){
        float wx,wy,wz;
        if(s==0){wx=a->sx;wy=a->sy;wz=a->sz;}
        else if(s==1){wx=-a->mx;wy=-a->my;wz=-a->mz;}
        else if(s==2){wx=-a->sx;wy=-a->sy;wz=-a->sz;}
        else{wx=a->mx;wy=a->my;wz=a->mz;}
        h->ax=a->cx+a->r*wx;
        h->ay=a->cy+a->r*wy;
        h->az=a->cz+a->r*wz;
        h->bx=tip_x;h->by=tip_y;h->bz=tip_z;
        h->info_x=a->cx-h->bx;
        h->info_y=a->cy-h->by;
        h->info_z=a->cz-h->bz;
        h->info_alvo=idx_b;
    }else{
        h->ax=tip_x;h->ay=tip_y;h->az=tip_z;
        h->bx=bx_;h->by=by_;h->bz=bz_;
    }
    if(!a_cacique&&!b_cacique&&a->timeout>0){
        float ix=a->dir_x,iy=a->dir_y,iz=a->dir_z;
        normalizar3D(&ix,&iy,&iz);
        h->bx=b->cx+b->r*ix;
        h->by=b->cy+b->r*iy;
        h->bz=b->cz+b->r*iz;
        h->info_x=ix;h->info_y=iy;h->info_z=iz;
        h->info_alvo=idx_b;
    }
    if(a->timeout>0)h->consome_timeout=1;
    h->toggle_modo=0;
    return 1;
}

void aplicar_interacao(const Interacao*h,Bolha*bolhas,int num_bolhas){
    (void)num_bolhas;
    float cax=bolhas[h->a].cx,cay=bolhas[h->a].cy,caz=bolhas[h->a].cz;
    float cbx=bolhas[h->b].cx,cby=bolhas[h->b].cy,cbz=bolhas[h->b].cz;

    reemitir_bolha(&bolhas[h->a],h->ax,h->ay,h->az);
    reemitir_bolha(&bolhas[h->b],h->bx,h->by,h->bz);

    if(h->a%128==0){
        bolhas[h->a].cx=cax;bolhas[h->a].cy=cay;bolhas[h->a].cz=caz;
    }
    if(h->b%128==0){
        bolhas[h->b].cx=cbx;bolhas[h->b].cy=cby;bolhas[h->b].cz=cbz;
    }

    if(h->info_alvo>=0){
        float ix=h->info_x,iy=h->info_y,iz=h->info_z;
        normalizar3D(&ix,&iy,&iz);
        Bolha*alvo=&bolhas[h->info_alvo];
        alvo->dir_x=ix;alvo->dir_y=iy;alvo->dir_z=iz;
        int cacique_fonte=-1;
        if(h->a%128==0)cacique_fonte=h->a;
        else if(bolhas[h->a].timeout>0&&bolhas[h->a].cacique_idx!=-1)cacique_fonte=bolhas[h->a].cacique_idx;
        if(cacique_fonte!=-1){
            alvo->cacique_idx=cacique_fonte;
            float rx=bolhas[cacique_fonte].cx-alvo->cx;
            float ry=bolhas[cacique_fonte].cy-alvo->cy;
            float rz=bolhas[cacique_fonte].cz-alvo->cz;
            normalizar3D(&rx,&ry,&rz);
            float alinhamento=alvo->sx*rx+alvo->sy*ry+alvo->sz*rz;
            alvo->timeout=(alinhamento>0.3f)?(L*3):L;
        }else alvo->timeout=L;
    }
    if(h->consome_timeout){
        Bolha*a=&bolhas[h->a];
        if(a->timeout>0){
            a->timeout--;
            if(a->timeout==0)a->dir_x=a->dir_y=a->dir_z=0.0f;
        }
    }
    for(int lado=0;lado<2;lado++){
        int idx=(lado==0)?h->a:h->b;
        if((idx%128!=0)&&bolhas[idx].tipo==0&&bolhas[idx].timeout==0){
            bolhas[idx].modo_m=(bolhas[idx].modo_m+1)%2;
        }
    }
    if(h->a%128==0){Bolha*a=&bolhas[h->a];a->modo_m=(a->modo_m+1)%4;}
    if(h->b%128==0){Bolha*b=&bolhas[h->b];b->modo_m=(b->modo_m+1)%4;}
}

void project(float x,float y,float z,float*px,float*py){
    float scale=(window_width<window_height?window_width:window_height)/14.0f;
    scale*=zoom;
    if(!isfinite(x)||!isfinite(y)||!isfinite(z)){x=y=z=0;}
    x+=pan_x;y+=pan_y;z+=pan_z;
    switch(vista_atual){
        case VISTA_XY:*px=(window_width/2.0f)+x*scale;*py=(window_height/2.0f)-y*scale;break;
        case VISTA_XZ:*px=(window_width/2.0f)+x*scale;*py=(window_height/2.0f)-z*scale;break;
        case VISTA_YZ:*px=(window_width/2.0f)+y*scale;*py=(window_height/2.0f)-z*scale;break;
        case VISTA_ISO:*px=(window_width/2.0f)+(x-y)*scale;*py=(window_height/2.0f)-((x+y)/2.0f-z)*scale;break;
    }
}

void desenhar_circulo(SDL_Renderer*ren,float cx,float cy,float cz,float raio,Uint8 r,Uint8 g,Uint8 b){
    int segmentos=24;
    float px_ant,py_ant;
    int primeiro=1;
    for(int i=0;i<=segmentos;i++){
        float ang=2.0f*(float)M_PI*i/segmentos;
        float px,py;
        project(cx+raio*cosf(ang),cy+raio*sinf(ang),cz,&px,&py);
        if(!primeiro){
            SDL_SetRenderDrawColor(ren,r,g,b,255);
            SDL_RenderLine(ren,px_ant,py_ant,px,py);
        }
        px_ant=px;py_ant=py;primeiro=0;
    }
}

void desenhar_seta(SDL_Renderer*ren,float cx,float cy,float cz,
                   float vx,float vy,float vz,float r,float color_r,float color_g,float color_b){
    float px1,py1,px2,py2;
    project(cx,cy,cz,&px1,&py1);
    project(cx+vx*r,cy+vy*r,cz+vz*r,&px2,&py2);
    SDL_SetRenderDrawColor(ren,(Uint8)color_r,(Uint8)color_g,(Uint8)color_b,255);
    SDL_RenderLine(ren,px1,py1,px2,py2);
    float tamanho_ponta=5.0f*(mostrar_bolhas?1.0f:0.5f);
    float dx=px2-px1;
    float dy=py2-py1;
    float len=sqrtf(dx*dx+dy*dy);
    if(len>0){
        float ux=dx/len;
        float uy=dy/len;
        float nx=-uy;
        float ny=ux;
        SDL_RenderLine(ren,px2,py2,px2-ux*tamanho_ponta-nx*(tamanho_ponta*0.6f),py2-uy*tamanho_ponta-ny*(tamanho_ponta*0.6f));
        SDL_RenderLine(ren,px2,py2,px2-ux*tamanho_ponta+nx*(tamanho_ponta*0.6f),py2-uy*tamanho_ponta+ny*(tamanho_ponta*0.6f));
    }
}

void desenhar_trajetoria_cm(SDL_Renderer*ren,Bolha*b,int num_bolhas){
    float cmx=0,cmy=0,cmz=0;
    int count=0;
    for(int k=0;k<num_bolhas;k++){
        if(b[k].tipo==0){cmx+=b[k].cx;cmy+=b[k].cy;cmz+=b[k].cz;count++;}
    }
    if(count>0){cmx/=count;cmy/=count;cmz/=count;}
    if(hist_count==hist_capacity){
        int new_cap=hist_capacity==0?MAX_HIST:hist_capacity*2;
        hist_cmx=(float*)realloc(hist_cmx,sizeof(float)*new_cap);
        hist_cmy=(float*)realloc(hist_cmy,sizeof(float)*new_cap);
        hist_cmz=(float*)realloc(hist_cmz,sizeof(float)*new_cap);
        hist_capacity=new_cap;
    }
    hist_cmx[hist_count]=cmx;hist_cmy[hist_count]=cmy;hist_cmz[hist_count]=cmz;hist_count++;
    for(int i=1;i<hist_count;i++){
        float px0,py0,px1,py1;
        project(hist_cmx[i-1],hist_cmy[i-1],hist_cmz[i-1],&px0,&py0);
        project(hist_cmx[i],hist_cmy[i],hist_cmz[i],&px1,&py1);
        SDL_SetRenderDrawColor(ren,0,255,128,120);
        SDL_RenderLine(ren,px0,py0,px1,py1);
    }
    float px,py;
    project(cmx,cmy,cmz,&px,&py);
    SDL_SetRenderDrawColor(ren,255,255,255,255);
    SDL_FRect r={px-3,py-3,6,6};
    SDL_RenderFillRect(ren,&r);
}

static void gravar_cacique(Bolha*b,int idx,
                           float**hx,float**hy,float**hz,
                           int*hcount,int*hcap){
    if(*hcount==*hcap){
        int new_cap=*hcap==0?MAX_HIST:*hcap*2;
        *hx=(float*)realloc(*hx,sizeof(float)*new_cap);
        *hy=(float*)realloc(*hy,sizeof(float)*new_cap);
        *hz=(float*)realloc(*hz,sizeof(float)*new_cap);
        *hcap=new_cap;
    }
    (*hx)[*hcount]=b[idx].cx;
    (*hy)[*hcount]=b[idx].cy;
    (*hz)[*hcount]=b[idx].cz;
    (*hcount)++;
}

static void desenhar_trajetoria_cacique(SDL_Renderer*ren,
                                        float*hx,float*hy,float*hz,int hcount,
                                        Uint8 r,Uint8 g,Uint8 b){
    if(hcount<2)return;
    for(int i=1;i<hcount;i++){
        float px0,py0,px1,py1;
        project(hx[i-1],hy[i-1],hz[i-1],&px0,&py0);
        project(hx[i],hy[i],hz[i],&px1,&py1);
        SDL_SetRenderDrawColor(ren,r,g,b,255);
        SDL_RenderLine(ren,px0,py0,px1,py1);
        SDL_RenderLine(ren,px0-1,py0,px1-1,py1);
        SDL_RenderLine(ren,px0+1,py0,px1+1,py1);
        SDL_RenderLine(ren,px0,py0-1,px1,py1-1);
        SDL_RenderLine(ren,px0,py0+1,px1,py1+1);
    }
    float px,py;
    project(hx[hcount-1],hy[hcount-1],hz[hcount-1],&px,&py);
    SDL_SetRenderDrawColor(ren,255,255,255,255);
    SDL_RenderLine(ren,px-8,py,px+8,py);
    SDL_RenderLine(ren,px,py-8,px,py+8);
    SDL_SetRenderDrawColor(ren,r,g,b,255);
    SDL_FRect rc={px-4,py-4,8,8};
    SDL_RenderFillRect(ren,&rc);
}

void desenhar_contador_fugidas(SDL_Renderer*ren,Bolha*b,int num_bolhas){
    int fugidas=0;
    for(int i=0;i<num_bolhas;i++)if(b[i].r>R_FUGA)fugidas++;
    SDL_SetRenderDrawColor(ren,40,40,40,255);
    SDL_FRect bg={10,10,200,20};
    SDL_RenderFillRect(ren,&bg);
    float ratio=(float)fugidas/num_bolhas;
    if(ratio>1.0f)ratio=1.0f;
    SDL_SetRenderDrawColor(ren,255,50,50,255);
    SDL_FRect fill={10,10,200*ratio,20};
    SDL_RenderFillRect(ren,&fill);
    SDL_SetRenderDrawColor(ren,200,200,200,255);
    SDL_RenderRect(ren,&bg);
    for(int i=1;i<=4;i++){
        float x=10+200*i/5.0f;
        SDL_RenderLine(ren,x,10,x,30);
    }
}

void desenhar_eixos(SDL_Renderer*ren){
    float px_origem,py_origem;
    project(0,0,0,&px_origem,&py_origem);
    float comprimento=5.8f/zoom;
    float px,py;
    project(comprimento,0,0,&px,&py);
    SDL_SetRenderDrawColor(ren,255,0,0,255);
    SDL_RenderLine(ren,px_origem,py_origem,px,py);
    project(comprimento+0.4f,0,0,&px,&py);
    SDL_FRect r={px-3,py-3,6,6};
    SDL_RenderFillRect(ren,&r);
    project(0,comprimento,0,&px,&py);
    SDL_SetRenderDrawColor(ren,0,255,0,255);
    SDL_RenderLine(ren,px_origem,py_origem,px,py);
    project(0,comprimento+0.4f,0,&px,&py);
    r=(SDL_FRect){px-3,py-3,6,6};
    SDL_RenderFillRect(ren,&r);
    project(0,0,comprimento,&px,&py);
    SDL_SetRenderDrawColor(ren,0,0,255,255);
    SDL_RenderLine(ren,px_origem,py_origem,px,py);
    project(0,0,comprimento+0.4f,&px,&py);
    r=(SDL_FRect){px-3,py-3,6,6};
    SDL_RenderFillRect(ren,&r);
    SDL_SetRenderDrawColor(ren,255,255,255,255);
    project(0,0,0,&px,&py);
    r=(SDL_FRect){px-3,py-3,6,6};
    SDL_RenderFillRect(ren,&r);
}

static SDL_Vertex*g_pts_vertices=NULL;
static int*g_pts_indices=NULL;
static int g_pts_capacity=0;

void desenhar_pontos_bolhas(SDL_Renderer*ren,Bolha*b,int num_bolhas,float raio_maximo){
    if(!mostrar_bolhas)return;
    int max_points=num_bolhas*MAX_PONTOS;
    if(max_points>g_pts_capacity){
        g_pts_vertices=(SDL_Vertex*)realloc(g_pts_vertices,sizeof(SDL_Vertex)*4*max_points);
        g_pts_indices=(int*)realloc(g_pts_indices,sizeof(int)*6*max_points);
        g_pts_capacity=max_points;
    }
    int vcount=0;
    int icount=0;
    for(int i=0;i<num_bolhas;i++){
        if(b[i].r<0.00005f||b[i].r>raio_maximo)continue;
        if(mostrar_somente_caciques&&(i%128!=0))continue;
        SDL_FColor c;
        float size;
        int step=1;
        float ox=0.0f,oy=0.0f;
        if(i%128==0){
            c=(SDL_FColor){1.0f,50.0f/255.0f,50.0f/255.0f,1.0f};
            size=2.0f;
        }else if(b[i].tipo==1){
            c=(SDL_FColor){1.0f,140.0f/255.0f,0.0f,1.0f};
            size=2.2f;
            step=2;
            ox=-1.0f;oy=-1.0f;
        }else{
            if(b[i].timeout>0)c=(SDL_FColor){1.0f,1.0f,100.0f/255.0f,1.0f};
            else c=(SDL_FColor){100.0f/255.0f,160.0f/255.0f,1.0f,1.0f};
            size=1.0f;
        }
        float r_vis=(b[i].r<0.15f)?0.15f:b[i].r;
        for(int p=0;p<MAX_PONTOS;p+=step){
            float px,py;
            project(b[i].cx+r_vis*b[i].pontos[p].dx,
                    b[i].cy+r_vis*b[i].pontos[p].dy,
                    b[i].cz+r_vis*b[i].pontos[p].dz,&px,&py);
            float x0=px+ox;
            float y0=py+oy;
            float x1=x0+size;
            float y1=y0+size;
            g_pts_vertices[vcount+0].position=(SDL_FPoint){x0,y0};
            g_pts_vertices[vcount+1].position=(SDL_FPoint){x1,y0};
            g_pts_vertices[vcount+2].position=(SDL_FPoint){x1,y1};
            g_pts_vertices[vcount+3].position=(SDL_FPoint){x0,y1};
            for(int k=0;k<4;k++){
                g_pts_vertices[vcount+k].color=c;
                g_pts_vertices[vcount+k].tex_coord=(SDL_FPoint){0.0f,0.0f};
            }
            g_pts_indices[icount+0]=vcount+0;
            g_pts_indices[icount+1]=vcount+1;
            g_pts_indices[icount+2]=vcount+2;
            g_pts_indices[icount+3]=vcount+0;
            g_pts_indices[icount+4]=vcount+2;
            g_pts_indices[icount+5]=vcount+3;
            vcount+=4;
            icount+=6;
        }
    }
    if(icount>0){
        SDL_RenderGeometry(ren,NULL,g_pts_vertices,vcount,g_pts_indices,icount);
    }
}

static void gerar_ms_fibonacci(int i,int n_pares,
                                float*mx,float*my,float*mz,
                                float*ax,float*ay,float*az){
    float golden=(1.0f+sqrtf(5.0f))/2.0f;
    float y=1.0f-(2.0f*i+1.0f)/(float)n_pares;
    float theta=2.0f*(float)M_PI*i/golden;
    float r_proj=sqrtf(1.0f-y*y);
    *mx=r_proj*cosf(theta);
    *my=r_proj*sinf(theta);
    *mz=y;
    float nx,ny,nz;
    if(fabsf(*mz)<0.9f){nx=0.0f;ny=0.0f;nz=1.0f;}
    else{nx=1.0f;ny=0.0f;nz=0.0f;}
    float ux=*my*nz-*mz*ny;
    float uy=*mz*nx-*mx*nz;
    float uz=*mx*ny-*my*nx;
    normalizar3D(&ux,&uy,&uz);
    float vx=*my*uz-*mz*uy;
    float vy=*mz*ux-*mx*uz;
    float vz=*mx*uy-*my*ux;
    float phi=2.0f*(float)M_PI*i*golden;
    float sx=ux*cosf(phi)+vx*sinf(phi);
    float sy=uy*cosf(phi)+vy*sinf(phi);
    float sz=uz*cosf(phi)+vz*sinf(phi);
    *ax=sy**mz-sz**my;
    *ay=sz**mx-sx**mz;
    *az=sx**my-sy**mx;
}

void cenario2(Bolha*b,int*num_bolhas){
    *num_bolhas=MAX_BOLHAS;
    int n_pares=MAX_BOLHAS/2;
    for(int i=0;i<n_pares;i++){
        float mx,my,mz,ax,ay,az;
        gerar_ms_fibonacci(i,n_pares,&mx,&my,&mz,&ax,&ay,&az);
        srand((unsigned)(i+1));
        float cx1,cy1,cz1,r_pos1;
        do{
            cx1=((float)rand()/(float)RAND_MAX)*6.0f-3.0f;
            cy1=((float)rand()/(float)RAND_MAX)*6.0f-3.0f;
            cz1=((float)rand()/(float)RAND_MAX)*6.0f-3.0f;
            r_pos1=sqrtf(cx1*cx1+cy1*cy1+cz1*cz1);
        }while(r_pos1>3.0f);
        float r1=0.1f+((float)rand()/(float)RAND_MAX)*0.4f;
        inicializar_bolha(&b[i],cx1,cy1,cz1,r1,mx,my,mz,ax,ay,az,0);
        srand((unsigned)(i+1+n_pares));
        float cx2,cy2,cz2,r_pos2;
        do{
            cx2=((float)rand()/(float)RAND_MAX)*6.0f-3.0f;
            cy2=((float)rand()/(float)RAND_MAX)*6.0f-3.0f;
            cz2=((float)rand()/(float)RAND_MAX)*6.0f-3.0f;
            r_pos2=sqrtf(cx2*cx2+cy2*cy2+cz2*cz2);
        }while(r_pos2>3.0f);
        float r2=0.1f+((float)rand()/(float)RAND_MAX)*0.4f;
        inicializar_bolha(&b[i+n_pares],cx2,cy2,cz2,r2,-mx,-my,-mz,ax,ay,az,0);
    }
    b[0].cx=-3.0f;b[0].cy=0.0f;b[0].cz=0.0f;
    if(MAX_BOLHAS>=129){
        b[128].cx=3.0f;b[128].cy=0.0f;b[128].cz=0.0f;
    }
}

void inicializar_cena(Bolha*b,int*num_bolhas){
    cenario2(b,num_bolhas);
}

int SDL_main(int argc,char*argv[]){
    AllocConsole();
    freopen("CONOUT$","w",stdout);
    freopen("CONOUT$","w",stderr);
    if(!SDL_Init(SDL_INIT_VIDEO)){
        printf("Erro ao inicializar SDL\n");
        return 1;
    }
    SDL_Window*win=SDL_CreateWindow("Toy Universe 3D - Bolhas - REV",window_width,window_height,0);
    SDL_Renderer*ren=SDL_CreateRenderer(win,"direct3d12");
    if(!ren){
        printf("D3D12 nao disponivel, tentando renderer padrao.\n");
        ren=SDL_CreateRenderer(win,NULL);
    }
    if(!ren){
        printf("Erro ao criar renderer: %s\n",SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    Bolha*b=(Bolha*)calloc(MAX_BOLHAS,sizeof(Bolha));
    if(!b){
        printf("Erro ao alocar memoria\n");
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    int num_bolhas;
    inicializar_cena(b,&num_bolhas);
    printf("\n=== DEBUG ===\n");
    int count_p=0;
    for(int i=0;i<num_bolhas;i++){
        if(b[i].tipo==1){
            count_p++;
            printf("P[%d] pos=(%.1f,%.1f,%.1f) r=%.2f\n",i,b[i].cx,b[i].cy,b[i].cz,b[i].r);
        }
    }
    printf("Total: %d | P: %d\n\n",num_bolhas,count_p);
    fflush(stdout);
    uint64_t last_time=SDL_GetTicks();
    int rodando=1;
    SDL_Event e;
    Interacao*hits=NULL;
    int hits_cap=0;
    int fase_expansao=FASE_EXPANSAO_FRAMES;
    int frame_count=0;
    int evoluir=1;
    while(rodando){
        uint64_t current_time=SDL_GetTicks();
        float dt=(current_time-last_time)/1000.0f;
        if(dt>0.1f)dt=0.1f;
        last_time=current_time;
        while(SDL_PollEvent(&e)){
            if(e.type==SDL_EVENT_QUIT)rodando=0;
            if(e.type==SDL_EVENT_KEY_DOWN){
                if(e.key.scancode==SDL_SCANCODE_X)vista_atual=VISTA_XY;
                if(e.key.scancode==SDL_SCANCODE_Y)vista_atual=VISTA_YZ;
                if(e.key.scancode==SDL_SCANCODE_Z)vista_atual=VISTA_XZ;
                if(e.key.scancode==SDL_SCANCODE_I)vista_atual=VISTA_ISO;
                if(e.key.scancode==SDL_SCANCODE_M){
                    zoom_out_ativado=!zoom_out_ativado;
                    zoom=zoom_out_ativado?0.25f:1.0f;
                }
                if(e.key.scancode==SDL_SCANCODE_ESCAPE)rodando=0;
                if(e.key.scancode==SDL_SCANCODE_V)mostrar_bolhas=!mostrar_bolhas;
                if(e.key.scancode==SDL_SCANCODE_C)modo_somente_cm=!modo_somente_cm;
                if(e.key.scancode==SDL_SCANCODE_K)mostrar_somente_caciques=!mostrar_somente_caciques;
                if(e.key.scancode==SDL_SCANCODE_LEFT)pan_x-=pan_speed/zoom;
                if(e.key.scancode==SDL_SCANCODE_RIGHT)pan_x+=pan_speed/zoom;
                if(e.key.scancode==SDL_SCANCODE_UP)pan_y+=pan_speed/zoom;
                if(e.key.scancode==SDL_SCANCODE_DOWN)pan_y-=pan_speed/zoom;
                if(e.key.scancode==SDL_SCANCODE_PAGEUP)pan_z+=pan_speed/zoom;
                if(e.key.scancode==SDL_SCANCODE_PAGEDOWN)pan_z-=pan_speed/zoom;
            }
            if(e.type==SDL_EVENT_MOUSE_WHEEL){
                if(e.wheel.y>0)zoom*=1.1f;
                else if(e.wheel.y<0)zoom*=0.9f;
                if(zoom<0.05f)zoom=0.05f;
                if(zoom>5.0f)zoom=5.0f;
            }
        }
        if(evoluir){
            for(int i=0;i<num_bolhas;i++){
                b[i].r+=0.5f*dt;
            }
            if(fase_expansao>0){
                fase_expansao--;
            }else{
                int frame=frame_count++;
                int centro=((num_bolhas-1)+frame*2)%num_bolhas;
                int max_hits=num_bolhas/2;
                if(max_hits<1)max_hits=1;
                if(hits_cap<max_hits){
                    hits=(Interacao*)realloc(hits,max_hits*sizeof(Interacao));
                    hits_cap=max_hits;
                }
                int num_hits=0;
                for(int i=0;i<num_bolhas;i++){
                    int j=(centro-i+num_bolhas)%num_bolhas;
                    if(i>=j)continue;
                    if(b[i].tipo==1&&b[j].tipo==1)continue;
                    Interacao h;
                    if(detectar_interacao(&b[i],&b[j],i,j,b,num_bolhas,&h)){
                        hits[num_hits++]=h;
                    }else if(detectar_interacao(&b[j],&b[i],j,i,b,num_bolhas,&h)){
                        hits[num_hits++]=h;
                    }
                }
                int acertou[MAX_BOLHAS]={0};
                for(int k=0;k<num_hits;k++){
                    aplicar_interacao(&hits[k],b,num_bolhas);
                    if(hits[k].a%128==0)acertou[hits[k].a]=1;
                    if(hits[k].b%128==0)acertou[hits[k].b]=1;
                }
                if((frame%(MAX_BOLHAS/2))==0){
                    for(int i=0;i<num_bolhas;i++){
                        if((i%128==0)&&!acertou[i]){
                            // Cacique ocioso: apenas gira o modo_m,
                            // NAO reemite (preserva raio e posicao)
                            b[i].modo_m=(b[i].modo_m+1)%4;
                        }
                    }
                }
            }
        }
        if(num_bolhas>0)
            gravar_cacique(b,0,&hist_c0x,&hist_c0y,&hist_c0z,&hist_c0_count,&hist_c0_cap);
        if(num_bolhas>128)
            gravar_cacique(b,128,&hist_c1x,&hist_c1y,&hist_c1z,&hist_c1_count,&hist_c1_cap);
        SDL_SetRenderDrawColor(ren,15,15,15,255);
        SDL_RenderClear(ren);
        if(!modo_somente_cm){
            desenhar_contador_fugidas(ren,b,num_bolhas);
            desenhar_eixos(ren);
            float raio_maximo=2.0f*(window_height/zoom);
            desenhar_pontos_bolhas(ren,b,num_bolhas,raio_maximo);
            for(int i=0;i<num_bolhas;i++){
                if(b[i].r<0.05f)continue;
                if(b[i].r>raio_maximo)continue;
                if(mostrar_somente_caciques&&(i%128!=0))continue;
                float tamanho_seta=mostrar_bolhas?b[i].r:((1.0f/zoom)*0.25f);
                desenhar_seta(ren,b[i].cx,b[i].cy,b[i].cz,b[i].mx,b[i].my,b[i].mz,tamanho_seta,120,120,40);
                if(b[i].tipo==1){
                    desenhar_seta(ren,b[i].cx,b[i].cy,b[i].cz,b[i].sx,b[i].sy,b[i].sz,tamanho_seta,255,140,0);
                    if(!mostrar_bolhas){
                        desenhar_circulo(ren,b[i].cx,b[i].cy,b[i].cz,tamanho_seta*0.5f,255,140,0);
                    }
                }else{
                    desenhar_seta(ren,b[i].cx,b[i].cy,b[i].cz,b[i].sx,b[i].sy,b[i].sz,tamanho_seta,0,255,255);
                }
                if(!mostrar_bolhas&&i%128==0){
                    desenhar_circulo(ren,b[i].cx,b[i].cy,b[i].cz,tamanho_seta*0.5f,255,0,0);
                }
            }
        }
        if(mostrar_somente_caciques){
            desenhar_trajetoria_cacique(ren,hist_c0x,hist_c0y,hist_c0z,hist_c0_count,255,200,50);
            desenhar_trajetoria_cacique(ren,hist_c1x,hist_c1y,hist_c1z,hist_c1_count,50,200,255);
        }
        desenhar_trajetoria_cm(ren,b,num_bolhas);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    free(hits);
    free(g_pts_vertices);
    free(g_pts_indices);
    free(b);
    free(hist_cmx);free(hist_cmy);free(hist_cmz);
    free(hist_c0x);free(hist_c0y);free(hist_c0z);
    free(hist_c1x);free(hist_c1y);free(hist_c1z);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}