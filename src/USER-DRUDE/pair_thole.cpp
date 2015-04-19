/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#include "math.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "pair_thole.h"
#include "atom.h"
#include "comm.h"
#include "force.h"
#include "neighbor.h"
#include "neigh_list.h"
#include "memory.h"
#include "error.h"
#include "fix.h"
#include "fix_store.h"

using namespace LAMMPS_NS;

/* ---------------------------------------------------------------------- */

PairThole::PairThole(LAMMPS *lmp) : Pair(lmp) {}

/* ---------------------------------------------------------------------- */

PairThole::~PairThole()
{
  if (allocated) {
    memory->destroy(setflag);
    memory->destroy(cutsq);
    memory->destroy(polar);
    memory->destroy(thole);
    memory->destroy(cut);
    memory->destroy(scale);
  }
}

/* ---------------------------------------------------------------------- */

void PairThole::compute(int eflag, int vflag)
{
  int i,j,ii,jj,inum,jnum,itype,jtype;
  double qi,qj,xtmp,ytmp,ztmp,delx,dely,delz,ecoul,fpair;
  double r,rsq,r2inv,rinv,forcecoul,factor_coul;
  int *ilist,*jlist,*numneigh,**firstneigh;
  double factor_f,factor_e,a_screen;
  int di,dj;

  ecoul = 0.0;
  if (eflag || vflag) ev_setup(eflag,vflag);
  else evflag = vflag_fdotr = 0;

  double **x = atom->x;
  double **f = atom->f;
  double *q = atom->q;
  int *type = atom->type;
  int nlocal = atom->nlocal;
  double *special_coul = force->special_coul;
  int newton_pair = force->newton_pair;
  double qqrd2e = force->qqrd2e;
  int *drudetype = atom->drudetype; //ivector[index_drudetype];
  tagint *drudeid = atom->drudeid; //ivector[index_drudeid];

  inum = list->inum;
  ilist = list->ilist;
  numneigh = list->numneigh;
  firstneigh = list->firstneigh;

  // loop over neighbors of my atoms

  for (ii = 0; ii < inum; ii++) {
    i = ilist[ii];

    // only on core-drude pair
    if (!drudetype[type[i]])
      continue;

    di = domain->closest_image(i, atom->map(drudeid[i]));
    // get dq of the core via the drude charge
    if (drudetype[type[i]] == 2)
      qi = q[i];
    else
      qi = -q[di];

    xtmp = x[i][0];
    ytmp = x[i][1];
    ztmp = x[i][2];
    itype = type[i];
    jlist = firstneigh[i];
    jnum = numneigh[i];

    for (jj = 0; jj < jnum; jj++) {
      j = jlist[jj];
      factor_coul = special_coul[sbmask(j)];
      j &= NEIGHMASK;

      // only on core-drude pair, but not into the same pair
      if (!drudetype[type[j]] || j == di)
        continue;

      // get dq of the core via the drude charge
      if (drudetype[type[j]] == 2)
        qj = q[j];
      else {
        dj = domain->closest_image(j, atom->map(drudeid[j]));
        qj = -q[dj];
      }

      delx = xtmp - x[j][0];
      dely = ytmp - x[j][1];
      delz = ztmp - x[j][2];
      rsq = delx*delx + dely*dely + delz*delz;
      jtype = type[j];

      if (rsq < cutsq[itype][jtype]) {
        r2inv = 1.0/rsq;
        rinv = sqrt(r2inv);

        r = sqrt(rsq);
        a_screen = thole[itype][jtype] / pow(polar[itype][jtype], 1./3.);
        factor_f = 0.5*(2 + (exp(-a_screen * r) * (-2 - a_screen*r * (2 + a_screen*r)))) - factor_coul;
        factor_e = 0.5*(2 - (exp(-a_screen * r) * (2 + a_screen*r))) - factor_coul;
        forcecoul = qqrd2e * scale[itype][jtype] * qi*qj*rinv;
        fpair = factor_f*forcecoul * r2inv;

        f[i][0] += delx*fpair;
        f[i][1] += dely*fpair;
        f[i][2] += delz*fpair;
        if (newton_pair || j < nlocal) {
          f[j][0] -= delx*fpair;
          f[j][1] -= dely*fpair;
          f[j][2] -= delz*fpair;
        }

        if (eflag)
          ecoul = factor_e * qqrd2e * scale[itype][jtype] * qi*qj*rinv;

        if (evflag) ev_tally(i,j,nlocal,newton_pair,
                             0.0,ecoul,fpair,delx,dely,delz);
      }
    }
  }

  if (vflag_fdotr) virial_fdotr_compute();
}

