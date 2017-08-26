/*
This a driver to demonstrate fastlap as a callable procedure from c.
The driver illustrates how FastLap may be used for solving potential problems 
cast as the Green or single layer formulations.  An example field 
quantity computation is included as well.  
Input compatible with this driver can be generated by sphere.f if it is 
processed through c2fdata.  The former code generates the geometry for a 
sphere and supplies boundary conditions for the sphere translating in an
infinite fluid.  The Green formulation is set up as a mixed Neumann and 
Dirichlet problem as dictated by the input file.  The single layer formulation
is set up by setting the RHS vector to be the potential value supplied in 
the input file.  The exact solution (including the source strength) is 
known for this problem, so the error in the solution may be computed and
is reported by this driver.

Written in C by Korsmeyer, winter 1993, updated occasionally thereafter.  

                        Copyright (c) MIT 1993-1995.
*/
/* # ***** sort to /src/main
   # ***** */
#include <stdio.h>
#include <math.h>
#include "mulStruct.h"
#include "mulGlobal.h"
#ifndef _TIME_			/* if not on a Sun4 */
#include <time.h>
#endif
#define XI 0
#define YI 1
#define ZI 2
#define DIRICHLET 0
#define NEUMANN 1
#define DIMEN 3
#define VERTS 4
#define ONE3 0.3333333333333
#define Dot_Product(V1,V2) V1[XI]*V2[XI]+V1[YI]*V2[YI]+V1[ZI]*V2[ZI]
#define DotP_Product(V1,R,S,T) (V1[XI])*(R)+(V1[YI])*(S)+(V1[ZI])*(T)

