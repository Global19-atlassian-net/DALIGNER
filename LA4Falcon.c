/* vim: set et ts=2 sts=2 sw=2 : */
/************************************************************************************\
*                                                                                    *
* Copyright (c) 2014, Dr. Eugene W. Myers (EWM). All rights reserved.                *
*                                                                                    *
* Redistribution and use in source and binary forms, with or without modification,   *
* are permitted provided that the following conditions are met:                      *
*                                                                                    *
*  · Redistributions of source code must retain the above copyright notice, this     *
*    list of conditions and the following disclaimer.                                *
*                                                                                    *
*  · Redistributions in binary form must reproduce the above copyright notice, this  *
*    list of conditions and the following disclaimer in the documentation and/or     *
*    other materials provided with the distribution.                                 *
*                                                                                    *
*  · The name of EWM may not be used to endorse or promote products derived from     *
*    this software without specific prior written permission.                        *
*                                                                                    *
* THIS SOFTWARE IS PROVIDED BY EWM ”AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES,    *
* INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND       *
* FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL EWM BE LIABLE   *
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS  *
* OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY      *
* THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING     *
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN  *
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                                      *
*                                                                                    *
* For any issues regarding this software and its use, contact EWM at:                *
*                                                                                    *
*   Eugene W. Myers Jr.                                                              *
*   Bautzner Str. 122e                                                               *
*   01099 Dresden                                                                    *
*   GERMANY                                                                          *
*   Email: gene.myers@gmail.com                                                      *
*                                                                                    *
\************************************************************************************/

/*******************************************************************************************
 *
 *  Utility for displaying the overlaps in a .las file in a variety of ways including
 *    a minimal listing of intervals, a cartoon, and a full out alignment.
 *
 *  Author:    Gene Myers
 *  Creation:  July 2013
 *  Last Mod:  Jan 2015
 *
 *******************************************************************************************/

/*******************************************************************************************
 *
 *  Based on the original LAshow.c, this code is modified by Jason Chin to support generating
 *    consensus sequences from daligner output
 *
 *  Last Mod:  July 2015
 *
 *******************************************************************************************/
#include "DB.h"
#include "DBX.h"
#include "align.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// debugging
#include <time.h>
//#include <locale.h>
#include <stdbool.h>
// end debugging



#define MAX_OVERLAPS 50000

#define MIN(X,Y)  ((X) < (Y)) ? (X) : (Y)

static bool GROUP = false;

// Allows us to group overlaps between a pair of a/b reads as a unit, one per
// direction (if applicable).  beg/end will point to the same overlap when
// only one overlap found.
typedef struct {
    Overlap beg;
    Overlap end;
    int score;
    int blen;
} OverlapGroup;

OverlapGroup *ovlgrps;

static int compare_ovlgrps(const void *grp1, const void *grp2) {
    return ((OverlapGroup *)grp2)->score - ((OverlapGroup *)grp1)->score;
}

static bool belongs(OverlapGroup *grp, const Overlap *ovl) {
    Overlap *prev = &grp->end;
    return prev->flags == ovl->flags
        &&(ovl->path.abpos>prev->path.aepos)
        &&(ovl->path.bbpos>prev->path.bepos)
        &&(ovl->path.abpos-prev->path.aepos) < 251;
}

