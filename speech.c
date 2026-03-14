#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define RATE 16000
#define AMP 20000
#define PI 3.1415926535
#define FORMANTS 5

typedef struct {
    float freq[FORMANTS];
    float bw[FORMANTS];
} Vowel;

/* ---------------- Formant Database ---------------- */

Vowel vowel_table(char c)
{
    switch(c)
    {
        case 'a': return (Vowel){{800,1200,2500,3500,4500},{80,90,120,200,300}};
        case 'e': return (Vowel){{500,1900,2600,3500,4500},{70,100,120,200,300}};
        case 'i': return (Vowel){{300,2200,3000,3700,4900},{60,100,120,200,300}};
        case 'o': return (Vowel){{500,900,2400,3500,4500},{70,90,120,200,300}};
        case 'u': return (Vowel){{350,800,2200,3000,4000},{60,80,120,200,300}};
        default:  return (Vowel){{600,1400,2600,3500,4500},{80,90,120,200,300}};
    }
}

/* ---------------- Resonator ---------------- */

typedef struct {
    float a,b,c;
    float y1,y2;
} Resonator;

void resonator_init(Resonator *r,float freq,float bw)
{
    float rcoef = exp(-PI*bw/RATE);
    r->c = -rcoef*rcoef;
    r->b = 2*rcoef*cos(2*PI*freq/RATE);
    r->a = 1-r->b-r->c;
    r->y1 = r->y2 = 0;
}

float resonator_process(Resonator *r,float x)
{
    float y = r->a*x + r->b*r->y1 + r->c*r->y2;
    r->y2 = r->y1;
    r->y1 = y;
    return y;
}

/* ---------------- Glottal Source ---------------- */

float glottal(int t,float pitch)
{
    float time=(float)t/RATE;
    float phase=fmod(time*pitch,1.0);
    return (phase<0.4) ? phase/0.4 : exp(-(phase-0.4)*8);
}

/* ---------------- Noise ---------------- */

float noise()
{
    return ((float)rand()/RAND_MAX)*2-1;
}

/* ---------------- Vowel Synth ---------------- */

void synth_vowel(FILE *f,char c,int *bytes)
{
    Vowel v=vowel_table(c);
    Resonator r[FORMANTS];

    for(int i=0;i<FORMANTS;i++)
        resonator_init(&r[i],v.freq[i],v.bw[i]);

    int duration=2400;
    float pitch=120;

    for(int t=0;t<duration;t++)
    {
        float source=glottal(t,pitch);
        float out=0;

        for(int i=0;i<FORMANTS;i++)
            out+=resonator_process(&r[i],source);

        float env=1;
        if(t<200) env=t/200.0;
        if(t>duration-200) env=(duration-t)/200.0;

        int16_t s=AMP*out*env;
        fwrite(&s,2,1,f);
        *bytes+=2;
    }
}

/* ---------------- Fricatives ---------------- */

void synth_fricative(FILE *f,int *bytes)
{
    for(int t=0;t<1400;t++)
    {
        float s=noise()*0.6;
        int16_t sample=AMP*s;
        fwrite(&sample,2,1,f);
        *bytes+=2;
    }
}

/* ---------------- Plosives ---------------- */

void synth_plosive(FILE *f,int *bytes)
{
    for(int t=0;t<700;t++)
    {
        float s=noise()*exp(-t/60.0);
        int16_t sample=AMP*s;
        fwrite(&sample,2,1,f);
        *bytes+=2;
    }
}

/* ---------------- Character Mapping ---------------- */

void synth_char(FILE *f,char c,int *bytes)
{
    c=tolower(c);

    if(strchr("aeiou",c))
        synth_vowel(f,c,bytes);

    else if(strchr("fstvz",c))
        synth_fricative(f,bytes);

    else if(strchr("ptkbdg",c))
        synth_plosive(f,bytes);

    else if(c==' ')
    {
        int16_t z=0;
        for(int i=0;i<1000;i++)
        {
            fwrite(&z,2,1,f);
            *bytes+=2;
        }
    }
}