main(argc, argv)
    int argc;
    char *argv[];
{
  FILE *stream, *fopen();
  char line[BUFSIZ], title[BUFSIZ], *delcr();
  int linecnt=0;
  char **chkp, *chk, infile[BUFSIZ], *ctime(), hostname[BUFSIZ];
  double strtod();
  double *exact_sol;
  int size, nlhs, nrhs, numMom, numLev, i, j;
  int cmderr = FALSE;
  long strtol(), clock;
  char *shapechar;
  double *x, *poten, *dbydnpoten, *xcoll, *xnrm, *lhsvect, *rhsvect;
  int *shape, *type, *dtype, *rhstype, *lhstype, *rhsindex, *lhsindex, job, fljob; 
  double error, max_diri=0., ave_diri=0., 
  max_neum=0., ave_neum=0.;
  double cnt_diri, cnt_neum; 
  /* Set the tolerance and max iterations for GMRES called by fastlap. */
  double tol = 0.0001;
  int maxit = 32;
  int numit;

  if(argc != 5) {
    fprintf(stderr,
	    "\ndriverc FE: Incorrect arg list.\nUsage: %s [-o<expansion order>] [-d<partitioning depth>] [-n<number of panels>] [<input file>]\n", argv[0]);
    exit(0);
  }

  chkp = &chk;			/* pointers for error checking */
  for(i = 1, stream = NULL; i < argc; i++) {
    if(argv[i][0] == '-') {
      if(argv[i][1] == 'o') {
	numMom = (int) strtol(&(argv[i][2]), chkp, 10);
      }
      else if(argv[i][1] == 'd') {
	numLev = (int) strtol(&(argv[i][2]), chkp, 10);
      }
      else if(argv[i][1] == 'n') {
	size = (int) strtol(&(argv[i][2]), chkp, 10);
      }
      else {
	fprintf(stderr, "\ndriverc FE: %s is an illegal option.\n",&(argv[i][1]));
	fprintf(stderr, "Usage: %s [-o<expansion order>] [-d<partitioning depth>] [-n<number of panels>] [<input file>]\n", argv[0]);
    exit(0);
	break;
      }
    }
    else {			/* Isn't an option, must be the input file. */
      if((stream = fopen(argv[i], "r")) == NULL) {
	fprintf(stderr, "\ndriverc FE: Can't open `%s' to read panel data.\n", 
		argv[i]);
	exit(0);
      }
      else sprintf(infile, "`%s'", argv[i]);
    }
  }

  /* Print driverc salutation with version and release date. */
  fprintf(stdout, "Running %s %.1f (%s)\n", 
	  argv[0], VERSION, RELEASE);

  time(&clock);
  fprintf(stdout, " Date: %s", ctime(&clock));
  if(gethostname(hostname, BUFSIZ) != -1)
      fprintf(stdout, " Host: %s\n", hostname);
  else fprintf(stdout, " Host: ? (gethostname() failure)\n");


  printf("  Expansion order selected: %i\n",numMom);
  printf("  Depth of tree selected: %i\n",numLev);

  /* Allocate space for the panel vertices and boundary conditions. */
  shape = (int*)calloc(size,sizeof(int));
  x = (double*)calloc(size*VERTS*DIMEN,sizeof(double));
  poten = (double*)calloc(size,sizeof(double));
  dbydnpoten = (double*)calloc(size,sizeof(double));
  type = (int*)calloc(size,sizeof(int));
  
  /* Allocate space for fastlap arg list vectors. */
  xcoll = (double*)calloc(size*DIMEN,sizeof(double));
  xnrm = (double*)calloc(size*DIMEN,sizeof(double));
  dtype = (int*)calloc(size,sizeof(int));
  lhsvect = (double*)calloc(size,sizeof(double));
  rhsvect = (double*)calloc(size,sizeof(double));
  rhstype = (int*)calloc(size,sizeof(int)); 
  lhstype = (int*)calloc(size,sizeof(int));
  rhsindex = (int*)calloc(size*VERTS,sizeof(int));
  lhsindex = (int*)calloc(size*VERTS,sizeof(int));

  /* Allocate space for the exact solution vector for error assessment. */
  exact_sol = (double*)calloc(size,sizeof(double));

  /* read in panel data from a file. */
  fgets(line, sizeof(line), stream);
  strcpy(title, delcr(&line[1]));  
  while(fgets(line, sizeof(line), stream) != NULL) {
    i = linecnt;
    if(linecnt > (size-1)) {
      fprintf(stderr, "\n More panels than asked for! \n");
      exit(0);
    }
    if(line[0] == 'Q' || line[0] == 'q') {
      shape[i] = QUADRILAT;
      if(sscanf(line,
                "%s %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %d",
                &shapechar,
		&x[i*VERTS*DIMEN],&x[i*VERTS*DIMEN+1],&x[i*VERTS*DIMEN+2],
		&x[i*VERTS*DIMEN+3],&x[i*VERTS*DIMEN+4],&x[i*VERTS*DIMEN+5],
		&x[i*VERTS*DIMEN+6],&x[i*VERTS*DIMEN+7],&x[i*VERTS*DIMEN+8],
		&x[i*VERTS*DIMEN+9],&x[i*VERTS*DIMEN+10],&x[i*VERTS*DIMEN+11],
		&poten[i],&dbydnpoten[i],&type[i])
         != 16) {
        fprintf(stderr, "Bad quad format, line %d:\n%s\n",
                linecnt, line);
        exit(0);
      }
    }
    else if(line[0] == 'T' || line[0] == 't') {
      shape[i] = TRIANGLE;
      if(sscanf(line,
                "%s %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %d",
                &shapechar,
		&x[i*VERTS*DIMEN],&x[i*VERTS*DIMEN+1],&x[i*VERTS*DIMEN+2],
		&x[i*VERTS*DIMEN+3],&x[i*VERTS*DIMEN+4],&x[i*VERTS*DIMEN+5],
		&x[i*VERTS*DIMEN+6],&x[i*VERTS*DIMEN+7],&x[i*VERTS*DIMEN+8],
		&poten[i],&dbydnpoten[i],&type[i])
         != 13) {
        fprintf(stderr, "Bad tri format, line %d:\n%s\n",
                linecnt, line);
        exit(0);
      }
    }
    linecnt++;
  }

  printf("  Data file title: %s\n",title);
  printf("  Lines read: %i\n",linecnt);
  size = linecnt;

  /* This is a Green formulation. */
  fljob = 1;
  /* Set up for the fastlap call and save the exact solution for comparison with the 
     computed solution.  Note that recovery of the correct signs for Green's Thm. are 
     obtained by kidding fastlap about the signs on the lhs and rhs vectors. */
  for(i=0; i<size; i++) {
    if(type[i] == DIRICHLET) {
      rhstype[i] = CONSTANT_DIPOLE;
      lhstype[i] = CONSTANT_SOURCE;
      exact_sol[i] = dbydnpoten[i];
      rhsvect[i] = -poten[i];
      rhsindex[i*VERTS] = i;
      lhsindex[i*VERTS] = i;
      Dcentroid(shape[i], &x[i*VERTS*DIMEN], &xcoll[i*DIMEN]);
      /* fprintf(stdout, "Panel:%d    Centroid:%.8g %.8g %.8g\n",i, xcoll[i*DIMEN],xcoll[i*DIMEN+1],xcoll[i*DIMEN+2]); */
    }
    else if(type[i] == NEUMANN) {
      rhstype[i] = CONSTANT_SOURCE;
      lhstype[i] = CONSTANT_DIPOLE;
      exact_sol[i] = poten[i];
      rhsvect[i] = dbydnpoten[i];
      rhsindex[i*VERTS] = i;
      lhsindex[i*VERTS] = i;
      Dcentroid(shape[i], &x[i*VERTS*DIMEN], &xcoll[i*DIMEN]);
      /* fprintf(stdout, "Panel:%d    Centroid:%.8g %.8g %.8g\n",i, xcoll[i*DIMEN],xcoll[i*DIMEN+1],xcoll[i*DIMEN+2]); */
    }
    else {
      printf("driverc FE: You're missing a boundary condition type");
      exit(0);
    }
  }
  numit = fastlap(&size,&size,&size,x,shape,dtype,lhstype,rhstype,lhsindex,rhsindex,lhsvect,rhsvect,xcoll,xnrm,&numLev,&numMom,&maxit,&tol,&fljob);

  fprintf(stdout, "\n\n %d iterations knocked down residual to:%.8g\n",
	  numit, tol);
  /* Compute the average and maximum errors on the Neumann and Dirichlet
     surfaces. Note again, the sign manipulation. */
  for(i=0;i<size;i++) {
    if(type[i] == DIRICHLET) {
      lhsvect[i] = -lhsvect[i];
      error = sqrt((exact_sol[i] - lhsvect[i])
		   *(exact_sol[i] - lhsvect[i]));
      max_diri = MAX(max_diri,error);
      ave_diri += error;
      cnt_diri += 1.;
      /* fprintf(stdout, "Panel:%d  exact, computed:%.8g %.8g \n",i,exact_sol[i],lhsvect[i]); */
    }
    else if(type[i] == NEUMANN) {
      error = sqrt((exact_sol[i] - lhsvect[i])
		   *(exact_sol[i] - lhsvect[i]));
      max_neum = MAX(max_neum,error);
      ave_neum += error;
      cnt_neum += 1.;
      /* fprintf(stdout, "Panel:%d  exact, computed:%.8g %.8g \n",i,exact_sol[i],lhsvect[i]); */
    }
  }
  if(cnt_diri != 0) {
      ave_diri /= cnt_diri;
      fprintf(stdout, "\nAverage absolute error on Dirichlet surface =%.8g\n",
	      ave_diri);
      fprintf(stdout, "Maximum absolute error on Dirichlet surface =%.8g\n",
	      max_diri);
  }
  if(cnt_neum != 0) {
      ave_neum /= cnt_neum;
      fprintf(stdout, "\nAverage absolute error on Neumann surface =%.8g\n",
	      ave_neum);
      fprintf(stdout, "Maximum absolute error on Neumann surface =%.8g\n",
	      max_neum);
  }
  return;
}

