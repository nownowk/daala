
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "switch_decision.h"

/* Warning, this will fail for images larger than 2024 x 2024 */
#define MAX_VAR_BLOCKS 1024
#define SQUARE(x) ((int)(x)*(int)(x))

#define CG4 (8.6*2/6)
#define CG8 (9.57*2/6)
#define CG16 (9.81*2/6)
#define CG32 (9.93*2/6)

#define OFF8   (2)
#define OFF16  (4)
#define OFF32  (4)

#define COUNT8   (3+2*OFF8)
#define COUNT16  (7+2*OFF16)
#define COUNT32 (15+2*OFF32)

void od_switch_init(od_switch_decision_state *st, int w)
{
  st->vstride = (w/2)+32;
  /* Need 32 microblock lines, plus space for one superblock on each side */
  st->var = malloc(32*st->vstride*sizeof(st->var[0]));
  st->var_1 = malloc(32*st->vstride*sizeof(st->var_1[0]));
}

void od_switch_free(od_switch_decision_state *st)
{
  free(st->var);
  free(st->var_1);
}

void od_switch_process_superblock(od_switch_decision_state *st, const unsigned char *img, int id, int s, int blocksize[4][4])
{
  int i, j;
  int vs;
  int voffset;
  int ioffset;
  ogg_int32_t (*Sxx)[17], (*Sx)[17];
  ogg_int16_t *var, *var_1;
  vs = st->vstride;

  Sxx = st->Sxx;
  Sx = st->Sx;
  /* Compute variances needed for this superblock */
  ioffset = 16*s+16;
  for(i=0;i<17;i++){
    for(j=0;j<17;j++){
      const unsigned char *x = img+ioffset+2*i*s+2*j;
      st->Sxx[i][j]=SQUARE(x[0])+SQUARE(x[1])+SQUARE(x[s])+SQUARE(x[s+1]);
      st->Sx[i][j]=x[0]+x[1]+x[s]+x[s+1];
    }
  }

  /* Here we assume that var is calculated up to the middle of the superblock because of lapping with the previous superblocks */
  voffset = 8*vs + (id+1)*16;
  for(i=0;i<16;i++){
    for(j=0;j<16;j++){
      ogg_int32_t sum_x;
      ogg_int32_t sum_xx;
      ogg_int16_t var_floor;
      sum_x=Sx[i][j]+Sx[i][j+1]+Sx[i+1][j]+Sx[i+1][j+1];
      sum_xx=Sxx[i][j]+Sxx[i][j+1]+Sxx[i+1][j]+Sxx[i+1][j+1];
      var[voffset+i*vs+j]=(sum_xx-(SQUARE(sum_x)>>4))>>5;
      var_floor = 4+(sum_x>>6);
      if (var[voffset+i*vs+j]<var_floor)var[voffset+i*vs+j]=var_floor;
      /*var_1[i][j] = 16384/var[i][j];*/
      st->var[voffset+i*vs+j] = 0;
    }
  }
}


#define PSY_LAMBDA 1.0

