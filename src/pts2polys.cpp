#include <Rcpp.h>
using namespace Rcpp;

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>


#define MAXNESTS 50000


//////////////////////////

/* Clarkson-Delaunay.h */
/*
* Ken Clarkson wrote this.  Copyright (c) 1995 by AT&T..
* Permission to use, copy, modify, and distribute this software for any
* purpose without fee is hereby granted, provided that this entire notice
* is included in all copies of any software which is or includes a copy
* or modification of this software and in all copies of the supporting
* documentation for such software.
*/


typedef double Coord;
typedef Coord* point;

#define max_blocks 10000
#define       Nobj 10000

#define MAXDIM 4
#define BLOCKSIZE 100000
#define MAXBLOCKS 1000

#define VA(x) ((x)->vecs+rdim)
#define VB(x) ((x)->vecs)

typedef point site;

typedef struct basis_s {
  struct basis_s *next; /* free list */
int ref_count;   /* storage management */
int lscale;    /* the log base 2 of total scaling of vector */
Coord sqa, sqb; /* sums of squared norms of a part and b part */
Coord vecs[1]; /* the actual vectors, extended by malloc'ing bigger */
} basis_s;

//STORAGE_GLOBALS(basis_s)
extern size_t basis_s_size;
extern basis_s *basis_s_list;
extern basis_s *new_block_basis_s(int);
extern void flush_basis_s_blocks(void);
void free_basis_s_storage(void);


typedef struct neighbor {
  site vert; /* vertex of simplex */
struct simplex *simp; /* neighbor sharing all vertices but vert */
basis_s *basis; /* derived vectors */
} neighbor;


typedef struct simplex {
  struct simplex *next;   /* free list */
long visit;      /* number of last site visiting this simplex */
short mark;
basis_s* normal;   /* normal vector pointing inward */
neighbor peak;      /* if null, remaining vertices give facet */
neighbor neigh[1];   /* neighbors of simplex */
} simplex;
// STORAGE_GLOBALS(simplex)
extern size_t simplex_size;
extern simplex *simplex_list;
extern simplex *new_block_simplex(int);
extern void flush_simplex_blocks(void);
void free_simplex_storage(void);


typedef struct fg_node fg;
typedef struct tree_node Tree;
struct tree_node {
  Tree *left, *right;
  site key;
  int size;   /* maintained to be the number of nodes rooted here */
fg *fgs;
Tree *next; /* freelist */
};
// STORAGE_GLOBALS(Tree)
extern size_t Tree_size;
extern Tree *Tree_list;
extern Tree *new_block_Tree(int);
extern void flush_Tree_blocks(void);
void free_Tree_storage(void);



typedef struct fg_node {
  Tree *facets;
  double dist, vol;   /* of Voronoi face dual to this */
fg *next;        /* freelist */
short mark;
int ref_count;
} fg_node;
// STORAGE_GLOBALS(fg)
extern size_t fg_size;
extern fg *fg_list;
extern fg *new_block_fg(int);
extern void flush_fg_blocks(void);
void free_fg_storage(void);



typedef simplex * visit_func(simplex *, void *);
typedef int test_func(simplex *, int, void *);

static int sees(site, simplex *);
static void buildhull(simplex *);
static simplex *facets_print(simplex *s, void *p);
static simplex *visit_triang_gen(simplex *s, visit_func *visit, test_func *test);



//////////////////////////


#define WORD  unsigned int


/*
* Ken Clarkson wrote this.  Copyright (c) 1995 by AT&T..
* Permission to use, copy, modify, and distribute this software for any
* purpose without fee is hereby granted, provided that this entire notice
* is included in all copies of any software which is or includes a copy
* or modification of this software and in all copies of the supporting
* documentation for such software.
*/

/* ----------------------------------------------------------------------------
Explanation of this triangulation function
---------------------------------------------------------------------

I, Eric Hufschmid, extracted the triangulation function from the standalone program
that Ken Clarkson created. The result is this triangulation routine. The logic comes
from Ken Clarkson. All I did is extract it from his convex hull program.  Clarkson's
source code and notes are here:
http://netlib.sandia.gov/voronoi/hull.html

The function that you will call to do the triangulation is at the bottom of this file.
It is called:

WORD *BuildTriangleIndexList ( void *, float, int *, int , int , int * );

It takes 6 arguments:

WORD *BuildTriangleIndexList (
    void *pointList,           INPUT, an array of either float or integer "points".
A "point" is either two values (an X and a Y),
or three values (XY and Z).
You must allocate memory and fill this array with XY or XYZ points.
float factor               INPUT, if pointList is a list of integers, set this parameter to
zero to let the function realize that you are using integers.
If you give this a value, pointList will be interpreted as
an array of floating-point values.
Clarkson's function works on integers, so if pointList is an array of
floating-point values, each value will be multiplied by this factor
in order to convert it to an integer. Therefore, provide a factor that
is large enough so that when the floating points are converted, you
don't lose too many digits after the decimal point, unless
you want to lose some digits.
Example: if you provide a factor of only 2.0, then the
floating-point values 3.0001 and 3.499 will become the
same integer value: 6
That would be acceptable if both of those points are supposed
to be the same, but otherwise it could cause trouble.
int numberOfInputPoints,   INPUT, the number of points in the list,
not the total number of integer values.
int numDimensions,         INPUT, 2 for X and Y, or 3 for XY and Z
int clockwise                There are three options:
-1: put triangles in anti-clockwise order
0: don't waste time ordering the triangles
1: put triangles in clockwise order
int *numTriangleVertices )  OUTPUT, this does not need to be initialized.
BuildTriangleIndexList gives this a value.

The function returns a pointer to an array of triangle indices.
I don't know the limit of Clarkson's function is in regards to how many input points
it will accept, but I set the return value to 16-bit integers because I assume nobody
needs more than 64,000 triangles during one function call. If you want to return
integers instead, you only need to do a search and replace to change WORD to int.
The few references to WORD are from me, not from Clarkson.

The function does not need to be initialized or closed down.
You are likely to want 4 variables to use it:

WORD *triangleIndexList;   <- OUTPUT, this does not need initialization
int  *testPointsXY;        <- INPUT, allocate this and fill it with xy, or xyz points
int   numPoints;           <- INPUT, the number of points.
int   numTriangleVertices; <- OUTPUT, this does not need initialization


Call it like this:

triangleIndexList =               <- allocated and filled by the function
BuildTriangleIndexList(
  testPointsXY,           <- The array of points that you create
0,                      <- zero if the points are integers, otherwise provide a float
numPoints,              <- The number of points
2,                      <- 2 if the list is X and Y points, 3 if XYZ points
1,                      <- DirectX wants clockwise triangles
&numTriangleVertices);  <- filled by the function

The calling function is responsible for releasing the return value with free().

That return value is an array of indices into the point list, and they define the triangles.

You don't have to do anything to that array.
Just put it into the block of code that creates the triangle index buffer,
and in the statement that draws the triangles. For example:

<..>
bd.Usage = D3D11_USAGE_DEFAULT;
bd.ByteWidth = sizeof( WORD ) * numTriangleVertices;    <-- numTriangleVertices from the function
bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
<..>
InitData.pSysMem =  triangleIndexList;                  <-- triangleIndexList from the function
pd3dDevice->CreateBuffer( &bd, &InitData, &g_pTestVectorIndexBuffer );
<..>

g_pImmediateContext->DrawIndexed( numTriangleVertices, 0, 0);   <-- numTriangleVertices from the function

Then free the array:
free ( triangleIndexList );

--------------------------------------------------------------------------------------
-------------------------------------------------------------------------------------- */