/* ----------------------------------------------------------------------
   allocate all arrays
------------------------------------------------------------------------- */

void PairThole::allocate()
{
  allocated = 1;
  int n = atom->ntypes;

  memory->create(setflag,n+1,n+1,"pair:setflag");
  for (int i = 1; i <= n; i++)
    for (int j = i; j <= n; j++)
      setflag[i][j] = 0;

  memory->create(cutsq,n+1,n+1,"pair:cutsq");

  memory->create(cut,n+1,n+1,"pair:cut");
  memory->create(scale,n+1,n+1,"pair:scale");
  memory->create(thole,n+1,n+1,"pair:thole");
  memory->create(polar,n+1,n+1,"pair:polar");
}

/* ----------------------------------------------------------------------
   global settings
------------------------------------------------------------------------- */

void PairThole::settings(int narg, char **arg)
{
  if (narg != 2) error->all(FLERR,"Illegal pair_style command");

  thole_global = force->numeric(FLERR,arg[0]);
  cut_global = force->numeric(FLERR,arg[1]);

  // reset cutoffs that have been explicitly set

  if (allocated) {
    int i,j;
    for (i = 1; i <= atom->ntypes; i++)
      for (j = i+1; j <= atom->ntypes; j++)
          if (setflag[i][j]) {
              thole[i][j] = thole_global;
              cut[i][j] = cut_global;
          }
  }
}

/* ----------------------------------------------------------------------
   set coeffs for one or more type pairs
------------------------------------------------------------------------- */

void PairThole::coeff(int narg, char **arg)
{
  if (narg < 3 || narg > 5) 
    error->all(FLERR,"Incorrect args for pair coefficients");
  if (!allocated) allocate();

  int ilo,ihi,jlo,jhi;
  force->bounds(arg[0],atom->ntypes,ilo,ihi);
  force->bounds(arg[1],atom->ntypes,jlo,jhi);

  double polar_one = force->numeric(FLERR,arg[2]);
  double thole_one = thole_global; 
  double cut_one = cut_global;
  if (narg >=4) thole_one = force->numeric(FLERR,arg[3]);
  if (narg == 5) cut_one = force->numeric(FLERR,arg[4]);

  int count = 0;
  for (int i = ilo; i <= ihi; i++) {
    for (int j = MAX(jlo,i); j <= jhi; j++) {
      polar[i][j] = polar_one;
      thole[i][j] = thole_one;
      cut[i][j] = cut_one;
      scale[i][j] = 1.0;
      setflag[i][j] = 1;
      count++;
    }
  }

  if (count == 0) error->all(FLERR,"Incorrect args for pair coefficients");
}


/* ----------------------------------------------------------------------
   init specific to this pair style
------------------------------------------------------------------------- */

void PairThole::init_style()
{
  if (!atom->q_flag)
    error->all(FLERR,"Pair style thole requires atom attribute q");

  /*char typetag[] = "drudetype", idtag[] = "drudeid";
  int dummy;
  index_drudetype = atom->find_custom(typetag, dummy);
  if (index_drudetype == -1)
    error->all(FLERR,"Unable to get DRUDETYPE atom property");

  index_drudeid = atom->find_custom(idtag, dummy);
  if (index_drudeid == -1) 
    error->all(FLERR,"Unable to get DRUDEID atom property");*/

  neighbor->request(this,instance_me);
}

/* ----------------------------------------------------------------------
   init for one type pair i,j and corresponding j,i
------------------------------------------------------------------------- */

double PairThole::init_one(int i, int j)
{
  if (setflag[i][j] == 0)
    cut[i][j] = mix_distance(cut[i][i],cut[j][j]);

  polar[j][i] = polar[i][j];
  thole[j][i] = thole[i][j];
  scale[j][i] = scale[i][j];

  return cut[i][j];
}

/* ----------------------------------------------------------------------
  proc 0 writes to restart file
------------------------------------------------------------------------- */

void PairThole::write_restart(FILE *fp)
{
  write_restart_settings(fp);

  int i,j;
  for (i = 1; i <= atom->ntypes; i++)
    for (j = i; j <= atom->ntypes; j++) {
      fwrite(&setflag[i][j],sizeof(int),1,fp);
      if (setflag[i][j]) {
        fwrite(&polar[i][j],sizeof(double),1,fp);
        fwrite(&thole[i][j],sizeof(double),1,fp);
        fwrite(&cut[i][j],sizeof(double),1,fp);
      }
    }
}

/* ----------------------------------------------------------------------
  proc 0 reads from restart file, bcasts
------------------------------------------------------------------------- */