// Add a new overlap to a new or existing overlap group. Always adds when group
// flag is false, effectively greating groups of 1.
// Returns 1 if added as a new overlap group, otherwise 0.
// caller keeps track of count
static bool add_overlap(const Alignment *aln, const Overlap *ovl, const int count) {
    int added = false;
    // we assume breads are in order
    if (!GROUP || count < 0 || ovlgrps[count].beg.bread != ovl->bread) {
        // Haven't seen this bread yet (or we're not grouping), move to new overlap group
        OverlapGroup *next = &ovlgrps[count+1];
        next->beg = *ovl;
        next->end = *ovl;
        next->blen = aln->blen;
        const Path *p = &ovl->path;
        int olen = p->bepos - p->bbpos;
        int hlen = (MIN(p->abpos, p->bbpos)) +
                   (MIN(aln->alen - p->aepos,aln->blen - p->bepos));
        next->score = olen - hlen;
        added = true;
    } else {
        OverlapGroup *curr = &ovlgrps[count];
        // Seen, should we combine it with the previous overlap group or move
        // on to the next?
        if (belongs(curr, ovl)) {
            curr->end = *ovl;
            // rescore
            Overlap *beg = &curr->beg;
            Overlap *end = &curr->end;
            int olen = end->path.bepos - beg->path.bbpos;
            int hlen = (MIN(beg->path.abpos, beg->path.bbpos)) +
                       (MIN(aln->alen - end->path.aepos,aln->blen - end->path.bepos));
            curr->score = olen - hlen;
        } else {
            OverlapGroup *next = &ovlgrps[count+1];
            next->beg = *ovl;
            next->end = *ovl;
            next->blen = aln->blen;
            const Path *p = &ovl->path;
            int olen = p->bepos - p->bbpos;
            int hlen = (MIN(p->abpos, p->bbpos)) + (MIN(aln->alen - p->aepos,aln->blen - p->bepos));
            next->score = olen - hlen;
            added = true;
        }
    }
    return added;
}

static void print_hits(const int hit_count, DAZZ_DBX *dbx2, char *bbuffer, char buffer[], int64 bsize, int alen, const int MAX_HIT_COUNT, int WRITE_MAPPING_COORDS) {
    int tmp_idx;
    qsort(ovlgrps, (hit_count+1), sizeof(OverlapGroup), compare_ovlgrps);
    for (tmp_idx = 0; tmp_idx < (hit_count+1) && tmp_idx < MAX_HIT_COUNT; tmp_idx++) {
        OverlapGroup *grp = &ovlgrps[tmp_idx];
        //Load_ReadX assuming db2 == db1 is true
        Load_ReadX(dbx2, grp->end.bread, bbuffer, 0);
        if (COMP(grp->end.flags)) Complement_Seq(bbuffer, grp->blen );
        Upper_Read(bbuffer);
        int64 const rlen = (int64)(grp->end.path.bepos) - (int64)(grp->beg.path.bbpos);
        if (rlen < bsize) {
            strncpy( buffer, bbuffer + grp->beg.path.bbpos, rlen );
            buffer[rlen] = '\0';

            if (WRITE_MAPPING_COORDS) {
                // The sequence is clipped.
                int bbpos = 0;
                int bepos = rlen;
                printf("%08d %s %d %d %d %d %d %d %d %s\n", grp->end.bread, buffer,
                            0, bbpos, bepos, grp->blen,
                            grp->beg.path.abpos, grp->end.path.aepos, alen, "*");

            } else {
                printf("%08d %s\n", grp->end.bread, buffer);
            }
        } else {
            fprintf(stderr, "[WARNING]Skipping super-long read %08d, len=%lld, buf=%lld\n", grp->end.bread, rlen, bsize);
        }
    }
    printf("+ +\n");
}

static char *Usage[] =
    { "[-smfocargyUFMPI] [-i<int(4)>] [-w<int(100)>] [-b<int(10)>] ",
      "    <src1:db|dam> [ <src2:db|dam> ] <align:las> [ <reads:FILE> | <reads:range> ... ]"
    };

#define LAST_READ_SYMBOL  '$'

static int ORDER(const void *l, const void *r)
{ int x = *((int32 *) l);
  int y = *((int32 *) r);
  return (x-y);
}

/* globals, for use in Compute_Detailed_Alignment() */
DAZZ_DB *db1;
DAZZ_DB *db2;
Overlap *ovl;
Alignment *aln;
int     tspace, tbytes, small;
uint16    *trace;
Work_Data *work;
char      *abuffer, *bbuffer;

int     ALIGN, CARTOON, REFERENCE, FLIP;
int     INDENT, WIDTH, BORDER, UPPERCASE;
int     ISTWO;
int     MAP;
int     FALCON, OVERLAP, M4OVL, IGNORE_INDELS;
// XXX: MAX_HIT_COUNT should be renamed
int     SEED_MIN, MAX_HIT_COUNT, SKIP;
int     PRELOAD;
int     WRITE_MAPPING_COORDS;