static int *ptrToIntsToIndex, *listOfIntsToIndex;
static float *ptrFloatsToIndex, *listOfFloatsToIndex, mult_up;

static WORD *ptrToOutputList ;
static int triangleDirection;

static int numPointsProcessed;
static int totalInputPoints;
static int maxOutputEntries ;
static int currenOutputIndex;
static void triangleList_out (int v0, int v1, int v2, int v3);

static point site_blocks[MAXBLOCKS];
static int   num_blocks;

// The next block of variables were static variables within functions that I moved
// outside of the function.  I prepended each of the variables with the name of the function.
// For example: sc_lscale was originally "lscale" in sc()
//              visit_triang_gen_ss was originally "ss" in visit_triang_gen()
//              search_ss was originally "ss" in search()
static long get_next_site_s_num;
static neighbor out_of_flat_p_neigh;
static basis_s *sees_b;
static long visit_triang_gen_vnum;
static long visit_triang_gen_ss;
static simplex **visit_triang_gen_st;
static simplex **search_st;
static long search_ss;
static int   sc_lscale;
static double   sc_max_scale, sc_ldetbound, sc_Sb;
static simplex *make_facets_ns;

// --------- from ch.c : numerical functions for hull computation ---------
const int    EXACT_BITS = 53;   // = (int)floor (DBL_MANT_DIG * log ((double)FLT_RADIX) / log(2.) );
const double B_ERR_MIN = (float)(DBL_EPSILON*MAXDIM*(1<<MAXDIM)*MAXDIM*3.01);
const double B_ERR_MIN_SQ = B_ERR_MIN * B_ERR_MIN;

static Coord  hull_infinity[10]={57.2,0,0,0,0}; /* point at infinity for Delaunay triangulation; value not used */

static basis_s   tt_basis = {0,1,-1,0,0,{0}},
  *tt_basisp = &tt_basis,
  *infinity_basis;

  static int   pdim;   /* point dimension */
static simplex *ch_root;

#define DELIFT 0
static int basis_vec_size;

// ------ from hull.c : "combinatorial" functions for hull computation
static long pnum;
static site p;
static int  rdim,   /* region dimension: (max) number of sites specifying region */
cdim,   /* number of sites currently specifying region */
site_size, /* size of malloc needed for a site */
point_size;  /* size of malloc needed for a point */

// STORAGE(simplex)    expands into:
size_t simplex_size;
simplex *simplex_list = 0;
simplex *new_block_simplex(int make_blocks)  {
  int i;
  static simplex *simplex_block_table[max_blocks];
  simplex *xlm, *xbt;
  static int num_simplex_blocks;
  if (make_blocks)  {
    xbt = simplex_block_table[num_simplex_blocks++] = (simplex*)malloc(Nobj * simplex_size);
    memset(xbt, 0, Nobj *simplex_size);
    xlm = (simplex*)( (char*)xbt + (Nobj * simplex_size));
    for (i=0;i<Nobj; i++) {
      // **6Sep2014** xlm = ((simplex*) ( (char*)xlm + ((-1)) *simplex_size));
      xlm = ((simplex*) ( (char*)xlm - simplex_size ));
      xlm->next = simplex_list;
      simplex_list = xlm;
    }
    return simplex_list;
  };
  for (i=0; i<num_simplex_blocks; i++)  free(simplex_block_table[i]);
  *simplex_block_table = 0;
  num_simplex_blocks = 0;
  simplex_list = 0;
  return 0;
}
void free_simplex_storage(void) { new_block_simplex(0); }


// STORAGE(basis_s)    expands into:
size_t basis_s_size;
basis_s *basis_s_list = 0;
basis_s *new_block_basis_s(int make_blocks) {
  int i;
  static basis_s *basis_s_block_table[max_blocks];
  basis_s *xlm, *xbt;
  static int num_basis_s_blocks;
  if (make_blocks) {
    xbt = basis_s_block_table[num_basis_s_blocks++] = (basis_s*)malloc(Nobj *basis_s_size);
    memset(xbt,0,Nobj *basis_s_size);
    xlm = (basis_s*)( (char*)xbt + (Nobj * basis_s_size));
    for (i=0;i<Nobj; i++) {
      // **6Sep2014** xlm = ((basis_s*) ( (char*)xlm + ((-1)) *basis_s_size));
      xlm = (basis_s*)( (char*)xlm - basis_s_size);
      xlm->next = basis_s_list;
      basis_s_list = xlm;
    }
    return basis_s_list;
  };
  for (i=0; i<num_basis_s_blocks; i++) free(basis_s_block_table[i]);
  *basis_s_block_table = NULL;
  num_basis_s_blocks = 0;
  basis_s_list = 0;
  return 0;
}
void free_basis_s_storage(void) {
  new_block_basis_s(0);
}



// --------- from ch.c : numerical functions for hull computation ---------

static Coord Vec_dot(point x, point y) {
  int i;
  Coord sum = 0;
  for (i=0;i<rdim;i++) sum += x[i] * y[i];
  return sum;
}
// ----------------------------------------------------------------
static Coord Vec_dot_pdim(point x, point y) {
  int i;
  Coord sum = 0;
  for (i=0;i<pdim;i++) sum += x[i] * y[i];
  return sum;
}
// ----------------------------------------------------------------
static Coord Norm2(point x) {
  int i;
  Coord sum = 0;
  for (i=0;i<rdim;i++) sum += x[i] * x[i];
  return sum;
}
// ----------------------------------------------------------------
static void Ax_plus_y(Coord a, point x, point y) {
  int i;
  for (i=0;i<rdim;i++) {
    *y++ += a * *x++;
  }
}
// ----------------------------------------------------------------
static void Ax_plus_y_test(Coord a, point x, point y) {
  int i;
  for (i=0;i<rdim;i++) {
    // check_overshoot(*y + a * *x);
    *y++ += a * *x++;
  }
}
// ----------------------------------------------------------------
static void Vec_scale_test(int n, Coord a, Coord *x)
{
  Coord *xx = x,
    *xend = xx + n   ;
  while (xx!=xend) {
    *xx *= a;
    // check_overshoot(*xx);
    xx++;
  }
}


// ----------------------------------------------------------------
static double sc(basis_s *v,simplex *s, int k, int j) {
  /* amount by which to scale up vector, for reduce_inner */

  double      labound;
  double temp;

  if (j<10) {
    labound = logb(v->sqa)/2;
    sc_max_scale = EXACT_BITS - labound - 0.66*(k-2)-1  -DELIFT;
    if (sc_max_scale<1) {
      // warning(-10, overshot exact arithmetic);
      sc_max_scale = 1;
    }

    if (j==0) {
      int   i;
      neighbor *sni;
      basis_s *snib;

      sc_ldetbound = DELIFT;

      sc_Sb = 0;
      for (i=k-1,sni=s->neigh+k-1;i>0;i--,sni--) {
        snib = sni->basis;
        sc_Sb += snib->sqb;
        sc_ldetbound += logb(snib->sqb)/2 + 1;
        sc_ldetbound -= snib->lscale;
      }
    }
  }
  // if (sc_ldetbound - v->lscale + _logb(v->sqb)/2 + 1 < 0)
  // when v->sqb is 0, _logb gives "divide by zero" error with Borland 2007 compilier, so check for it
  temp = v->sqb;
  if (temp)  temp = logb(temp) * 0.5;
  if (sc_ldetbound - v->lscale + temp + 1 < 0) {
    return 0;
  } else {
    sc_lscale = (int)floor(logb(2*sc_Sb/(v->sqb + v->sqa*B_ERR_MIN)))/2;
    if (sc_lscale > sc_max_scale) {
      sc_lscale = (int)floor(sc_max_scale);
    } else if (sc_lscale<0) sc_lscale = 0;
    v->lscale += sc_lscale;
    return ( ((int)(sc_lscale)<20) ? 1<<(int)(sc_lscale) : ldexp(1.f,(int)(sc_lscale)) );
  }
}