void PairThole::read_restart(FILE *fp)
{
  read_restart_settings(fp);
  allocate();

  int i,j;
  int me = comm->me;
  for (i = 1; i <= atom->ntypes; i++)
    for (j = i; j <= atom->ntypes; j++) {
      if (me == 0) fread(&setflag[i][j],sizeof(int),1,fp);
      MPI_Bcast(&setflag[i][j],1,MPI_INT,0,world);
      if (setflag[i][j]) {
          if (me == 0) {
            fread(&polar[i][j],sizeof(double),1,fp);
            fread(&thole[i][j],sizeof(double),1,fp);
            fread(&cut[i][j],sizeof(double),1,fp);
          }
        MPI_Bcast(&polar[i][j],1,MPI_DOUBLE,0,world);
        MPI_Bcast(&thole[i][j],1,MPI_DOUBLE,0,world);
        MPI_Bcast(&cut[i][j],1,MPI_DOUBLE,0,world);
      }
    }
}

/* ----------------------------------------------------------------------
  proc 0 writes to restart file
------------------------------------------------------------------------- */

void PairThole::write_restart_settings(FILE *fp)
{
  fwrite(&thole_global,sizeof(double),1,fp);
  fwrite(&cut_global,sizeof(double),1,fp);
  fwrite(&offset_flag,sizeof(int),1,fp);
  fwrite(&mix_flag,sizeof(int),1,fp);
}

/* ----------------------------------------------------------------------
  proc 0 reads from restart file, bcasts
------------------------------------------------------------------------- */

void PairThole::read_restart_settings(FILE *fp)
{
  if (comm->me == 0) {
    fread(&thole_global,sizeof(double),1,fp);
    fread(&cut_global,sizeof(double),1,fp);
    fread(&offset_flag,sizeof(int),1,fp);
    fread(&mix_flag,sizeof(int),1,fp);
  }
  MPI_Bcast(&thole_global,1,MPI_DOUBLE,0,world);
  MPI_Bcast(&cut_global,1,MPI_DOUBLE,0,world);
  MPI_Bcast(&offset_flag,1,MPI_INT,0,world);
  MPI_Bcast(&mix_flag,1,MPI_INT,0,world);
}

/* ---------------------------------------------------------------------- */

double PairThole::single(int i, int j, int itype, int jtype,
                         double rsq, double factor_coul, double factor_lj,
                         double &fforce)
{
  double r2inv,rinv,r,forcecoul,phicoul;
  double qi,qj,factor_f,factor_e,a_screen;
  int di, dj;

  int *drudetype = atom->drudetype; //ivector[index_drudetype];
  tagint *drudeid = atom->drudeid; //ivector[index_drudeid];
  int *type = atom->type;

  // only on core-drude pair, but not on the same pair
  if (!drudetype[type[i]] || !drudetype[type[j]] || j == i)
    return 0.0;

  // get dq of the core via the drude charge
  if (drudetype[type[i]] == 2)
    qi = atom->q[i];
  else {
    di = domain->closest_image(i, atom->map(drudeid[i]));
    qi = -atom->q[di];
  }
  if (drudetype[type[j]] == 2)
    qj = atom->q[j];
  else {
    dj = domain->closest_image(j, atom->map(drudeid[j]));
    qj = -atom->q[dj];
  }

  r2inv = 1.0/rsq;
  if (rsq < cutsq[itype][jtype]) {
    rinv = sqrt(r2inv);
    r = sqrt(rsq);
    a_screen = thole[itype][jtype] / pow(polar[itype][jtype], 1./3.);
    factor_f = 0.5*(2 + (exp(-a_screen * r) *
                         (-2 - a_screen*r * (2 + a_screen*r)))) - factor_coul;
    forcecoul = force->qqrd2e * qi*qj*rinv;
  } else forcecoul= 0.0;
  fforce = factor_f*forcecoul * r2inv;

  if (rsq < cutsq[itype][jtype]) {
    factor_e = 0.5*(2 - (exp(-a_screen * r) * (2 + a_screen*r))) - factor_coul;
    phicoul = factor_e * force->qqrd2e * qi*qj*rinv;
  } else phicoul = 0.0;

  return phicoul;
}

/* ---------------------------------------------------------------------- */

void *PairThole::extract(const char *str, int &dim)
{
  dim = 2;
  if (strcmp(str,"scale") == 0) return (void *) scale;
  if (strcmp(str,"polar") == 0) return (void *) polar;
  if (strcmp(str,"thole") == 0) return (void *) thole;
  return NULL;
}