void Compute_Detailed_Alignment(
    int small)
{
                char *aseq, *bseq;
                int   amin,  amax;
                int   bmin,  bmax;

                if (FLIP)
                  Flip_Alignment(aln,0);
                if (small)
                  Decompress_TraceTo16(ovl);

                amin = ovl->path.abpos - BORDER;
                if (amin < 0) amin = 0;
                amax = ovl->path.aepos + BORDER;
                if (amax > aln->alen) amax = aln->alen;
                if (COMP(aln->flags))
                  { bmin = (aln->blen-ovl->path.bepos) - BORDER;
                    if (bmin < 0) bmin = 0;
                    bmax = (aln->blen-ovl->path.bbpos) + BORDER;
                    if (bmax > aln->blen) bmax = aln->blen;
                  }
                else
                  { bmin = ovl->path.bbpos - BORDER;
                    if (bmin < 0) bmin = 0;
                    bmax = ovl->path.bepos + BORDER;
                    if (bmax > aln->blen) bmax = aln->blen;
                  }

                aseq = Load_Subread(db1,ovl->aread,amin,amax,abuffer,0);
                bseq = Load_Subread(db2,ovl->bread,bmin,bmax,bbuffer,0);

                aln->aseq = aseq - amin;
                if (COMP(aln->flags))
                  { Complement_Seq(bseq,bmax-bmin);
                    aln->bseq = bseq - (aln->blen - bmax);
                  }
                else
                  aln->bseq = bseq - bmin;

                Compute_Trace_PTS(aln,work,tspace,GREEDIEST);

                if (FLIP)
                  { if (COMP(aln->flags))
                      { Complement_Seq(aseq,amax-amin);
                        Complement_Seq(bseq,bmax-bmin);
                        aln->aseq = aseq - (aln->alen - amax);
                        aln->bseq = bseq - bmin;
                      }
                    Flip_Alignment(aln,1);
                  }
}