// ----------------------------------------------------------------
static int reduce_inner(basis_s *v, simplex *s, int k) {
  // nothing is using the return value of this function
  point   va = VA(v),
    vb = VB(v);
  int   i,j;
  double   dd;
  basis_s   *snibv;
  neighbor *sni;
  // static int failcount;

  v->sqa = v->sqb = Norm2(vb);
  if (k<=1) {
    memcpy(vb,va,basis_vec_size);
    return 1;
  }
  for (j=0;j<250;j++) {

    memcpy(vb,va,basis_vec_size);
    for (i=k-1,sni=s->neigh+k-1;i>0;i--,sni--) {
      snibv = sni->basis;
      dd = -Vec_dot(VB(snibv),vb)/ snibv->sqb;
      Ax_plus_y( dd, VA(snibv), vb);
    }
    v->sqb = Norm2(vb);
    v->sqa = Norm2(va);

    if (2*v->sqb >= v->sqa) { return 1;}

    Vec_scale_test(rdim, sc(v,s,k,j), va);

    for (i=k-1,sni=s->neigh+k-1;i>0;i--,sni--) {
      snibv = sni->basis;
      dd = -Vec_dot(VB(snibv),va)/snibv->sqb;
      dd = floor(dd+0.5);
      Ax_plus_y_test( dd, VA(snibv), va);
    }
  }
  //  if (failcount++<10) {} a failure ?
  return 0;
}

// ----------------------------------------------------------------
static int reduce(basis_s **v, point p, simplex *s, int k) {
  // nothing is using the return value of this function
  point   z;
  point   tt = s->neigh[0].vert;

  // if (!*v) NEWLRC(basis_s,(*v))
  if (!*v) {
    (*v) = basis_s_list ? basis_s_list : new_block_basis_s(1);
    basis_s_list = (*v)->next;
    (*v)->ref_count = 1;
  }
  else (*v)->lscale = 0;


  // z = VB(*v);
  z = ((*v)->vecs);
  if (p==hull_infinity) memcpy(*v,infinity_basis,basis_s_size);
  // else {trans(z,p,tt); lift(z,s);}
  else {
    {
      int i;
      for (i=0;i<pdim;i++) z[i+rdim] = z[i] = p[i] - tt[i];
    };
    {
      z[2*rdim-1] = z[rdim-1] = ldexp(Vec_dot_pdim(z,z), -0);
    };
  }
  return reduce_inner(*v,s,k);
}

// ----------------------------------------------------------------
static void get_basis_sede(simplex *s) {

  int   k=1;
  neighbor *sn = s->neigh+1,
    *sn0 = s->neigh;

  if (sn0->vert == hull_infinity && cdim >1) {
    // SWAP(neighbor, *sn0, *sn );
    { neighbor t; t = *sn0; *sn0 = *sn; *sn = t; };
    // NULLIFY(basis_s,sn0->basis);
    {{ if ((sn0->basis) && --(sn0->basis)->ref_count == 0) {
      memset(((sn0->basis)),0,basis_s_size);
      ((sn0->basis))->next = basis_s_list;
      basis_s_list = (sn0->basis);
    };
    };
      sn0->basis = 0;
    };
    sn0->basis = tt_basisp;
    tt_basisp->ref_count++;
  } else {
    if (!sn0->basis) {
      sn0->basis = tt_basisp;
      tt_basisp->ref_count++;
    } else while (k < cdim && sn->basis) {k++;sn++;}
  }
  while (k<cdim) {
    // NULLIFY(basis_s,sn->basis);
    {{ if ((sn->basis) && --(sn->basis)->ref_count == 0) {
      memset(((sn->basis)),0,basis_s_size);
      ((sn->basis))->next = basis_s_list;
      basis_s_list = (sn->basis);
    };
    };
      sn->basis = 0;
    };
    reduce(&sn->basis,sn->vert,s,k);
    k++; sn++;
  }
}


// ----------------------------------------------------------------
static int out_of_flat(simplex *root, point p) {

  if (!out_of_flat_p_neigh.basis)
    out_of_flat_p_neigh.basis = (basis_s*) malloc(basis_s_size);

  out_of_flat_p_neigh.vert = p;
  cdim++;
  root->neigh[cdim-1].vert = root->peak.vert;
  // NULLIFY(basis_s,root->neigh[cdim-1].basis);
  {{ if ((root->neigh[cdim-1].basis) && --(root->neigh[cdim-1].basis)->ref_count == 0) {
    memset(((root->neigh[cdim-1].basis)),0,basis_s_size);
    ((root->neigh[cdim-1].basis))->next = basis_s_list;
    basis_s_list = (root->neigh[cdim-1].basis);
  };
  };
    root->neigh[cdim-1].basis = 0;
  };

  get_basis_sede(root);
  if (root->neigh[0].vert == hull_infinity) return 1;
  reduce(&out_of_flat_p_neigh.basis,p,root,cdim);
  if (out_of_flat_p_neigh.basis->sqa != 0) return 1;
  cdim--;
  return 0;
}


// ----------------------------------------------------------------
static void get_normal_sede(simplex *s) {

  neighbor *rn;
  int i,j;

  get_basis_sede(s);
  if (rdim==3 && cdim==3) {
    point   c,
    a = VB(s->neigh[1].basis),
    b = VB(s->neigh[2].basis);
    // NEWLRC(basis_s,s->normal);
    { s->normal = basis_s_list ? basis_s_list : new_block_basis_s(1);
      basis_s_list = s->normal->next;
      s->normal->ref_count = 1;
    };
    // c = VB(s->normal);
    c = ((s->normal)->vecs);
    c[0] = a[1]*b[2] - a[2]*b[1];
    c[1] = a[2]*b[0] - a[0]*b[2];
    c[2] = a[0]*b[1] - a[1]*b[0];
    s->normal->sqb = Norm2(c);
    for (i=cdim+1,rn = ch_root->neigh+cdim-1; i; i--, rn--) {
      for (j = 0; j<cdim && rn->vert != s->neigh[j].vert;j++);
      if (j<cdim) continue;
      if (rn->vert==hull_infinity) {
        if (c[2] > -B_ERR_MIN) continue;
      } else  if (!sees(rn->vert,s)) continue;
      c[0] = -c[0]; c[1] = -c[1]; c[2] = -c[2];
      break;
    }
    return;
  }

  for (i=cdim+1,rn = ch_root->neigh+cdim-1; i; i--, rn--) {
    for (j = 0; j<cdim && rn->vert != s->neigh[j].vert;j++);
    if (j<cdim) continue;
    reduce(&s->normal,rn->vert,s,cdim);
    if (s->normal->sqb != 0) break;
  }

}

