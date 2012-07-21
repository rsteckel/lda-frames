#ifndef _SAMPLIB_H
#define _SAMPLIB_H
double gammaln(double x);
double digamma(double x);
double trigamma(double x);
double invdigamma(double x);

double gengam(double a,double r);
void advnst(long k);
double genbet(double aa,double bb);
double genchi(double df);
double genexp(double av);
double genf(double dfn, double dfd);
void genmn(double *parm,double *x,double *work);
void genmul(long n,double *p,long ncat,long *ix);
double gennch(double df,double xnonc);
double gennf(double dfn, double dfd, double xnonc);
double gennor(double av,double sd);
void genprm(long *iarray,int larray);
double genunf(double low,double high);
void getsd(long *iseed1,long *iseed2);
void gscgn(long getset,long *g);
long ignbin(long n,double pp);
long ignnbn(long n,double p);
long ignlgi(void);
long ignpoi(double mu);
long ignuin(long low,long high);
void initgn(long isdtyp);
long mltmod(long a,long s,long m);
void phrtsd(char* phrase,long* seed1,long* seed2);
double ranf(void);
void setall(long iseed1,long iseed2);
void setant(long qvalue);
void setgmn(double *meanv,double *covm,long p,double *parm);
void setsd(long iseed1,long iseed2);
double sexpo(void);
double sgamma(double a);
double snorm(void);
#endif