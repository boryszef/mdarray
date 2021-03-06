/***************************************************************************

    mdarray

    Python module for manipulation of atomic coordinates
    Copyright (C) 2012, Borys Szefczyk

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

 ***************************************************************************/

#ifndef __TRAJECTORY_H__
#define __TRAJECTORY_H__

/* This should come before other Numpy-related declarations in every *
 * file that does not define the module's init function              */
#define NO_IMPORT_ARRAY

/* Make sure the general declarations are made first */
#include "mdarray.h"

typedef enum __moldenStyle {
	MLATOMS, MLGEOM, MLFREQ, MLUNK } MoldenStyle;

typedef struct __moldenSection {
		long offset;
		char name[20];
	} MoldenSection;
#define MAX_MOLDEN_SECTIONS 50

typedef struct {

	PyObject_HEAD

	enum { GUESS, XYZ, MOLDEN, GRO, XTC } type;
	enum { ANGS, BOHR, NM } units;
	char mode;
	char *fileName; /* Used while opening the file and for __repr__ */
	FILE *fd;
	/* Used for keeping track of the position in the file while reading     *
	 * frames. Two variables are needed, because some formats, like Molden, *
	 * store geometries and energies in different parts of the file.        */
#ifdef HAVE_GROMACS
	t_fileio *xd;
	rvec *xtcCoord;
#endif
	long filePosition1;
	long filePosition2;
	MoldenStyle moldenStyle;
	int nAtoms;
	int lastFrame;
	PyObject *symbols; /* list of symbols */
	PyObject *aNumbers; /* atomic numbers */
	PyObject *resids; /* residue numbers */
	PyObject *resNames; /* residue names */
	PyObject *masses; /* atomic Masses */

	/* Sections in Molden file and offsets */
	MoldenSection moldenSect[MAX_MOLDEN_SECTIONS];

} Trajectory;

#define MLSEC_ATOMS       0
#define MLSEC_GEOCONV     1
#define MLSEC_GEOMETRIES  2
#define MLSEC_FREQ        3
#define MLSEC_FR_COORD    4

static int read_topo_from_xyz(Trajectory *self);
static int read_topo_from_gro(Trajectory *self);
static PyObject *read_frame_from_xyz(Trajectory *self, int doWrap, ARRAY_REAL *box);
static int write_frame_to_xyz(Trajectory *self, PyObject *py_coords, char *comment);

static PyObject *read_frame_from_gro(Trajectory *self, int doWrap, ARRAY_REAL *box);
static int write_frame_to_gro(Trajectory *self, PyObject *py_coords,
				PyObject *py_vel, PyObject *py_box, char *comment);
#ifdef HAVE_GROMACS
static PyObject *read_frame_from_xtc(Trajectory *self, int doWrap, ARRAY_REAL *box);
#endif

static int read_molden_sections(Trajectory *self);
static int get_section_idx(Trajectory *self, const char name[]);
static int read_topo_from_molden(Trajectory *self);
//static PyObject *read_frame_from_molden_atoms(Trajectory *self);
//static PyObject *read_frame_from_molden_geometries(Trajectory *self);

#endif /* __TRAJECTORY_H__ */