// ----------------------------------------------------------------
static int sees(site p, simplex *s) {
  point   tt,zz;
  double   dd,dds;
  int i;

  if (!sees_b)
    sees_b = (basis_s*)malloc(basis_s_size);
  else
    sees_b->lscale = 0;
  // zz = VB(sees_b);
  zz = ((sees_b)->vecs);
  if (cdim==0) return 0;
  if (!s->normal) {
    get_normal_sede(s);
    // for (i=0;i<cdim;i++) NULLIFY(basis_s,s->neigh[i].basis);
    for (i=0;i<cdim;i++) {
      { if ((s->neigh[i].basis) && --(s->neigh[i].basis)->ref_count == 0) {
        memset(((s->neigh[i].basis)),0,basis_s_size);
        ((s->neigh[i].basis))->next = basis_s_list;
        basis_s_list = (s->neigh[i].basis);
      };
      };
      s->neigh[i].basis = 0;
    };
  }
  tt = s->neigh[0].vert;
  if (p==hull_infinity) memcpy(sees_b,infinity_basis,basis_s_size);
  // else {trans(zz,p,tt); lift(zz,s);}
  else {
    { int i;
      for (i=0;i<pdim;i++) zz[i+rdim] = zz[i] = p[i] - tt[i];
    };
    {
      zz[2*rdim-1] =zz[rdim-1]= ldexp(Vec_dot_pdim(zz,zz), -0);
    };
  }
  for (i=0;i<3;i++) {
    dd = Vec_dot(zz,s->normal->vecs);
    if (dd == 0.0) {
      return 0;
    }
    dds = dd*dd/s->normal->sqb/Norm2(zz);
    if (dds > B_ERR_MIN_SQ) return (dd<0);
    get_basis_sede(s);
    reduce_inner(sees_b,s,cdim);
  }
  //          exit(1);
  return 0;
}


// ----------------------------------------------------------------
static void ReleaseMemory(void)  {
  int i;
  free_basis_s_storage();
  free_simplex_storage();

  for (i=0; i<num_blocks; i++)
    free (site_blocks[i]);
  if (sees_b)
    free (sees_b);
  if (visit_triang_gen_st)
    free (visit_triang_gen_st);
  if (search_st)
    free (search_st);
}

// ----------------------------------------------------------------
static simplex *facet_test(simplex *s, void *dummy) {return (!s->peak.vert) ? s : NULL;}
// -------------------------------------------
static int hullt(simplex *s, int i, void *dummy) {return i>-1;}
// -------------------------------------------
static int truet(simplex *s, int i, void *dum) {return 1;}
// -------------------------------------------
static simplex *visit_triang(simplex *root, visit_func *visit)
  /* visit the whole triangulation */
{return visit_triang_gen(root, visit, truet);}

// ----------------------------------------------------------------
static void build_convex_hull(void) {
  // site_numm   returns number of site when given site
  // dim         dimension of point set

  simplex *s, *root;

  // In order to use Clarkson's program as a function, the global and static variables
  // have to be reset every time
  cdim = 0;
  rdim = pdim+1;
  if (rdim > MAXDIM)
    Rcpp::stop("dimension bound MAXDIM exceeded; rdim=%d; pdim=%d\n", rdim, pdim);

  numPointsProcessed = 0;
  ptrToIntsToIndex  = listOfIntsToIndex;    // reset this in case the points are integers
  ptrFloatsToIndex = listOfFloatsToIndex;   // reset this in case the points are floats

  ptrToOutputList = NULL;

  get_next_site_s_num = 0;
  memset ((char*)site_blocks, 0, sizeof (site_blocks) );
  num_blocks = 0;

  out_of_flat_p_neigh.basis = 0;
  out_of_flat_p_neigh.simp = 0;
  out_of_flat_p_neigh.vert = 0;

  sees_b = NULL;

  visit_triang_gen_st = NULL;
  visit_triang_gen_vnum = -1;
  visit_triang_gen_ss = 2000;

  search_st = NULL;
  search_ss = MAXDIM;

  tt_basis.next = NULL;
  tt_basis.ref_count = 1;
  tt_basis.lscale = -1;
  tt_basis.sqa = 0;
  tt_basis.sqb = 0;
  tt_basis.vecs[0] = 0;

  sc_lscale = 0;
  sc_max_scale = sc_ldetbound = sc_Sb = 0;

  make_facets_ns = NULL;

  point_size = site_size = sizeof(Coord)*pdim;
  basis_vec_size = sizeof(Coord)*rdim;
  basis_s_size = sizeof(basis_s)+ (2*rdim-1)*sizeof(Coord);
  simplex_size = sizeof(simplex) + (rdim-1)*sizeof(neighbor);

  root = NULL;
  p = hull_infinity;
  // NEWLRC(basis_s, infinity_basis);
  { infinity_basis = basis_s_list ? basis_s_list : new_block_basis_s(1);
    basis_s_list = infinity_basis->next;
    infinity_basis->ref_count = 1;
  };
  infinity_basis->vecs[2*rdim-1]
  = infinity_basis->vecs[rdim-1]
  = 1;
  infinity_basis->sqa
    = infinity_basis->sqb
    = 1;

    // NEWL(simplex,root);
    { root = simplex_list ? simplex_list : new_block_simplex(1);
      simplex_list = root->next;
    };

    ch_root = root;

    // copy_simp(s,root);
    { {
      s = simplex_list ? simplex_list : new_block_simplex(1);
      simplex_list = s->next;
    };
      memcpy(s,root,simplex_size);
      {
        int i;
        neighbor *mrsn;
        for (i=-1,mrsn=root->neigh-1;i<cdim;i++,mrsn++) {
          if (mrsn->basis) mrsn->basis->ref_count++;
        };
      }; };

    root->peak.vert = p;
    root->peak.simp = s;
    s->peak.simp = root;

    buildhull(root);  // process the points

    /* visit all simplices with facets of the current hull */
    visit_triang_gen( visit_triang(root, facet_test), facets_print, hullt);      // create a triangle list

    ReleaseMemory();
}



// -------------------------------------------
static simplex *visit_triang_gen(simplex *s, visit_func *visit, test_func *test) {
  /*
  * starting at s, visit simplices t such that test(s,i,0) is true,
  * and t is the i'th neighbor of s;
  * apply visit function to all visited simplices;
  * when visit returns nonNULL, exit and return its value
  */
  neighbor *sn;
  void *v;
  simplex *t;
  int i;
  long tms = 0;
#define pushv(x) *(visit_triang_gen_st + tms++) = x;
#define popv(x)  x = *(visit_triang_gen_st + --tms);


  visit_triang_gen_vnum--;
  if (!visit_triang_gen_st)
    visit_triang_gen_st = (simplex**)malloc((visit_triang_gen_ss + MAXDIM+1) * sizeof(simplex*));
  if (s) pushv(s);
  while (tms) {
    if (tms>visit_triang_gen_ss) { // DEBEXP(-1,tms);
      visit_triang_gen_st=(simplex**)realloc(visit_triang_gen_st,
                           ((visit_triang_gen_ss += visit_triang_gen_ss)+MAXDIM+1) * sizeof(simplex*));
    }
    popv(t);
    if (!t || t->visit == visit_triang_gen_vnum) continue;
    t->visit = visit_triang_gen_vnum;
    if ((v=(*visit)(t,0))) {return (simplex*)v;}
    for (i=-1,sn = t->neigh-1;i<cdim;i++,sn++)
      if ((sn->simp->visit != visit_triang_gen_vnum) && sn->simp && test(t,i,0))
        pushv(sn->simp);
  }
  return NULL;
}



