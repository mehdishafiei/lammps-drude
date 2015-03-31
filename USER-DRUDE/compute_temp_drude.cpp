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

#include "mpi.h"
#include "stdlib.h"
#include "string.h"
#include "compute_temp_drude.h"
#include "atom.h"
#include "update.h"
#include "force.h"
#include "group.h"
#include "modify.h"
#include "fix.h"
#include "domain.h"
#include "lattice.h"
#include "memory.h"
#include "error.h"

using namespace LAMMPS_NS;

/* ---------------------------------------------------------------------- */

ComputeTempDrude::ComputeTempDrude(LAMMPS *lmp, int narg, char **arg) :
  Compute(lmp, narg, arg)
{
  if (narg != 3) error->all(FLERR,"Illegal compute temp command");

  scalar_flag = vector_flag = 1;
  size_vector = 6;
  extscalar = 0;
  extvector = 1;
  tempflag = 1;
  tempbias = 1;

  vector = new double[6];
  maxatom = 0;
  vbiasall = NULL;
}

/* ---------------------------------------------------------------------- */

ComputeTempDrude::~ComputeTempDrude()
{
  delete [] vector;
  memory->destroy(vbiasall);
}

/* ---------------------------------------------------------------------- */

void ComputeTempDrude::init()
{}

/* ---------------------------------------------------------------------- */

void ComputeTempDrude::setup()
{
  fix_dof = 0;
  for (int i = 0; i < modify->nfix; i++)
    fix_dof += modify->fix[i]->dof(igroup);
  dof_compute();
}

/* ---------------------------------------------------------------------- */

void ComputeTempDrude::dof_compute()
{
  double natoms = group->count(igroup);
  int nper = domain->dimension;
  dof = nper * natoms;
  dof -= extra_dof + fix_dof;
  if (dof > 0) tfactor = force->mvv2e / (dof * force->boltz);
  else tfactor = 0.0;
}

/* ---------------------------------------------------------------------- */

double ComputeTempDrude::compute_scalar()
{
  char idtag[] = "drudeid";
  double vthermal[3];

  if (atom->nlocal > maxatom) {
    maxatom = atom->nmax;
    memory->destroy(vbiasall);
    memory->create(vbiasall,maxatom,3,"temp/profile:vbiasall");
  }

  invoked_scalar = update->ntimestep;

  double **v = atom->v;
  double *mass = atom->mass;
  double *rmass = atom->rmass;
  int *type = atom->type;
  int *mask = atom->mask;
  int nlocal = atom->nlocal;
  int flag;
  int index = atom->find_custom(idtag, flag); 
  if (index == -1)
    error->all(FLERR,"Unable to get DRUDEID atom property");
  int *drudeid = atom->ivector[index];

  double t = 0.0;
  for (int i = 0; i < nlocal; i++)
    if (mask[i] & groupbit) {
      int icoeur = atom->map(drudeid[i]);
      vbiasall[i][0] = atom->v[icoeur][0];
      vbiasall[i][1] = atom->v[icoeur][1];
      vbiasall[i][2] = atom->v[icoeur][2];
      vthermal[0] = v[i][0] - vbiasall[i][0];
      vthermal[1] = v[i][1] - vbiasall[i][1];
      vthermal[2] = v[i][2] - vbiasall[i][2];
      if (rmass)
        t += (vthermal[0]*vthermal[0] + vthermal[1]*vthermal[1] +
              vthermal[2]*vthermal[2]) * rmass[i];
      else
        t += (vthermal[0]*vthermal[0] + vthermal[1]*vthermal[1] +
              vthermal[2]*vthermal[2]) * mass[type[i]];
    }

  MPI_Allreduce(&t,&scalar,1,MPI_DOUBLE,MPI_SUM,world);
  if (dynamic) dof_compute();
  scalar *= tfactor;
  return scalar;
}

/* ---------------------------------------------------------------------- */