int switch_decision(unsigned char *img, int w, int h, int stride)
{
  int i,j;
  int h4, w4,h8,w8,h16,w16,h32,w32;
  static int Sx[MAX_VAR_BLOCKS][MAX_VAR_BLOCKS];
  static int Sxx[MAX_VAR_BLOCKS][MAX_VAR_BLOCKS];
  static int var[MAX_VAR_BLOCKS][MAX_VAR_BLOCKS];
  static int var_1[MAX_VAR_BLOCKS][MAX_VAR_BLOCKS];
  static float nmr4[MAX_VAR_BLOCKS>>1][MAX_VAR_BLOCKS>>1];
  static float nmr8[MAX_VAR_BLOCKS>>2][MAX_VAR_BLOCKS>>2];
  static float cg8[MAX_VAR_BLOCKS>>2][MAX_VAR_BLOCKS>>2];
  static float nmr16[MAX_VAR_BLOCKS>>3][MAX_VAR_BLOCKS>>3];
  static float cg16[MAX_VAR_BLOCKS>>3][MAX_VAR_BLOCKS>>3];
  static float nmr32[MAX_VAR_BLOCKS>>4][MAX_VAR_BLOCKS>>4];
  static float cg32[MAX_VAR_BLOCKS>>4][MAX_VAR_BLOCKS>>4];

  static int dec8[MAX_VAR_BLOCKS>>2][MAX_VAR_BLOCKS>>2];
  const unsigned char *x;
  w>>=1;
  h>>=1;
  w4 = w>>1;
  h4 = h>>1;
  w8 = w>>2;
  h8 = h>>2;
  w16 = w>>3;
  h16 = h>>3;
  w32 = w>>4;
  h32 = h>>4;
  x = img;
  for(i=0;i<h;i++){
    for(j=0;j<w;j++){
      Sx[i][j]=x[2*j]+x[2*j+1]+x[stride+2*j]+x[stride+2*j+1];
      Sxx[i][j]=SQUARE(x[2*j])+SQUARE(x[2*j+1])+SQUARE(x[stride+2*j])+SQUARE(x[stride+2*j+1]);
    }
    x+=2*stride;
  }

  for(i=0;i<h-1;i++){
    for(j=0;j<w-1;j++){
      int sum_x;
      int sum_xx;
      int var_floor;
      sum_x=Sx[i][j]+Sx[i][j+1]+Sx[i+1][j]+Sx[i+1][j+1];
      sum_xx=Sxx[i][j]+Sxx[i][j+1]+Sxx[i+1][j]+Sxx[i+1][j+1];
      var[i][j]=(sum_xx-(SQUARE(sum_x)>>4))>>5;
      var_floor = 4+(sum_x>>8);
      if (var[i][j]<var_floor)var[i][j]=var_floor;
      /*printf("%d ", var[i][j]);*/
      var_1[i][j] = 16384/var[i][j];
    }
    /*printf("\n");*/
  }

  for(i=1;i<h4-1;i++){
    for(j=1;j<w4-1;j++){
      int k,m;
      int sum_var=0;
      int sum_var_1=0;
      float psy=0;
      float noise;
      for(k=0;k<3;k++){
        for(m=0;m<3;m++){
          sum_var+=var[2*i-1+k][2*j-1+m];
        }
      }
      noise = sum_var/(3*3);
      for(k=0;k<3;k++){
        for(m=0;m<3;m++){
          psy += log2(1+noise*var_1[2*i-1+k][2*j-1+m]/16384.);
        }
      }
      psy /= (3*3);
      nmr4[i][j] = psy;
      /*printf("%f ", nmr4[i][j]);*/
    }
    /*printf("\n");*/
  }

  for(i=1;i<h8-1;i++){
    for(j=1;j<w8-1;j++){
      int k,m;
      int sum_var=0;
      int sum_var_1=0;
      float nmr4_avg;
      float cgl, cgs;
      float noise;
      float psy;
      for(k=0;k<COUNT8;k++){
        for(m=0;m<COUNT8;m++){
          sum_var  +=var[4*i-OFF8+k][4*j-OFF8+m];
        }
      }
      noise = sum_var/(COUNT8*COUNT8);
      psy=0;
      for(k=0;k<COUNT8;k++){
        for(m=0;m<COUNT8;m++){
          psy += log2(1+noise*var_1[4*i-OFF8+k][4*j-OFF8+m]/16384.);
        }
      }
      psy /= (COUNT8*COUNT8);
      nmr8[i][j] = psy;
      nmr4_avg = .25f*(nmr4[2*i][2*j]+nmr4[2*i][2*j+1]+nmr4[2*i+1][2*j]+nmr4[2*i+1][2*j+1]);
      cgs = CG4 - PSY_LAMBDA*(nmr4_avg);
      cgl = CG8 - PSY_LAMBDA*(nmr8[i][j]);
      if (cgl>=cgs)
      {
        dec8[i][j] = 1;
        cg8[i][j] = CG8;
      } else {
        nmr8[i][j] = nmr4_avg;
        dec8[i][j] = 0;
        cg8[i][j] = CG4;
      }
      /*printf("%d ", dec8[i][j]);*/
    }
    /*printf("\n");*/
  }

  for(i=1;i<h16-1;i++){
    for(j=1;j<w16-1;j++){
      int k,m;
      int sum_var=0;
      int sum_var_1=0;
      float nmr8_avg;
      float cgl,cgs;
      float noise;
      float psy;
      for(k=0;k<COUNT16;k++){
        for(m=0;m<COUNT16;m++){
          sum_var+=var[8*i-OFF16+k][8*j-OFF16+m];
        }
      }
      noise = sum_var/(float)(COUNT16*COUNT16);
      psy=0;
      for(k=0;k<COUNT16;k++){
        for(m=0;m<COUNT16;m++){
          psy += log2(1+noise*var_1[8*i-OFF16+k][8*j-OFF16+m]/16384.);
        }
      }
      psy /= (COUNT16*COUNT16);
      nmr16[i][j] = psy;
      nmr8_avg = .25f*(nmr8[2*i][2*j]+nmr8[2*i][2*j+1]+nmr8[2*i+1][2*j]+nmr8[2*i+1][2*j+1]);
      cg16[i][j] = .25*(cg8[2*i][2*j] + cg8[2*i][2*j+1] + cg8[2*i+1][2*j] + cg8[2*i+1][2*j+1]);
      cgs = cg16[i][j] - PSY_LAMBDA*(nmr8_avg);
      cgl = CG16 - PSY_LAMBDA*(nmr16[i][j]);
      /*printf("%f ", psy);*/
      if (cgl>=cgs)
      {
        dec8[2*i][2*j] = 2;
        dec8[2*i][2*j+1] = 2;
        dec8[2*i+1][2*j] = 2;
        dec8[2*i+1][2*j+1] = 2;
        cg16[i][j] = CG16;
      } else {
        nmr16[i][j] = nmr8_avg;
      }
    }
    /*printf("\n");*/
  }

#if 1
  for(i=1;i<h32-1;i++){
    for(j=1;j<w32-1;j++){
      int k,m;
      int sum_var=0;
      int sum_var_1=0;
      float nmr16_avg;
      float cgl,cgs;
      float noise, psy;
      for(k=0;k<COUNT32;k++){
        for(m=0;m<COUNT32;m++){
          sum_var  +=var[16*i-OFF32+k][16*j-OFF32+m];
        }
      }
      noise = sum_var/(float)(COUNT32*COUNT32);
      psy=0;
      for(k=0;k<COUNT32;k++){
        for(m=0;m<COUNT32;m++){
          psy += log2(1.+noise*var_1[16*i-OFF32+k][16*j-OFF32+m]/16384.);
        }
      }
      psy /= (COUNT32*COUNT32);
      nmr32[i][j] = psy;
      nmr16_avg = .25f*(nmr16[2*i][2*j]+nmr16[2*i][2*j+1]+nmr16[2*i+1][2*j]+nmr16[2*i+1][2*j+1]);
      cg32[i][j] = .25*(cg16[2*i][2*j] + cg16[2*i][2*j+1] + cg16[2*i+1][2*j] + cg16[2*i+1][2*j+1]);
      cgs = cg32[i][j] - PSY_LAMBDA*(nmr16_avg);
      cgl = CG32 - PSY_LAMBDA*(nmr32[i][j]);
      /*printf("%f ", nmr32[i][j]);*/
      if (cgl>=cgs)
      {
        for(k=0;k<4;k++){
          for(m=0;m<4;m++){
            dec8[4*i+k][4*j+m]=3;
          }
        }
        cg32[i][j] = CG32;
      } else {
        nmr32[i][j] = nmr16_avg;
      }
    }
    /*printf("\n");*/
  }
#endif
#if 1
  for(i=4;i<h8-4;i++){
    for(j=4;j<w8-4;j++){
      printf("%d ", dec8[i][j]);
    }
    printf("\n");
  }
#endif
#if 0
  fprintf(stderr, "size : %dx%d\n", (w<<1), (h<<1));
  for(i=0;i<(h<<1);i++){
    for(j=0;j<1296;j++){
      putc(dec8[i>>3][j>>3], stdout);
    }
  }
#endif
#if 1
  for(i=4;i<h8-4;i++){
    for(j=4;j<w8-4;j++){
      if ((i&3)==0 && (j&3)==0){
        int k;
        for(k=0;k<32;k++)
          img[i*stride*8+j*8+k] = 0;
        for(k=0;k<32;k++)
          img[(8*i+k)*stride+j*8] = 0;
      }
      if ((i&1)==0 && (j&1)==0 && dec8[i][j]==2){
        int k;
        for(k=0;k<16;k++)
          img[i*stride*8+j*8+k] = 0;
        for(k=0;k<16;k++)
          img[(8*i+k)*stride+j*8] = 0;
      }
      if (dec8[i][j]<=1){
        int k;
        for(k=0;k<8;k++)
          img[i*stride*8+j*8+k] = 0;
        for(k=0;k<8;k++)
          img[(8*i+k)*stride+j*8] = 0;
        if (dec8[i][j]==0){
          img[(8*i+3)*stride+j*8+3] = 0;
          img[(8*i+3)*stride+j*8+4] = 0;
          img[(8*i+4)*stride+j*8+3] = 0;
          img[(8*i+4)*stride+j*8+4] = 0;
        }
      }
    }
  }
  for (i=32;i<(w32-1)*32;i++)
    img[(h32-1)*32*stride+i]=0;
  for (i=32;i<(h32-1)*32;i++)
    img[i*stride+(w32-1)*32]=0;
#endif
  return 0;
}
