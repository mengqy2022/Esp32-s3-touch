#include "game2048.h"
#include <stdlib.h>
#include <string.h>
static void add_tile(game2048_t *g){int e[16],n=0; for(int y=0;y<4;y++)for(int x=0;x<4;x++)if(!g->tile[y][x])e[n++]=y*4+x; if(n){int p=e[rand()%n]; g->tile[p/4][p%4]=(rand()%10==0)?4:2;}}
static void transpose(game2048_t*g){for(int y=0;y<4;y++)for(int x=y+1;x<4;x++){int t=g->tile[y][x];g->tile[y][x]=g->tile[x][y];g->tile[x][y]=t;}}
static bool slide_line(int*a,int*score){int b[4]={0},n=0,chg=0;for(int i=0;i<4;i++)if(a[i])b[n++]=a[i];for(int i=0;i<3;i++)if(b[i]&&b[i]==b[i+1]){b[i]*=2;*score+=b[i];b[i+1]=0;}int c[4]={0},j=0;for(int i=0;i<4;i++)if(b[i])c[j++]=b[i];for(int i=0;i<4;i++){if(a[i]!=c[i])chg=1;a[i]=c[i];}return chg;}
void game2048_init(game2048_t*g){memset(g,0,sizeof(*g));g->score=0;add_tile(g);add_tile(g);}
bool game2048_move(game2048_t*g,int d){bool c=false;if(d==0||d==1){transpose(g);c=game2048_move(g,d==0?2:3);transpose(g);return c;} if(d==3){for(int y=0;y<4;y++){int t[4]={g->tile[y][3],g->tile[y][2],g->tile[y][1],g->tile[y][0]};if(slide_line(t,&g->score))c=1;for(int x=0;x<4;x++)g->tile[y][x]=t[3-x];}} else {for(int y=0;y<4;y++)if(slide_line(g->tile[y],&g->score))c=1;} if(c)add_tile(g);return c;}