// ----------------------------------------------------------------
static neighbor *op_simp(simplex *a, simplex *b) {{
  int i;
  /* the neighbor entry of a containing b */
  neighbor *x;
  for (i=0, x = a->neigh; (x->simp != b) && (i<cdim) ; i++, x++) ;
  if (i<cdim) return x;
  else {
    Rcpp::stop("Error!\n"); }
  }}


// ----------------------------------------------------------------
static neighbor *op_vert(simplex *a, site b)   {  {
  int i;
  /* the neighbor entry of a containing b */
  neighbor *x;
  for (i=0, x = a->neigh; (x->vert != b) && (i<cdim) ; i++, x++) ;
  if (i<cdim)
    return x;
  else {
    Rcpp::stop("Error!\n"); }
  } }


// ----------------------------------------------------------------
static void connect(simplex *s) {
  /* make neighbor connections between newly created simplices incident to p */

  site xf,xb,xfi;
  simplex *sb, *sf, *seen;
  int i;
  neighbor *sn;

  if (!s) return;
  // assert(!s->peak.vert && s->peak.simp->peak.vert==p && !op_vert(s,p)->simp->peak.vert);
  if (s->visit==pnum) return;
  s->visit = pnum;
  seen = s->peak.simp;
  xfi = op_simp(seen,s)->vert;
  for (i=0, sn = s->neigh; i<cdim; i++,sn++) {
    xb = sn->vert;
    if (p == xb) continue;
    sb = seen;
    sf = sn->simp;
    xf = xfi;
    if (!sf->peak.vert) {   /* are we done already? */
  sf = op_vert(seen,xb)->simp;
      if (sf->peak.vert) continue;
    } else do {
      xb = xf;
      xf = op_simp(sf,sb)->vert;
      sb = sf;
      sf = op_vert(sb,xb)->simp;
    } while (sf->peak.vert);

    sn->simp = sf;
    op_vert(sf,xf)->simp = s;

    connect(sf);
  }
}



// ----------------------------------------------------------------
static simplex *make_facets(simplex *seen) {
  /*
  * visit simplices s with sees(p,s), and make a facet for every neighbor
  * of s not seen by p
  */

  simplex *n;
  neighbor *bn;
  int i;


  if (!seen) return NULL;
  seen->peak.vert = p;

  for (i=0,bn = seen->neigh; i<cdim; i++,bn++) {
    n = bn->simp;
    if (pnum != n->visit) {
      n->visit = pnum;
      if (sees(p,n)) make_facets(n);
    }
    if (n->peak.vert) continue;
    // copy_simp(make_facets_ns,seen);
    { { make_facets_ns = simplex_list ? simplex_list : new_block_simplex(1);
      simplex_list = make_facets_ns->next;
    };
      memcpy(make_facets_ns,seen,simplex_size);
      {
        int i;
        neighbor *mrsn;
        for (i=-1,mrsn=seen->neigh-1;i<cdim;i++,mrsn++) {
          if (mrsn->basis) mrsn->basis->ref_count++;
        };
      }; };

    make_facets_ns->visit = 0;
    make_facets_ns->peak.vert = 0;
    make_facets_ns->normal = 0;
    make_facets_ns->peak.simp = seen;
    // NULLIFY(basis_s,make_facets_ns->neigh[i].basis);
    {{ if ((make_facets_ns->neigh[i].basis) && --(make_facets_ns->neigh[i].basis)->ref_count == 0) {
      memset(((make_facets_ns->neigh[i].basis)),0,basis_s_size);
      ((make_facets_ns->neigh[i].basis))->next = basis_s_list;
      basis_s_list = (make_facets_ns->neigh[i].basis);
    };
    };
      make_facets_ns->neigh[i].basis = 0;
    };
    make_facets_ns->neigh[i].vert = p;
    bn->simp = op_simp(n,seen)->simp = make_facets_ns;
  }
  return make_facets_ns;
}



// ----------------------------------------------------------------
static simplex *extend_simplices(simplex *s) {
  /*
  * p lies outside flat containing previous sites;
  * make p a vertex of every current simplex, and create some new simplices
  */

  int   i, ocdim=cdim-1;
  simplex *ns;
  neighbor *nsn;

  if (s->visit == pnum) return s->peak.vert ? s->neigh[ocdim].simp : s;
  s->visit = pnum;
  s->neigh[ocdim].vert = p;
  // NULLIFY(basis_s,s->normal);
  {{ if ((s->normal) && --(s->normal)->ref_count == 0) {
    memset(((s->normal)),0,basis_s_size);
    ((s->normal))->next = basis_s_list;
    basis_s_list = (s->normal);
  };
  };
    s->normal = 0;
  };
  // NULLIFY(basis_s,s->neigh[0].basis);
  {{ if ((s->neigh[0].basis) && --(s->neigh[0].basis)->ref_count == 0) {
    memset(((s->neigh[0].basis)),0,basis_s_size);
    ((s->neigh[0].basis))->next = basis_s_list;
    basis_s_list = (s->neigh[0].basis);
  };
  };
    s->neigh[0].basis = 0;
  };
  if (!s->peak.vert) {
    s->neigh[ocdim].simp = extend_simplices(s->peak.simp);
    return s;
  } else {
    // copy_simp(ns,s);
    { { ns = simplex_list ? simplex_list : new_block_simplex(1);
      simplex_list = ns->next;
    };
      memcpy(ns,s,simplex_size);
      {
        int i;
        neighbor *mrsn;
        for (i=-1,mrsn=s->neigh-1;i<cdim;i++,mrsn++) {
          if (mrsn->basis) mrsn->basis->ref_count++;
        };
      }; };

    s->neigh[ocdim].simp = ns;
    ns->peak.vert = NULL;
    ns->peak.simp = s;
    ns->neigh[ocdim] = s->peak;
    // inc_ref(basis_s,s->peak.basis);
    { if (s->peak.basis)  s->peak.basis->ref_count++; };
    for (i=0,nsn=ns->neigh;i<cdim;i++,nsn++)
      nsn->simp = extend_simplices(nsn->simp);
  }
  return ns;
}


// ----------------------------------------------------------------
static simplex *search(simplex *root) {
  /* return a simplex s that corresponds to a facet of the
  * current hull, and sees(p, s) */

  simplex *s;
  neighbor *sn;
  int i;
  long tms = 0;
#define pushs(x) *(search_st + tms++) = x;
#define pops(x)  x = *(search_st + --tms);

  if (!search_st)
    search_st = (simplex **)malloc((search_ss+MAXDIM+1)*sizeof(simplex*));
  pushs(root->peak.simp);
  root->visit = pnum;
  if (!sees(p,root))
    for (i=0,sn=root->neigh;i<cdim;i++,sn++) pushs(sn->simp);
  while (tms) {
    if (tms>search_ss)
      search_st=(simplex**)realloc(search_st,
                 ((search_ss += search_ss) + MAXDIM+1) * sizeof(simplex*));
    pops(s);
    if (s->visit == pnum) continue;
    s->visit = pnum;
    if (!sees(p,s)) continue;
    if (!s->peak.vert) return s;
    for (i=0, sn=s->neigh; i<cdim; i++,sn++) pushs(sn->simp);
  }
  return NULL;
}