/*
Makes 1st \n in a string = \0 and then deletes all trail/leading white space.
*/
char *delcr(str)
char *str;
{
  int i, j, k;
  for(k = 0; str[k] != '\0'; k++) if(str[k] == '\n') { str[k] = '\0'; break; }
  for(i = 0; str[i] == ' ' || str[i] == '\t'; i++); /* count leading spaces */
  if(i > 0) {
    for(j = 0; str[j+i] != '\0'; j++) str[j] = str[j+i];
    str[j] = '\0';
  }
  for(k--; str[k] == ' ' || str[k] == '\t'; k--) str[k] = '\0';
  return(str);
}

Dcentroid(shape, pc, xcout)
double *pc, *xcout;
int shape;
{
  double corner[4][3], X[3], Y[3], Z[3], vertex1[3], vertex3[3];
  double sum, delta, dl, x1, y1, x2, x3, y3, xc, yc;
  int i, j;
  double normalize();
  /* Load the corners. */
  for(i=0; i<4; i++) { 
      for(j=0; j<3; j++) { 
	  corner[i][j] = *(pc++);
      }
  }

  /* Use vertex 0 as the origin and get diags and lengths. */
  for(sum=0, i=0; i<3; i++) {
    X[i] = delta = corner[2][i] - corner[0][i];
    sum += delta * delta;
    vertex1[i] = corner[1][i] - corner[0][i];
    if(shape == QUADRILAT) {
      vertex3[i] = corner[3][i] - corner[0][i];
      Y[i] = corner[1][i] - corner[3][i];
    }
    else if(shape == TRIANGLE) {
      vertex3[i] = corner[2][i] - corner[0][i];
      Y[i] = corner[1][i] - corner[0][i];
    }
    else {
      printf("Dcentroid FE: Shape indicator is neither triangle nor quadrilateral");
      exit(0);
    }
  }
  x2 = sqrt(sum);

  /* Z-axis is normal to two diags. */
  Cross_Product(X, Y, Z);
  normalize(X);
  normalize(Z);

  /* Real Y-axis is normal to X and Z. */
  Cross_Product(Z, X, Y);

  /* Project into the panel axes. */
  y1 = Dot_Product(vertex1, Y);
  y3 = Dot_Product(vertex3, Y);
  x1 = Dot_Product(vertex1, X);
  x3 = Dot_Product(vertex3, X);

  yc = ONE3 * (y1 + y3);
  xc = ONE3 * (x2 + ((x1 * y1 - x3 * y3)/(y1 - y3)));

  *(xcout+0) = corner[0][XI] + xc * X[XI] + yc * Y[XI];
  *(xcout+1) = corner[0][YI] + xc * X[YI] + yc * Y[YI];
  *(xcout+2) = corner[0][ZI] + xc * X[ZI] + yc * Y[ZI];
}