void ComputeTempDrude::compute_vector()
{
  char idtag[] = "drudeid";
  int i;
  double vthermal[3];

  if (atom->nlocal > maxatom) {
    maxatom = atom->nmax;
    memory->destroy(vbiasall);
    memory->create(vbiasall,maxatom,3,"temp/profile:vbiasall");
  }

  invoked_vector = update->ntimestep;

  double **v = atom->v;
  double *mass = atom->mass;
  double *rmass = atom->rmass;
  int *type = atom->type;
  int *mask = atom->mask;
  int nlocal = atom->nlocal;
  int flag;
  int index = atom->find_custom(idtag, flag); 
  if (index == -1)
    error->all(FLERR,"Unable to get DRUDEID atom property");
  int *drudeid = atom->ivector[index];

  double massone,t[6];
  for (i = 0; i < 6; i++) t[i] = 0.0;

  for (i = 0; i < nlocal; i++)
    if (mask[i] & groupbit) {
      int icoeur = atom->map(drudeid[i]);
      vbiasall[i][0] = atom->v[icoeur][0];
      vbiasall[i][1] = atom->v[icoeur][1];
      vbiasall[i][2] = atom->v[icoeur][2];
      vthermal[0] = v[i][0] - vbiasall[i][0];
      vthermal[1] = v[i][1] - vbiasall[i][1];
      vthermal[2] = v[i][2] - vbiasall[i][2];

      if (rmass) massone = rmass[i];
      else massone = mass[type[i]];
      t[0] += massone * vthermal[0]*vthermal[0];
      t[1] += massone * vthermal[1]*vthermal[1];
      t[2] += massone * vthermal[2]*vthermal[2];
      t[3] += massone * vthermal[0]*vthermal[1];
      t[4] += massone * vthermal[0]*vthermal[2];
      t[5] += massone * vthermal[1]*vthermal[2];
    }

  MPI_Allreduce(t,vector,6,MPI_DOUBLE,MPI_SUM,world);
  for (i = 0; i < 6; i++) vector[i] *= force->mvv2e;
}

/* ----------------------------------------------------------------------
   remove velocity bias from atom I to leave thermal velocity
------------------------------------------------------------------------- */

void ComputeTempDrude::remove_bias(int i, double *v)
{
  v[0] -= vbiasall[i][0];
  v[1] -= vbiasall[i][1];
  v[2] -= vbiasall[i][2];
}

/* ----------------------------------------------------------------------
   remove velocity bias from all atoms to leave thermal velocity
------------------------------------------------------------------------- */

void ComputeTempDrude::remove_bias_all()
{
  double **v = atom->v;
  int *mask = atom->mask;
  int nlocal = atom->nlocal;

  for (int i = 0; i < nlocal; i++)
    if (mask[i] & groupbit) {
      v[i][0] -= vbiasall[i][0];
      v[i][1] -= vbiasall[i][1];
      v[i][2] -= vbiasall[i][2];
    }
}

/* ----------------------------------------------------------------------
   add back in velocity bias to atom I removed by remove_bias()
   assume remove_bias() was previously called
------------------------------------------------------------------------- */

void ComputeTempDrude::restore_bias(int i, double *v)
{
  v[0] += vbiasall[i][0];
  v[1] += vbiasall[i][1];
  v[2] += vbiasall[i][2];
}

/* ----------------------------------------------------------------------
   add back in velocity bias to all atoms removed by remove_bias_all()
   assume remove_bias_all() was previously called
------------------------------------------------------------------------- */

void ComputeTempDrude::restore_bias_all()
{
  double **v = atom->v;
  int *mask = atom->mask;
  int nlocal = atom->nlocal;

  for (int i = 0; i < nlocal; i++)
    if (mask[i] & groupbit) {
      v[i][0] += vbiasall[i][0];
      v[i][1] += vbiasall[i][1];
      v[i][2] += vbiasall[i][2];
    }
}