// -------------------------------------------
static site new_site (site p, long j) {

  if (0==(j%BLOCKSIZE)) {
    return(site_blocks[num_blocks++]=(site)malloc(BLOCKSIZE*site_size));
  } else
    return p + pdim;
}

// -------------------------------------------
static site get_next_site(void) {
  int i;
  p = new_site(p, get_next_site_s_num);
  get_next_site_s_num++;

  if (numPointsProcessed >= totalInputPoints)  {
    // guess at how much memory is needed for the output list
    maxOutputEntries = numPointsProcessed * 3*3; // 3 values per triangle, and there will be about 2 times as many triangles as input points
    ptrToOutputList = (WORD*)malloc((maxOutputEntries + 1) * sizeof(WORD));
    currenOutputIndex = 0;
    return 0;
  }
  if (ptrToIntsToIndex)  {         // if there is a list of integer points
    for (i=0; i<pdim; i++)  {
      p[i] = *ptrToIntsToIndex++;
    }
  }
  else  {                         // else convert the floating points to integers
    for (i=0; i<pdim; i++)  {
      p[i] = floor(*ptrFloatsToIndex * mult_up + 0.5);
      ptrFloatsToIndex++;
    }
  }
  numPointsProcessed ++;
  return p;
}

// -------------------------------------------
static long site_numm(site p) {
  long i,j;

  if (p==hull_infinity) return -1;
  if (!p) return -2;
  for (i=0; i<num_blocks; i++)
    if ((j=p-site_blocks[i])>=0 && j < BLOCKSIZE*pdim)
      return j/pdim + BLOCKSIZE*i;
    return -3;
}

// ----------------------------------------------------------------
static point get_another_site(void) {
  point pnext;

  pnext = get_next_site();

  if (!pnext) return NULL;
  pnum = site_numm(pnext)+2;
  return pnext;
}


// ----------------------------------------------------------------
static void buildhull (simplex *root) {

  while (cdim < rdim) {
    p = get_another_site();
    if (!p) return;
    if (out_of_flat(root,p))
      extend_simplices(root);
    else
      connect(make_facets(search(root)));
  }
  while ((p = get_another_site()))
    connect(make_facets(search(root)));
}


// ------------------------------------------------------
static simplex *facets_print(simplex *s, void *p) {
  point v[MAXDIM];
  int j;

  for (j=0;j<cdim;j++) v[j] = s->neigh[j].vert;

  triangleList_out ( (int)site_numm(v[0]), (int)site_numm(v[1]), (int)site_numm(v[2]),
                     (pdim == 3) ? (int)site_numm(v[3]) : 0 );
  return NULL;
}

// ---------------------------------------------------------------------------
static int IsFloatTriangleClockwise (float *a, float *b, float *c)  {
  return ( ((b[0] - a[0]) * (b[1] + a[1]) +
           (c[0] - b[0]) * (c[1] + b[1]) +
           (a[0] - c[0]) * (a[1] + c[1])) > 0);
}
// ---------------------------------------------------------------------------
static int IsTriangleClockwise (int *a, int *b, int *c)  {
  return ( ((b[0] - a[0]) * (b[1] + a[1]) +
           (c[0] - b[0]) * (c[1] + b[1]) +
           (a[0] - c[0]) * (a[1] + c[1])) > 0);
}

// ------------------------------------------------------
static void triangleList_out (int v0, int v1, int v2, int v3) {
  // outfunc: given a list of points, output in a given format
  // if one of the values < 0, it is a point to identify the convex hull rather than a triangle
  int isCW;

  // the v3 value is valid only when the input points are 3-D, XY and Z values,
  // but what is what is the v3 value used for, aside from becoming -1 once in
  // a while to identify points on the convex hull?

  if (v0 >= 0 && v1 >= 0 && v2 >= 0 && v3 >= 0)  {
    // set the direction of the triangles to clockwise
    // v0, v1, v2 are indexes to triangle vertexes, an x and y, in listOfIntsToIndex, so,
    // v0 is index to the first vertex: ie, listOfIntsToIndex[v0*2], listOfIntsToIndex[v0*2+1]
    // v1 is listOfIntsToIndex[v1*2], listOfIntsToIndex[v1*2+1]
    if (triangleDirection)  {
      if (ptrToIntsToIndex)  {         // if there is a list of integer points
        isCW = IsTriangleClockwise (&listOfIntsToIndex[v0*2], &listOfIntsToIndex[v1*2], &listOfIntsToIndex[v2*2]);
      }
      else  {
        isCW = IsFloatTriangleClockwise (&listOfFloatsToIndex[v0*2], &listOfFloatsToIndex[v1*2], &listOfFloatsToIndex[v2*2]);
      }
      if ( ((triangleDirection > 0) && !isCW)  ||   // if user wants CW triangles, but it is not CW
           ((triangleDirection < 0) && isCW))  {     // or user wants CCW triangles, but it is CW
        ptrToOutputList[currenOutputIndex++] = (WORD)v2;
        ptrToOutputList[currenOutputIndex++] = (WORD)v1;
        ptrToOutputList[currenOutputIndex++] = (WORD)v0;
        return;
      }
    }
    ptrToOutputList[currenOutputIndex++] = (WORD)v0;
    ptrToOutputList[currenOutputIndex++] = (WORD)v1;
    ptrToOutputList[currenOutputIndex++] = (WORD)v2;

    // In all the testing I did so far, currenOutputIndex has never exceeeded maxOutputEntries
    // So I removed the test to see if currenOutputIndex is within the appropriate range
  }
}

// ------------------------------------------------------
WORD *BuildTriangleIndexList (void *pointList, float factor, int numberOfInputPoints, int numDimensions, int clockwise, int *numTriangleVertices ) {
  // returns an index list that can be used by: ->IASetIndexBuffer(), using the format: DXGI_FORMAT_R16_UINT
  // Adjust triangleList_out() if you do not want to spend time putting the triangles into clockwise order,
  // or to put them in anti-clockwise order.

  // I don't know what the limit of Clarkson's function is in regards to how many input points
  // it will accept, but I set the return value to 16-bit integers because I assume nobody needs
  // more than 64,000 triangles at a time

  if (factor)  {
    ptrToIntsToIndex = NULL;     // set to NULL to show get_next_site() to process floating-points
    mult_up = factor;
    listOfFloatsToIndex = (float*)pointList;
  }
  else  {
    // the points are integers, in which case mult_up and listOfFloatsToIndex will not be used
    // so they don't need to be initialized
    listOfIntsToIndex = (int*)pointList;
  }

  pdim = numDimensions;
  totalInputPoints = numberOfInputPoints;
  triangleDirection = clockwise;

  build_convex_hull();    // This function does all the work

  *numTriangleVertices = currenOutputIndex;
  return ptrToOutputList ;    // calling function has to free return value: ptrToOutputList ;
}




//////////////////////////







/*
* Defines a wrapper class for a 2D array, implemented as a 1D array
*/
template <typename T>
class Array2D{
  // private data members
private:
  int rowNum;
  int columnNum;
  T* Matrix;

public:
  /*
  * Constructor
  */
  Array2D(int row, int column){
    if (row > 0 && column > 0){
      rowNum = row;
      columnNum = column;
      Matrix = new T[rowNum *columnNum + columnNum];
    }

  }

  /*
  * Deconstructor
  */
  ~Array2D(){
    delete[] Matrix;
  }

