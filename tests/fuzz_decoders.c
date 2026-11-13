/* PARCHE 029: fuzzer de los decodificadores de imagen y de descompresion (JPEG base y progresivo, PNG, GIF, BMP, inflate).
 * Mutacion determinista (semilla = argv[2]); se compila con ASAN+UBSAN: cualquier lectura/escritura fuera de rango o
 * desbordamiento de enteros detiene la ejecucion.   Uso:  make fuzz     (genera semillas con tests/fuzz_seeds.py, necesita Pillow) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "web_img.h"
#include "web_inflate.h"
#include "jpeg.h"
void *kmalloc(size_t n){ return malloc(n?n:1); }
void kfree(void*p){ free(p); }
void kprint(const char*s){(void)s;}
void jpeg_yield_hook(void){}
static uint64_t S=88172645463325252ULL;
static uint32_t rnd(void){ S^=S<<13; S^=S>>7; S^=S<<17; return (uint32_t)(S>>11); }
static void run(const uint8_t *d, uint32_t n, int mode){
  if(mode==0){ struct wimg o; if(wi_decode(d,n,&o)==0) wi_free(&o);
    int m=(int)(rnd()%4); static const int mm[4]={1,9,20,40}; if(wi_decode_fit(d,n,&o,mm[m],mm[m])==0) wi_free(&o); }   /* PARCHE 086: decodificacion reducida */
  else { struct wb out; memset(&out,0,sizeof out); wz_inflate(d,n,3,&out,8u<<20); wb_free(&out); }
}
int main(int argc,char**argv){
  long iters=atol(argv[1]); S^=(uint64_t)atol(argv[2])*0x9E3779B97F4A7C15ULL;
  for(int a=3;a<argc;a++){
    FILE*f=fopen(argv[a],"rb"); if(!f) continue; static uint8_t seed[1<<16]; uint32_t sn=(uint32_t)fread(seed,1,sizeof seed,f); fclose(f);
    int mode = strstr(argv[a],".z")?1:0;
    uint8_t *buf=malloc(sn+64);
    for(long it=0;it<iters;it++){
      uint32_t n=sn; memcpy(buf,seed,n);
      int m=1+rnd()%6;
      while(m--){
        switch(rnd()%6){
          case 0: buf[rnd()%n]^=1u<<(rnd()%8); break;
          case 1: buf[rnd()%n]=(uint8_t)rnd(); break;
          case 2: { uint32_t p=rnd()%n; buf[p]=(uint8_t[]){0,1,0x7f,0x80,0xff}[rnd()%5]; } break;
          case 3: if(n>16) n=8+rnd()%(n-8); break;
          case 4: { uint32_t p=rnd()%n, l=1+rnd()%16; if(p+l<n) memmove(buf+p,buf+p+l,n-p-l), n-=l; } break;
          case 5: { uint32_t p=rnd()%(n>4?n-4:1); buf[p]=0xFF; buf[p+1]=(uint8_t)rnd(); } break;
        }
      }
      uint8_t *c=malloc(n?n:1); memcpy(c,buf,n);   /* copia exacta: ASAN detecta lecturas pasadas del final */
      run(c,n,mode); free(c);
    }
    free(buf); fprintf(stderr,"ok %s\n",argv[a]);
  }
  return 0;
}
uint64_t web_now_ms(void){ return 0; }
void web_yield(void){}