int main(int argc, char *argv[])
{
  DAZZ_DBX   _dbx1, *dbx1 = &_dbx1;
  DAZZ_DBX   _dbx2, *dbx2 = &_dbx2;
  Overlap   _ovl;
  Alignment _aln;

  db1 = &dbx1->db;
  db2 = &dbx2->db;
  ovl = &_ovl;
  aln = &_aln;

  FILE   *input;
  int64   novl;
  int     reps, *pts;
  int     input_pts;

  //  Process options

  { int    i, j, k;
    int    flags[128];
    char  *eptr;

    ARG_INIT("LA4Falcon")

    INDENT    = 4;
    WIDTH     = 100;
    BORDER    = 10;

    FALCON    = 0;
    M4OVL     = 0;
    IGNORE_INDELS = 0;
    SEED_MIN  = 8000;
    SKIP      = 0;

    ALIGN     = 0;
    REFERENCE = 0;
    CARTOON   = 0;
    FLIP      = 0;
    MAX_HIT_COUNT = 400;

    WRITE_MAPPING_COORDS = 0;

    j = 1;
    for (i = 1; i < argc; i++)
      if (argv[i][0] == '-')
        switch (argv[i][1])
        { default:
            ARG_FLAGS("smfocargUFMPIy")
            break;
          case 'i':
            ARG_NON_NEGATIVE(INDENT,"Indent")
            break;
          case 'w':
            ARG_POSITIVE(WIDTH,"Alignment width")
            break;
          case 'b':
            ARG_NON_NEGATIVE(BORDER,"Alignment border")
            break;
          case 'H':
            ARG_POSITIVE(SEED_MIN,"seed threshold (in bp)")
            break;
          case 'n':
            ARG_POSITIVE(MAX_HIT_COUNT, "max numer of supporting read ouput (used for FALCON consensus. default 400, max: 2000)")
            if (MAX_HIT_COUNT > 2000) MAX_HIT_COUNT = 2000;
            break;
        }
      else
        argv[j++] = argv[i];
    argc = j;

    UPPERCASE = flags['U'];
    ALIGN     = flags['a'];
    REFERENCE = flags['r'];
    CARTOON   = flags['c'];
    FLIP      = flags['F'];
    MAP       = flags['M'];
    OVERLAP   = flags['o'];
    M4OVL     = flags['m'];
    FALCON    = flags['f'];
    SKIP      = flags['s'];
    GROUP     = flags['g'];
    PRELOAD   = flags['P']; // Preload DB reads, if possible.
    IGNORE_INDELS = flags['I']; // Ignore indels. Accuracy counts mismatches only. With "-m" flag.
    WRITE_MAPPING_COORDS = flags['y'];

    if (argc <= 2)
      { fprintf(stderr,"Usage: %s %s\n",Prog_Name,Usage[0]);
        fprintf(stderr,"       %*s %s\n",(int) strlen(Prog_Name),"",Usage[1]);
        exit (1);
      }
  }

  if (WRITE_MAPPING_COORDS && FALCON) {
      fprintf(stderr, "[DALIGNER Info] Mapping coordinates will be written for FALCON consensus.\n");
  } else {
      fprintf(stderr, "[DALIGNER Info] No mapping coordinates will be written for FALCON consensus. The consensus will have to re-map.\n");
  }

  //  Open trimmed DB or DB pair

  { int   status;
    char *pwd, *root;
    FILE *input;

    ISTWO  = 0;
    status = Open_DBX(argv[1],dbx1,PRELOAD);
    if (status < 0)
      exit (1);
    if (db1->part > 0)
      { fprintf(stderr,"%s: Cannot be called on a block: %s\n",Prog_Name,argv[1]);
        exit (1);
      }

    if (argc > 3)
      { pwd   = PathTo(argv[3]);
        root  = Root(argv[3],".las");
        if ((input = fopen(Catenate(pwd,"/",root,".las"),"r")) != NULL)
          { ISTWO = 1;
            fclose(input);
            status = Open_DBX(argv[2],dbx2,PRELOAD);
            if (status < 0)
              exit (1);
            if (db2->part > 0)
              { fprintf(stderr,"%s: Cannot be called on a block: %s\n",Prog_Name,argv[2]);
                exit (1);
              }
            Trim_DB(db2);
          }
        else
            { dbx2 = dbx1;
              db2 = db1;
            }
        free(root);
        free(pwd);
      }
    else
      { dbx2 = dbx1;
        db2 = db1;
      }
    Trim_DB(db1);
  }

  //  Process read index arguments into a sorted list of read ranges

  input_pts = 0;
  if (argc == ISTWO+4)
    { if (argv[ISTWO+3][0] != LAST_READ_SYMBOL || argv[ISTWO+3][1] != '\0')
        { char *eptr, *fptr;
          int   b, e;

          b = strtol(argv[ISTWO+3],&eptr,10);
          if (eptr > argv[ISTWO+3] && b > 0)
            { if (*eptr == '-')
                { if (eptr[1] != LAST_READ_SYMBOL || eptr[2] != '\0')
                    { e = strtol(eptr+1,&fptr,10);
                      input_pts = (fptr <= eptr+1 || *fptr != '\0' || e <= 0);
                    }
                }
              else
                input_pts = (*eptr != '\0');
            }
          else
            input_pts = 1;
        }
    }

  if (input_pts)
    { int v, x;
      FILE *input;

      input = Fopen(argv[ISTWO+3],"r");
      if (input == NULL)
        exit (1);

      reps = 0;
      while ((v = fscanf(input," %d",&x)) != EOF)
        if (v == 0)
          { fprintf(stderr,"%s: %d'th item of input file %s is not an integer\n",
                           Prog_Name,reps+1,argv[2]);
            exit (1);
          }
        else
          reps += 1;

      reps *= 2;
      pts   = (int *) Malloc(sizeof(int)*reps,"Allocating read parameters");
      if (pts == NULL)
        exit (1);

      rewind(input);
      for (v = 0; v < reps; v += 2)
        { fscanf(input," %d",&x);
          pts[v] = pts[v+1] = x;
        }

      fclose(input);
    }

  else
    { pts  = (int *) Malloc(sizeof(int)*2*argc,"Allocating read parameters");
      if (pts == NULL)
        exit (1);

      reps = 0;
      if (argc > 3+ISTWO)
        { int   c, b, e;
          char *eptr, *fptr;

          for (c = 3+ISTWO; c < argc; c++)
            { if (argv[c][0] == LAST_READ_SYMBOL)
                { b = db1->nreads;
                  eptr = argv[c]+1;
                }
              else
                b = strtol(argv[c],&eptr,10);
              if (eptr > argv[c])
                { if (b <= 0)
                    { fprintf(stderr,"%s: %d is not a valid index\n",Prog_Name,b);
                      exit (1);
                    }
                  if (*eptr == '\0')
                    { pts[reps++] = b;
                      pts[reps++] = b;
                      continue;
                    }
                  else if (*eptr == '-')
                    { if (eptr[1] == LAST_READ_SYMBOL)
                        { e = INT32_MAX;
                          fptr = eptr+2;
                        }
                      else
                        e = strtol(eptr+1,&fptr,10);
                      if (fptr > eptr+1 && *fptr == 0 && e > 0)
                        { pts[reps++] = b;
                          pts[reps++] = e;
                          if (b > e)
                            { fprintf(stderr,"%s: Empty range '%s'\n",Prog_Name,argv[c]);
                              exit (1);
                            }
                          continue;
                        }
                    }
                }
              fprintf(stderr,"%s: argument '%s' is not an integer range\n",Prog_Name,argv[c]);
              exit (1);
            }

          qsort(pts,reps/2,sizeof(int64),ORDER);

          b = 0;
          for (c = 0; c < reps; c += 2)
            if (b > 0 && pts[b-1] >= pts[c]-1)
              { if (pts[c+1] > pts[b-1])
                  pts[b-1] = pts[c+1];
              }
            else
              { pts[b++] = pts[c];
                pts[b++] = pts[c+1];
              }
          pts[b++] = INT32_MAX;
          reps = b;
        }
      else
        { pts[reps++] = 1;
          pts[reps++] = INT32_MAX;
        }
    }

  //  Initiate file reading and read (novl, tspace) header

  { char  *over, *pwd, *root;

    pwd   = PathTo(argv[2+ISTWO]);
    root  = Root(argv[2+ISTWO],".las");
    over  = Catenate(pwd,"/",root,".las");
    input = Fopen(over,"r");
    if (input == NULL)
      exit (1);

    if (fread(&novl,sizeof(int64),1,input) != 1)
      SYSTEM_READ_ERROR
    if (fread(&tspace,sizeof(int),1,input) != 1)
      SYSTEM_READ_ERROR

    if (tspace == 0) {
        printf("\nCRITICAL ERROR: tspace=0 in '%s'", root);
        exit(1);
    }
    if (tspace <= TRACE_XOVR)
      { small  = 1;
        tbytes = sizeof(uint8);
      }
    else
      { small  = 0;
        tbytes = sizeof(uint16);
      }

    if (!(FALCON || M4OVL))
      {
        printf("\n%s: ",root);
        Print_Number(novl,0,stdout);
        printf(" records\n");
      }

    free(pwd);
    free(root);
  }

  //  Read the file and display selected records

  { int        j;
    int        tmax;
    int        in, npt, idx, ar;
    int64      tps;
    int64      p_aread = -1;
    char       buffer[131072];
    int        skip_rest = 0;

    int        ar_wide, br_wide;
    int        ai_wide, bi_wide;
    int        mn_wide, mx_wide;
    int        tp_wide;
    int        blast, match, seen, lhalf, rhalf;
    int        hit_count;

    aln->path = &(ovl->path);
    if (ALIGN || REFERENCE || FALCON || (M4OVL && IGNORE_INDELS))
      { work = New_Work_Data();
        abuffer = New_Read_Buffer(db1);
        bbuffer = New_Read_Buffer(db2);
        if (FALCON) {
            ovlgrps = calloc(sizeof(OverlapGroup), MAX_OVERLAPS+1);
            hit_count = -1;
        }
      }
    else
      { abuffer = NULL;
        bbuffer = NULL;
        work = NULL;
      }

    tmax  = 1000;
    trace = (uint16 *) Malloc(sizeof(uint16)*tmax,"Allocating trace vector");
    if (trace == NULL)
      exit (1);

    in  = 0;
    npt = pts[0];
    idx = 1;

    ar_wide = Number_Digits((int64) db1->nreads);
    br_wide = Number_Digits((int64) db2->nreads);
    ai_wide = Number_Digits((int64) db1->maxlen);
    bi_wide = Number_Digits((int64) db2->maxlen);
    if (db1->maxlen < db2->maxlen)
      { mn_wide = ai_wide;
        mx_wide = bi_wide;
        tp_wide = Number_Digits((int64) db1->maxlen/tspace+2);
      }
    else
      { mn_wide = bi_wide;
        mx_wide = ai_wide;
        tp_wide = Number_Digits((int64) db2->maxlen/tspace+2);
      }
    ar_wide += (ar_wide-1)/3;
    br_wide += (br_wide-1)/3;
    ai_wide += (ai_wide-1)/3;
    bi_wide += (bi_wide-1)/3;
    mn_wide += (mn_wide-1)/3;
    tp_wide += (tp_wide-1)/3;

    if (FLIP)
      { int x;
        x = ar_wide; ar_wide = br_wide; br_wide = x;
        x = ai_wide; ai_wide = bi_wide; bi_wide = x;
      }

    //  For each record do

    // debugging
    // setlocale(LC_NUMERIC, ""); // for commas in numbers with %'d, but this can cause other problems
    fprintf(stderr, "\nabout to go into loop with novl = %lld %s\n", novl, argv[3]);
    time_t timeLast;
    time(&timeLast);
    // end debugging

    blast = -1;
    match = 0;
    seen  = 0;
    lhalf = rhalf = 0;
    for (j = 0; j < novl; j++)
       //  Read it in

      {

         // debugging
         time_t timeCurrent;
         time(&timeCurrent);
         double diff_t = difftime(timeCurrent, timeLast);


         if ( ( diff_t > 60.0 ) || ( j < 10) || ( j % 1000000 == 0 ) ) {
            time_t mytime = time(NULL);
            fprintf(stderr, "before Read_Overlap record j = %d out of %lld at %s %s", j, novl, argv[3], ctime(&mytime));
            fflush(stderr);
            timeLast = timeCurrent;
         }
         // end debugging



        Read_Overlap(input,ovl);
        if (ovl->path.tlen > tmax)
          { tmax = ((int) 1.2*ovl->path.tlen) + 100;
            trace = (uint16 *) Realloc(trace,sizeof(uint16)*tmax,"Allocating trace vector");
            if (trace == NULL)
              exit (1);
          }
        ovl->path.trace = (void *) trace;
        Read_Trace(input,ovl,tbytes);

        //  Determine if it should be displayed

        ar = ovl->aread+1;
        if (in)
          { while (ar > npt)
              { npt = pts[idx++];
                if (ar < npt)
                  { in = 0;
                    break;
                  }
                npt = pts[idx++];
              }
          }
        else
          { while (ar >= npt)
              { npt = pts[idx++];
                if (ar <= npt)
                  { in = 1;
                    break;
                  }
                npt = pts[idx++];
              }
          }
        if (!in)
          continue;

        //  Display it

        aln->alen  = db1->reads[ovl->aread].rlen;
        aln->blen  = db2->reads[ovl->bread].rlen;
        aln->flags = ovl->flags;
        tps        = ((ovl->path.aepos-1)/tspace - ovl->path.abpos/tspace);

        if (OVERLAP && !FALCON)
          { if (ovl->path.abpos != 0 && ovl->path.bbpos != 0)
              continue;
            if (ovl->path.aepos != aln->alen && ovl->path.bepos != aln->blen)
              continue;
          }

        if (MAP)
          { while (ovl->bread != blast)
              { if (!match && seen && !(lhalf && rhalf))
                  { printf("Missing ");
                    Print_Number((int64) blast+1,br_wide+1,stdout);
                    printf(" %d ->%lld\n",db2->reads[blast].rlen,db2->reads[blast].coff);
                  }
                match = 0;
                seen  = 0;
                lhalf = rhalf = 0;
                blast += 1;
              }
            seen = 1;
            if (ovl->path.abpos == 0)
              rhalf = 1;
            if (ovl->path.aepos == aln->alen)
              lhalf = 1;
            if (ovl->path.bbpos != 0 || ovl->path.bepos != aln->blen)
              continue;
            match = 1;
          }

        // printf(" %7d %7d\n",ovl->path.abpos,ovl->path.aepos);
        // continue;

        if (!(FALCON || M4OVL) ) {
            if (ALIGN || CARTOON || REFERENCE)
              printf("\n");
            if (FLIP)
              { Flip_Alignment(aln,0);
                Print_Number((int64) ovl->bread+1,ar_wide+1,stdout);
                printf("  ");
                Print_Number((int64) ovl->aread+1,br_wide+1,stdout);
              }
            else
              { Print_Number((int64) ovl->aread+1,ar_wide+1,stdout);
                printf("  ");
                Print_Number((int64) ovl->bread+1,br_wide+1,stdout);
              }
            if (COMP(ovl->flags))
              printf(" c");
            else
              printf(" n");
            printf("   [");
            Print_Number((int64) ovl->path.abpos,ai_wide,stdout);
            printf("..");
            Print_Number((int64) ovl->path.aepos,ai_wide,stdout);
            printf("] x [");
            Print_Number((int64) ovl->path.bbpos,bi_wide,stdout);
            printf("..");
            Print_Number((int64) ovl->path.bepos,bi_wide,stdout);
            printf("]");
        }

        //  Display it
        if (M4OVL)
          {
            int64 bbpos, bepos;
            double acc;
            if (COMP(ovl->flags)) {
                bbpos = (int64) aln->blen - (int64) ovl->path.bepos;
                bepos = (int64) aln->blen - (int64) ovl->path.bbpos;
            } else {
                bbpos = (int64) ovl->path.bbpos;
                bepos = (int64) ovl->path.bepos;
            }
            double const ovllen = 0.5*((ovl->path.aepos - ovl->path.abpos) + (ovl->path.bepos - ovl->path.bbpos));
            int diffs = aln->path->diffs;
            if (IGNORE_INDELS) {
              /* Compute the detailed alignment from trace points so that aln->path->tlen
               *     is equal to the number of indels in the alignment. */
              Compute_Detailed_Alignment(small);
              diffs -= (aln->path->tlen);
            }
            acc = 100-(100. * diffs)/ovllen;
            printf("%09lld %09lld %lld %0.2f ", (int64) ovl->aread, (int64) ovl->bread,  (int64) bbpos - (int64) bepos, acc);
            printf("0 %lld %lld %lld ", (int64) ovl->path.abpos, (int64) ovl->path.aepos, (int64) aln->alen);
            printf("%d %lld %lld %lld ", COMP(ovl->flags), bbpos, bepos, (int64) aln->blen);

            if ( ((int64) aln->blen < (int64) aln->alen) && ((int64) ovl->path.bbpos < 1) && ((int64) aln->blen - (int64) ovl->path.bepos < 1) )
              {
                printf("contains\n");
              }
            else if ( ((int64) aln->alen < (int64) aln->blen) && ((int64) ovl->path.abpos < 1) && ((int64) aln->alen -
 (int64) ovl->path.aepos < 1) )
              {
                printf("contained\n");
              }
            else
              {
                printf("overlap\n");
              }
          }
        if (FALCON)
          {
            if (p_aread == -1) {
                Load_ReadX(dbx1, ovl->aread, abuffer, 2);
                if (WRITE_MAPPING_COORDS) {
                    printf("%08d %s %d %d %d %d %d %d %d %s\n", ovl->aread, abuffer, 0, 0, aln->alen, aln->alen, 0, aln->alen, aln->alen, "*");
                } else {
                    printf("%08d %s\n", ovl->aread, abuffer);
                }
                p_aread = ovl->aread;
                skip_rest = 0;
            }
            if (p_aread != ovl -> aread ) {
                print_hits(hit_count, dbx2, bbuffer, buffer, (int64)sizeof(buffer), aln->alen, MAX_HIT_COUNT, WRITE_MAPPING_COORDS);
                hit_count = -1;

                Load_ReadX(dbx1, ovl->aread, abuffer, 2);

                if (WRITE_MAPPING_COORDS) {
                    printf("%08d %s %d %d %d %d %d %d %d %s\n", ovl->aread, abuffer, 0, 0, aln->alen, aln->alen, 0, aln->alen, aln->alen, "*");
                } else {
                    printf("%08d %s\n", ovl->aread, abuffer);
                }
                p_aread = ovl->aread;
                skip_rest = 0;
            }

            if (skip_rest == 0) {
                if (add_overlap(aln, ovl, hit_count))
                    hit_count ++;

                if ((hit_count+1) > MAX_OVERLAPS)
                    skip_rest = 1;

#undef TEST_ALN_OUT
#ifdef TEST_ALN_OUT
                {
                    tps = ((ovl->path.aepos-1)/tspace - ovl->path.abpos/tspace);
                    if (small)
                        Decompress_TraceTo16(ovl);
                    Load_ReadX(dbx1, ovl->aread, abuffer, 0);
                    Load_ReadX(dbx2, ovl->bread, bbuffer, 0);
                    if (COMP(aln->flags))
                        Complement_Seq(bbuffer, aln->blen);
                    Compute_Trace_PTS(aln,work,tspace);
                    int tlen  = aln->path->tlen;
                    int *trace = aln->path->trace;
                    int u;
                    printf(" ");
                    for (u = 0; u < tlen; u++)
                        printf("%d,", (int16) trace[u]);
                }
#endif
                //printf("\n");
                if (SKIP == 1) {  //if SKIP = 0, then skip_rest is always 0
                    if ( ((int64) aln->alen < (int64) aln->blen) && ((int64) ovl->path.abpos < 1) && ((int64) aln->alen - (int64) ovl->path.aepos < 1) ) {
                        printf("* *\n");
                        skip_rest = 1;
                    }
                }
            }
         }

        if (ALIGN || CARTOON || REFERENCE)
          { if (ALIGN || REFERENCE)
              Compute_Detailed_Alignment(small);
            if (CARTOON)
              { printf("  (");
                Print_Number(tps,tp_wide,stdout);
                printf(" trace pts)\n\n");
                Alignment_Cartoon(stdout,aln,INDENT,mx_wide);
              }
            else
              { printf(" :   = ");
                Print_Number((int64) ovl->path.diffs,mn_wide,stdout);
                printf(" diffs  (");
                Print_Number(tps,tp_wide,stdout);
                printf(" trace pts)\n");
              }
            if (REFERENCE)
              Print_Reference(stdout,aln,work,INDENT,WIDTH,BORDER,UPPERCASE,mx_wide);
            if (ALIGN)
              Print_Alignment(stdout,aln,work,INDENT,WIDTH,BORDER,UPPERCASE,mx_wide);
          }
        else if (!(FALCON || M4OVL) )
          { printf(" :   < ");
            Print_Number((int64) ovl->path.diffs,mn_wide,stdout);
            printf(" diffs  (");
            Print_Number(tps,tp_wide,stdout);
            printf(" trace pts)\n");
          }
      }

    // debugging
    time_t mytime = time(NULL);
    fprintf(stderr, "\ncompleted loop record j = %d out of %lld at %s %s\n", j, novl, argv[3], ctime(&mytime));
    // end debugging


    if (FALCON && hit_count != -1)
      {
        print_hits(hit_count, dbx2, bbuffer, buffer, (int64)sizeof(buffer), aln->alen, MAX_HIT_COUNT, WRITE_MAPPING_COORDS);
        printf("- -\n");
        free(ovlgrps);
      }


    free(trace);
    if (NULL != work)
      { free(bbuffer-1);
        free(abuffer-1);
        Free_Work_Data(work);
      }
  }

  Close_DBX(dbx1);
  if (ISTWO)
    Close_DBX(dbx2);
  exit (0);
}