  /*
  * Returns pointer to item at Matrx[x][y][0]
  * i.e, an "array" of size depthNum
  */
  T* operator () (int x){
    return &Matrix[x*columnNum + 0];
  }

  /*
  * Returns item at Matrix[x][y][z]
  */
  const T& operator () (int x, int y) const{
    return Matrix[x*columnNum + y];
  }
  T& operator () (int x, int y){
    return Matrix[x*columnNum + y];
  }

  int getRowNum(){
    return rowNum;
  }

  int getColumnNum(){
    return columnNum;
  }

};


struct edge{
  int p1; //End point 1
  int p2; //End point 2
  int t1; //occurence of edge in indexlist
};


double *comparelen;
int comparelength(const void *e1,const void *e2)
{
  //Sort by decreasing length, then by increasing p1, then increasing p2.
  struct edge *edge1 = (struct edge*)e1;
  struct edge *edge2 = (struct edge*)e2;

  double len1 = comparelen[edge1->t1];
  double len2 = comparelen[edge2->t1];

  if(len1 > len2) return -1;
  else if(len1 < len2) return 1;
  else if(edge1->p1 == edge2->p1) return edge1->p2 - edge2->p2;
  else return edge1->p1 - edge2->p1;
}


double *boundarypoints(float *points,int numpoints,int *numboundarypoints,long MINLEN)
{
  unsigned int *indexlist;
  int numTriangleVertices;
  int vnum;

  indexlist = BuildTriangleIndexList (points, 1.0, numpoints, 2, 1, &numTriangleVertices);

  //Build list of edges
  struct edge *edgelist = (struct edge*)malloc(numTriangleVertices*sizeof(struct edge));
  double *len = (double*)malloc(numTriangleVertices*sizeof(double));
  for(vnum=0; vnum < numTriangleVertices; vnum++){
    edgelist[vnum].p1 = indexlist[vnum];
    if(vnum%3 == 2) edgelist[vnum].p2 = indexlist[vnum-2];
    else edgelist[vnum].p2 = indexlist[vnum+1];
    if(edgelist[vnum].p2 < edgelist[vnum].p1){
      //Swap vertices
      edgelist[vnum].p1 = edgelist[vnum].p2;
      edgelist[vnum].p2 = indexlist[vnum];
    }
    edgelist[vnum].t1 = vnum;

    double x1 = points[2*(edgelist[vnum].p1)];
    double y1 = points[2*(edgelist[vnum].p1)+1];
    double x2 = points[2*(edgelist[vnum].p2)];
    double y2 = points[2*(edgelist[vnum].p2)+1];
    len[vnum] = sqrt((x1-x2)*(x1-x2) + (y1-y2)*(y1-y2));
  }

  //sort by length
  comparelen = len;
  qsort(edgelist,numTriangleVertices,sizeof(struct edge),&comparelength);

  //identify internal and external edges
  int *eext = (int*)malloc(numTriangleVertices*sizeof(int));
  int *partner = (int*)malloc(numTriangleVertices*sizeof(int));
  eext[edgelist[numTriangleVertices-1].t1] = 1; //Shortest edge is external by default
  partner[edgelist[numTriangleVertices-1].t1] = -1;
  for(vnum=0; vnum < numTriangleVertices-1; vnum++){
    if((edgelist[vnum].p1 == edgelist[vnum+1].p1) && (edgelist[vnum].p2 == edgelist[vnum+1].p2))
    {
      eext[edgelist[vnum].t1] = eext[edgelist[vnum+1].t1] = 0;
      partner[edgelist[vnum].t1] = edgelist[vnum+1].t1;
      partner[edgelist[vnum+1].t1] = edgelist[vnum].t1;
      vnum++;
    }
    else{
      eext[edgelist[vnum].t1] = 1;
      partner[edgelist[vnum].t1] = -1;
    }
  }

  //identify internal and external points
  int *pext = (int*)malloc(numpoints*sizeof(int));
  for(vnum=0;vnum<numpoints;vnum++) pext[vnum]=0;
  for(vnum=0;vnum<numTriangleVertices;vnum++){
    if(eext[vnum]){
      if(vnum%3 == 2) pext[indexlist[vnum]]=pext[indexlist[vnum-2]]=1;
      else pext[indexlist[vnum]]=pext[indexlist[vnum+1]]=1;
    }
  }

  //Remove external edges that can be removed
  int *removed = (int*)malloc(numTriangleVertices*sizeof(int));
  for(vnum=0;vnum<numTriangleVertices;vnum++) removed[vnum]=0;
  for(vnum=0;vnum<numTriangleVertices;vnum++){
    int edge = edgelist[vnum].t1;
    int edgeA = edge+1;
    if(edgeA%3 == 0) edgeA-=3;
    int edgeB = edgeA+1;
    if(edgeB%3 == 0) edgeB-=3;
    int vert = indexlist[edgeB];
    if(!removed[edge] && len[edge]>MINLEN && eext[edge] && !eext[edgeA] && !eext[edgeB] && !pext[vert]){
      removed[edge] = removed[edgeA] = removed[edgeB] = 1;
      eext[partner[edgeA]] = eext[partner[edgeB]] = 1;
      pext[vert] = 1;
      vnum = -1; //Very inefficient - returns to the beginning after each edge removal
    }
  }

  //Construct clockwise simple polygon
  for(vnum=0; vnum<numTriangleVertices; vnum++){
    if(eext[vnum] && !removed[vnum]) break; //First external edge
  }

  int next = vnum;
  *numboundarypoints = 0;
  double *boundary = (double*)malloc(2*numpoints*sizeof(double));

  do{
    boundary[2*(*numboundarypoints)] = points[2*indexlist[next]];
    boundary[2*(*numboundarypoints)+1] = points[2*indexlist[next]+1];
    (*numboundarypoints)++;
    next++;
    if(next%3 == 0) next -= 3;

    while(!eext[next]){
      next = partner[next];
      next++;
      if(next%3 == 0) next -= 3;
    }
  }
  while(next!=vnum);

  free(indexlist);
  free(edgelist);
  free(len);
  free(eext);
  free(pext);
  free(removed);
  free(partner);

  return boundary;
}


int doublecompare(const void *e1,const void *e2)
{
  double *x1 = (double*)e1;
  double *x2 = (double*)e2;

  if(*x1 < *x2) return -1;
  else if(*x1 > *x2) return 1;
  return 0;
}


void updategrid(double *boundary,int numboundarypoints,Array2D<long> &grid,long xstart,long ystart,long gridsize)
{
  long bottomrow,toprow;

  //Get lists of crossovers
  Array2D<double> crossovers(grid.getRowNum(),numboundarypoints);
  int *numcrossovers = (int*)malloc(grid.getRowNum()*sizeof(int));
  for(int i=0;i<grid.getRowNum();i++) numcrossovers[i] = 0;

  for(int i=0; i<numboundarypoints;i++){
    double x1 = boundary[2*i];
    double y1 = boundary[2*i+1];
    double x2,y2;
    if(i == numboundarypoints-1){
      x2 = boundary[0];
      y2 = boundary[1];
    }
    else{
      x2 = boundary[2*(i+1)];
      y2 = boundary[2*(i+1)+1];
    }
    if(y1==y2) continue;

    if(y1<y2){
      bottomrow = (int)ceil((y1 - ystart)/gridsize);
      if(ystart + (bottomrow-1)*gridsize >= y1) bottomrow--;
      toprow = (int)floor((y2 - ystart)/gridsize);
      if(ystart + toprow*gridsize == y2) toprow--;
      else if(ystart + (toprow+1)*gridsize < y2) toprow++;
    }
    else{
      bottomrow = (int)ceil((y2 - ystart)/gridsize);
      if(ystart + (bottomrow-1)*gridsize >= y2) bottomrow--;
      toprow = (int)floor((y1 - ystart)/gridsize);
      if(ystart + toprow*gridsize == y1) toprow--;
      else if(ystart + (toprow+1)*gridsize < y1) toprow++;
    }

    double m = (x2 - x1) / (y2 - y1);
    for(int j=bottomrow;j<=toprow;j++){
      double y = ystart + j*gridsize;
      crossovers(j,numcrossovers[j]) = x1 + m*(y - y1);
      numcrossovers[j]++;
    }
  }

  //Get bounding box of boundary
  double left,right,top,bottom;
  left = right = boundary[0];
  top = bottom = boundary[1];
  for(int i=1;i<numboundarypoints;i++){
    if(boundary[2*i] < left) left = boundary[2*i];
    else if(boundary[2*i] > right) right = boundary[2*i];
    if(boundary[2*i+1] < bottom) bottom = boundary[2*i+1];
    else if(boundary[2*i+1] > top) top = boundary[2*i+1];
  }

  //Sort crossovers
  bottomrow = (int)floor((bottom - ystart)/gridsize);
  toprow = (int)ceil((top - ystart)/gridsize);
  for(int i=bottomrow;i<=toprow;i++){
    if(numcrossovers[i]>0) qsort(crossovers(i), numcrossovers[i], sizeof(double), &doublecompare);
  }

  //Increment grid
  int leftcol = (int)ceilf((left - xstart)/gridsize);
  int rightcol = (int)floorf((right - xstart)/gridsize);
  for(int i=bottomrow;i<=toprow;i++){
    int count = 0;
    for(int j=leftcol;j<=rightcol;j++){
      double x = xstart + j*gridsize;
      while(count<numcrossovers[i] && (x > crossovers(i,count) || (x == crossovers(i,count) && count%2 == 0))) count++;
      if(count%2 == 1) grid(j,i)++;
    }
  }

  free(numcrossovers);
}


int readsample(FILE *nestCoords,float *currentList)
{
  char line[1000];
  int numpts;


  fgets(line,1000,nestCoords);
  if(line[0] != 'P'){
    Rcpp::stop("Input file not at start of point set in readsample.\n");
  }

  numpts = 0;
  while(fgets(line,1000,nestCoords) && strlen(line) > 1){
    sscanf(line,"%f %f",&currentList[2*numpts],&currentList[2*numpts+1]);
    numpts++;
  }

  return numpts;
}


// [[Rcpp::export]]
List pts2polys(std::string in_string,
                 int SAMPLESIZE,long MINLEN,long GRIDSIZE,
                 long MINX,long MAXX,long MINY,long MAXY){


  const char *nestCoordsFileName = in_string.c_str();

  int sample;//counter to iterate through the (non burn in) samples;

  long xstart = (MINX/GRIDSIZE)*GRIDSIZE;
  if(xstart < MINX) xstart=(MINX/GRIDSIZE+1)*GRIDSIZE;
  long ystart = (MINY/GRIDSIZE)*GRIDSIZE;
  if(ystart < MINY) ystart = (MINY/GRIDSIZE+1)*GRIDSIZE;
  long xwidth = (MAXX - xstart + 1)/GRIDSIZE;
  long ywidth = (MAXY - ystart + 1)/GRIDSIZE;

  FILE *nestCoords;//reading in the nest Coordinates
  nestCoords = fopen(nestCoordsFileName, "r");

  if (!nestCoords){
    Rcpp::stop("Could not open file %s.\n", nestCoordsFileName);
  }
  else{
    Rprintf("Opened file %s.\n", nestCoordsFileName);
  }

  Array2D<long> grid(xwidth,ywidth);
  for(long i=0;i<xwidth;i++){
    for(long j=0;j<ywidth;j++){
      grid(i,j) = 0;
    }
  }

  for (sample = 0; sample < SAMPLESIZE; sample++){
    float currentList[MAXNESTS];
    int numnests = readsample(nestCoords, currentList);

    Rprintf("Processing sample %d. numnests=%d\n",sample+1,numnests);

    int numboundarypoints;
    double *boundary = boundarypoints(currentList,numnests,&numboundarypoints,MINLEN);

    updategrid(boundary,numboundarypoints,grid,xstart,ystart,GRIDSIZE);
    free(boundary);
  }
  fclose(nestCoords);

  //Create polygons
  double threshvals[7] = {0.999,0.99,0.975,0.75,0.5,0.25,0.025};
  double *boundary[7];
  int boundarylen[7];
  float *list = (float*)malloc(2*xwidth*ywidth*sizeof(float));

  for(int poly=0;poly<7;poly++){
    int numboundarypoints = 0;
    int thresh = (int)floor((1.0-threshvals[poly])*SAMPLESIZE);
    for(long i=0;i<xwidth;i++){
      for(long j=0;j<ywidth;j++){
        if((grid(i,j) > thresh) && (i==0 || i==xwidth-1 || j==0 || j==ywidth-1 ||
           (grid(i-1,j) <= thresh) || (grid(i+1,j) <= thresh) ||
           (grid(i,j-1) <= thresh) || (grid(i,j+1) <= thresh))){
          list[2*numboundarypoints] = (float)(xstart + i*GRIDSIZE);
          list[2*numboundarypoints+1] = (float)(ystart + j*GRIDSIZE);
          numboundarypoints++;
        }
      }
    }

    boundary[poly] = boundarypoints(list,numboundarypoints,boundarylen+poly,MINLEN);
  }
  free(list);



  IntegerVector blen(7);
  for(int i=0;i<7;i++){
    blen[i] = boundarylen[i];
  }

  NumericVector poly999(2*boundarylen[0]);
  for(int i=0;i<2*boundarylen[0];i++){
    poly999[i] = boundary[0][i];
  }

  NumericVector poly990(2*boundarylen[1]);
  for(int i=0;i<2*boundarylen[1];i++){
    poly990[i] = boundary[1][i];
  }

  NumericVector poly975(2*boundarylen[2]);
  for(int i=0;i<2*boundarylen[2];i++){
    poly975[i] = boundary[2][i];
  }

  NumericVector poly750(2*boundarylen[3]);
  for(int i=0;i<2*boundarylen[3];i++){
    poly750[i] = boundary[3][i];
  }

  NumericVector poly500(2*boundarylen[4]);
  for(int i=0;i<2*boundarylen[4];i++){
    poly500[i] = boundary[4][i];
  }

  NumericVector poly250(2*boundarylen[5]);
  for(int i=0;i<2*boundarylen[5];i++){
    poly250[i] = boundary[5][i];
  }

  NumericVector poly025(2*boundarylen[6]);
  for(int i=0;i<2*boundarylen[6];i++){
    poly025[i] = boundary[6][i];
  }

  List polylist = List::create(Named("boundarylen") = blen,
      Named("boundary") = List::create(poly999,poly990,poly975,poly750,poly500,poly250,poly025));

  for(int poly=0;poly<7;poly++){
    free(boundary[poly]);
  }

  return(polylist);
}




