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


#include <unistd.h>
#include "trajectory.h"
#include "utils.h"
#include "periodic_table.h"
#include "measure.h"





/* Method definitions */

static void Trajectory_dealloc(Trajectory* self)
{
    PyObject *tmp;

    tmp = self->symbols;
    self->symbols = NULL;
    Py_XDECREF(tmp);

    tmp = self->resids;
    self->resids = NULL;
    Py_XDECREF(tmp);

    tmp = self->resNames;
    self->resNames = NULL;
    Py_XDECREF(tmp);

    tmp = self->aNumbers;
    self->aNumbers = NULL;
    Py_XDECREF(tmp);

    tmp = self->masses;
    self->masses = NULL;
    Py_XDECREF(tmp);

    free(self->fileName);
    switch(self->type) {
        case XYZ:
        case MOLDEN:
        case GRO:
            if (self->fd != NULL) fclose(self->fd);
            break;
#ifdef HAVE_GROMACS
        case XTC:
            if (self->xd != NULL) close_xtc(self->xd);
            break;
#endif
        case GUESS:
        default:
            break;
    }
#ifdef HAVE_GROMACS
    sfree(self->xtcCoord);
#endif
    Py_TYPE(self)->tp_free((PyObject*)self);
}




static PyObject *Trajectory_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    Trajectory *self;

    self = (Trajectory *)type->tp_alloc(type, 0);
    if (self != NULL) {

        self->type = GUESS;
        self->units = ANGS;
        self->mode = 'r';
        self->fileName = NULL;
        self->fd = NULL;
#ifdef HAVE_GROMACS
        self->xd = NULL;
        self->xtcCoord = NULL;
#endif
        self->filePosition1 = -1;
        self->filePosition2 = -1;
        self->moldenStyle = MLUNK; // Unknown format
        self->nAtoms = 0;
        self->lastFrame = -1;

        Py_INCREF(Py_None);
        self->symbols = Py_None;
        
        Py_INCREF(Py_None);
        self->aNumbers = Py_None;
        
        Py_INCREF(Py_None);
        self->masses = Py_None;
        
        Py_INCREF(Py_None);
        self->resids = Py_None;
        
        Py_INCREF(Py_None);
        self->resNames = Py_None;
        
    }

    return (PyObject *)self;
}




/* This method should just guess the type, open the file and collect     *
 * general data like number of atoms and list of symbols. Actual reading *
 * of coordinates should go into read method                             */

static int Trajectory_init(Trajectory *self, PyObject *args, PyObject *kwds) {

    const char *filename;
    FILE *test;
    char *str_type = NULL;
    char ext[5];
    char *line = NULL;
	 size_t buflen = 0;
    char *mode = NULL;
    char *units = NULL;
#ifdef HAVE_GROMACS
    int step;
    real time, prec;
    matrix box;
    gmx_bool bOK;
#endif

	PyObject *py_sym = NULL;
	PyObject *py_resid = NULL;
	PyObject *py_resn = NULL;;

    static char *kwlist[] = {
        "filename", "mode", "symbols", "resids", "resnames",
        "format", "units",
        NULL };

    if(!PyArg_ParseTupleAndKeywords(args, kwds, "s|sO!O!O!ss", kwlist,
            &filename, &mode,
            &PyList_Type, &py_sym,
            &PyArray_Type, &py_resid,
            &PyList_Type, &py_resn,
            &str_type, &units))
        return -1;

    self->fileName = (char*) malloc((strlen(filename)+1) * sizeof(char));
    strcpy(self->fileName, filename);
    if (mode == NULL || mode[0] == 'r')
        self->mode = 'r';
    else if (mode[0] == 'w')
        self->mode = 'w';
    else if (mode[0] == 'a')
        self->mode = 'a';
    else {
        PyErr_SetString(PyExc_ValueError, "Incorrect mode");
        return -1;
    }

    /* Set the enum symbol of the file format */
    if ( str_type != NULL ) {
        if      ( !strcmp(str_type,    "XYZ") ) self->type = XYZ;
        else if ( !strcmp(str_type, "MOLDEN") ) self->type = MOLDEN;
        else if ( !strcmp(str_type,    "GRO") ) self->type = GRO;
        else if ( !strcmp(str_type,    "XTC") ) self->type = XTC;
        else if ( !strcmp(str_type,  "GUESS") ) self->type = GUESS;
		else {
	        PyErr_SetString(PyExc_ValueError, "Incorrect format specification");
    	    return -1;
		}
    }

    /* Guess the file format, if not given explicitly */
    if ( self->type == GUESS ) {
        strcpy(ext, filename + strlen(filename) - 4);
        if      ( !strcmp(ext, ".xyz") ) self->type = XYZ;
        else if ( !strcmp(ext, ".gro") ) self->type = GRO;
        else if ( !strcmp(ext, ".xtc") ) self->type = XTC;
        else if (self->mode == 'r' || self->mode == 'a') {
            /* Extract the first line */
            if ( (test = fopen(filename, "r")) == NULL ) {
                PyErr_SetFromErrno(PyExc_IOError);
                return -1; }
            if ( _getline(&line, &buflen, test) == -1 ) return -1;
            make_lowercase(line);
            stripline(line);
            fclose(test);

            /* Perhaps it's Molden format? */
            if ( !strcmp(line, "[molden format]") ) self->type = MOLDEN;

            free(line);
				line == NULL;
			}
    }
    if ( self->type == GUESS ) {
            PyErr_SetString(PyExc_RuntimeError, "Could not guess file format");
            return -1;
        }

    /* Set correct units */
    if (units == NULL) {
        switch(self->type) {
            case XYZ:
				// For Molden format this is just preliminary; could be a.u.
				// as well and will be determined during topology read
            case MOLDEN:
                self->units = ANGS;
                break;
            case GRO:
            case XTC:
                self->units = NM;
                break;
            case GUESS:
            default:
                PyErr_SetString(PyExc_RuntimeError, "Should not be here");
                return -1;
                break;
        }
	// If units were given:
    } else {
        if      (!strcmp(units, "angs")) self->units = ANGS;
        else if (!strcmp(units, "bohr")) self->units = BOHR;
        else if (!strcmp(units,   "nm")) self->units = NM;
		else {
            PyErr_SetString(PyExc_ValueError, "Supported units are: angs, bohr, nm");
            return -1;
		}
    }

    if (py_sym != NULL) {
		if (self->mode == 'r') {
            PyErr_SetString(PyExc_ValueError, "Don't use symbols in 'r' mode");
            return -1;
		}
        Py_DECREF(self->symbols);
        self->symbols = py_sym;
        Py_INCREF(self->symbols);
    }

    if (py_resid != NULL) {
        Py_DECREF(self->resids);
        self->resids = py_resid;
        Py_INCREF(self->resids);
    }

    if (py_resn != NULL) {
        Py_DECREF(self->resNames);
        self->resNames = py_resn;
        Py_INCREF(self->resNames);
    }

    if (self->mode == 'w' || self->mode == 'a') {

		if (self->mode == 'w' && !access(filename, F_OK)) {
            PyErr_SetString(PyExc_FileExistsError, "Selected 'w' mode, but file exists");
            return -1; }

        if (self->symbols == Py_None) {
            PyErr_SetString(PyExc_ValueError, "Need atomic symbols");
            return -1; }

        self->nAtoms = PyList_Size(self->symbols);

        /* Open the coordinate file */
        switch(self->type) {
            case XYZ:
            case GRO:
                if ( (self->fd = fopen(filename, mode)) == NULL ) {
                    PyErr_SetFromErrno(PyExc_IOError);
                    return -1; }
                break;
            case MOLDEN:
            case XTC:
            default:
                PyErr_SetString(PyExc_NotImplementedError,
                                "Writing in this format is not implemented");
                return -1;
                break;
        }

	// File was opened for reading
    } else {

        /* Open the coordinate file */
        switch(self->type) {
            case XYZ:
            case GRO:
                if ( (self->fd = fopen(filename, "r")) == NULL ) {
                    PyErr_SetFromErrno(PyExc_IOError);
                    return -1; }
                break;
            case MOLDEN:
                if ( (self->fd = fopen(filename, "r")) == NULL ) {
                    PyErr_SetFromErrno(PyExc_IOError);
                    return -1; }
                if (read_molden_sections(self) == -1) {
                	PyErr_SetString(PyExc_SystemError, "Could not read Molden sections");
					 	return -1;
					}
					if (self->moldenStyle == MLUNK) {
                		PyErr_SetString(PyExc_SystemError, "Molden style unknown");
					 	return -1;
					}
                break;
            case XTC:
#ifdef HAVE_GROMACS
                if( (self->xd = open_xtc(filename, "r")) == NULL) {
                    PyErr_SetString(PyExc_IOError, "Error opening XTC file");
                    return -1; }
#else
                PyErr_SetString(PyExc_SystemError,
                    "mdarray has to be compiled with gromacs support to handle XTC files");
                return -1;
#endif
                break;
            case GUESS:
            default:
                PyErr_SetString(PyExc_RuntimeError, "Should not be here");
                return -1;
                break;
        }

        /* Router */
        switch(self->type) {
            case XYZ:
                if (read_topo_from_xyz(self) == -1) return -1;
                rewind(self->fd);
                //self->filePosition1 = ftell(self->fd);
                //self->filePosition2 = self->filePosition1;
                break;
            case MOLDEN:
                if (read_topo_from_molden(self) == -1) return -1;
                fseek(self->fd, self->filePosition1, SEEK_SET);
                //self->filePosition1 = ftell(self->fd);
                //self->filePosition2 = self->filePosition1;
                break;
            case GRO:
                if (read_topo_from_gro(self) == -1) return -1;
                rewind(self->fd);
                //self->filePosition1 = ftell(self->fd);
                //self->filePosition2 = self->filePosition1;
                break;
            case XTC:
#ifdef HAVE_GROMACS
                if (!read_first_xtc(self->xd, &(self->nAtoms), &step, &time,
                                    box, &(self->xtcCoord), &prec, &bOK) && bOK) {
                    PyErr_SetString(PyExc_IOError, "Error reading first frame");
                    return -1; }
                close_xtc(self->xd);
                self->xd = open_xtc(filename, "r");
#endif
                break;
            /* If the file format is GUESS or different,
               it means we've failed to guess :-(        */
            case GUESS:
            default:
                PyErr_SetString(PyExc_RuntimeError, "Should not be here");
                return -1;
        }
    }

    return 0;
}



static PyObject *Trajectory_read(Trajectory *self, PyObject *args,
							PyObject *kwds) {

    PyObject *py_result = NULL;
	char *buffer = NULL;
	size_t buflen;
	long offset;
	int status;
	int doWrap = 0;
	ARRAY_REAL box[3];
	ARRAY_REAL *boxptr = NULL;
	int type;

	PyObject *py_box = NULL;

    static char *kwlist[] = {
        "wrap", "box", NULL };

    if (self->mode != 'r') {
        PyErr_SetString(PyExc_RuntimeError, "Trying to read in write mode");
        return NULL; }

    if(!PyArg_ParseTupleAndKeywords(args, kwds, "|pO!", kwlist,
			&doWrap, &PyArray_Type, &py_box))
        return NULL;

	if(doWrap) {
		// If the box is specified - use it to wrap atoms.
		// Otherwise apply information from filetypes that
		// support PBC or fail in other cases.
		if (py_box != NULL) {
			type = PyArray_TYPE((PyArrayObject*)py_box);
			box[0] = (ARRAY_REAL)getFromVector(py_box, type, 0);
			box[1] = (ARRAY_REAL)getFromVector(py_box, type, 1);
			box[2] = (ARRAY_REAL)getFromVector(py_box, type, 2);
			boxptr = box;
		}

		//if (py_box != NULL && self->type == XTC) {
		//	PyErr_WarnEx(PyExc_RuntimeWarning,
		//		"File type is XTC and box was supplied;"
		//		" PBC box from the file will be ignored.", 1);
		//}
	}

	// Before really reading a frame, make sure that there is something
	// to read. Get the next line, see if it makes sense and then rewind.
	//
	if (self->type != XTC) {
		offset = ftell(self->fd);
	    status = getline(&buffer, &buflen, self->fd);
		if (status == -1) Py_RETURN_NONE;
		stripline(buffer);
		if (self->type == MOLDEN && buffer[0] == '[') Py_RETURN_NONE;
	    fseek(self->fd, offset, SEEK_SET);
		free(buffer);
		buffer = NULL;
	}

    switch(self->type) {

        case MOLDEN:
        case XYZ:
            py_result = read_frame_from_xyz(self, doWrap, boxptr);
            break;

        case GRO:
            py_result = read_frame_from_gro(self, doWrap, boxptr);
            break;

#ifdef HAVE_GROMACS
        case XTC:
            py_result = read_frame_from_xtc(self, doWrap, boxptr);
			// Checking for the EOF is done inside the function
			if (py_result == Py_None) return py_result;
            break;
#endif

        default:
            break;
    }

	if (py_result == NULL) return NULL;
    self->lastFrame += 1;
    return py_result;

}




static PyObject *Trajectory_write(Trajectory *self, PyObject *args, PyObject *kwds) {

	PyObject *py_coords = NULL;
	PyObject *py_vel = NULL;
	PyObject *py_box = NULL;
	char *comment = NULL;;
	int out;
	npy_intp *dims;

	static char *kwlist[] = {
		"coordinates", "velocities", "box", "comment", NULL };

	if(!PyArg_ParseTupleAndKeywords(args, kwds, "O!|O!O!s", kwlist,
			&PyArray_Type, &py_coords,
			&PyArray_Type, &py_vel,
			&PyArray_Type, &py_box,
			&comment))
		return NULL;


	// Perform checks.

	// Mode must be w/a
	if (self->mode != 'a' && self->mode != 'w') {
		PyErr_SetString(PyExc_RuntimeError, "Trying to write in read mode");
		return NULL; }

	// Arrays must be 2D:
	if (PyArray_NDIM((PyArrayObject*)py_coords) != 2) {
        PyErr_SetString(PyExc_RuntimeError, "Coordinate array must be 2D");
		return NULL; }
	if (py_vel != NULL && PyArray_NDIM((PyArrayObject*)py_vel) != 2) {
        PyErr_SetString(PyExc_RuntimeError, "Velocities array must be 2D");
		return NULL; }
	if (py_box != NULL && PyArray_NDIM((PyArrayObject*)py_box) != 2) {
        PyErr_SetString(PyExc_RuntimeError, "Box array must be 2D");
		return NULL; }

	// dims should be (nAtoms,3)
	dims = PyArray_DIMS((PyArrayObject*)py_coords);
	if (dims[0] != self->nAtoms || dims[1] != 3) {
        PyErr_SetString(PyExc_RuntimeError, "Shape of the coordinates array must be (nAtoms, 3)");
		return NULL; }
	if (py_vel != NULL) {
		dims = PyArray_DIMS((PyArrayObject*)py_vel);
		if (dims[0] != self->nAtoms || dims[1] != 3) {
    	    PyErr_SetString(PyExc_RuntimeError, "Shape of the velocities array must be (nAtoms, 3)");
			return NULL; }
	}
	if (py_box != NULL) {
		dims = PyArray_DIMS((PyArrayObject*)py_box);
		if (dims[0] != 3 || dims[1] != 3) {
    	    PyErr_SetString(PyExc_RuntimeError, "Shape of the box array must be (3, 3)");
			return NULL; }
	}

	// Symbols must be a sequence
	if (!PyList_Check(self->symbols)) {
        PyErr_SetString(PyExc_RuntimeError, "Trajectory instance must contain a list of symbols");
		return NULL; }

	switch(self->type) {
		case XYZ:
			out = write_frame_to_xyz(self, py_coords, comment);
			if (out != 0) return NULL;
			break;
		case GRO:
			if (write_frame_to_gro(self, py_coords, py_vel, py_box, comment)) {
				PyErr_SetString(PyExc_RuntimeError, "Could not write");
				return NULL;
			}
			break;

		default:
			break;
	}

	self->lastFrame += 1;
	Py_RETURN_NONE;

}





static PyObject* Trajectory_repr(Trajectory *self) {
    PyObject* str;
    char format[10];
    char units[10];

    switch(self->type) {
        case XYZ:
            strcpy(format,    "XYZ"); break;
/*        case MOLDEN:
            strcpy(format, "MOLDEN"); break;*/
        case GRO:
            strcpy(format,    "GRO"); break;
        case XTC:
            strcpy(format,    "XTC"); break;
        default:
            strcpy(format,       ""); break;
    }
    switch(self->units) {
        case ANGS:
            strcpy(units, "angs"); break;
        case BOHR:
            strcpy(units, "bohr"); break;
        case NM:
            strcpy(units,   "nm"); break;
        default:
            strcpy(units,     ""); break;
	 }
    str = PyUnicode_FromFormat("Trajectory('%s', format='%s', "
	 				"mode='%c', units='%s')", self->fileName, format,
					self->mode, units);
    return str;
}

/* End of method definitions */




/* Class definition */

static PyMemberDef Trajectory_members[] = {
    {"symbols", T_OBJECT_EX, offsetof(Trajectory, symbols), READONLY,
     "A list of atomic symbols"},
    {"aNumbers", T_OBJECT_EX, offsetof(Trajectory, aNumbers), READONLY,
     "An ndarray with atomic numbers"},
    {"masses", T_OBJECT_EX, offsetof(Trajectory, masses), READONLY,
     "An ndarray with atomic masses"},
    {"resids", T_OBJECT_EX, offsetof(Trajectory, resids), READONLY,
     "An ndarray with residue numbers - one number per atom"},
    {"resNames", T_OBJECT_EX, offsetof(Trajectory, resNames), READONLY,
     "A list of residue names"},
    {"nAtoms", T_INT, offsetof(Trajectory, nAtoms), READONLY,
     "Number of atoms (int)"},
    {"lastFrame", T_INT, offsetof(Trajectory, lastFrame), READONLY,
     "Index of the last frame read or written; starts with 0, "
	 "lastFrame = -1 means that none has been read/written."},
    //{"moldenSections", T_OBJECT_EX, offsetof(Trajectory, moldenSections), READONLY,
    // "Dictionary containing byte offsets to sections in Molden file"},
    {"fileName", T_STRING, offsetof(Trajectory, fileName), READONLY,
     "File name (str)"},
    {NULL}  /* Sentinel */
};




static PyMethodDef Trajectory_methods[] = {

    {"read", (PyCFunction)Trajectory_read, METH_VARARGS | METH_KEYWORDS,
        "\n"
        "Trajectory.read()\n"
        "\n"
        "Read next frame from trajectory. Returns a dictionary with:\n"
        "\n"
        "coordinates (ndarray)\n"
        "step (int)\n"
        "time (float)\n"
        "box (ndarray) shape=3,3\n"
        "\n" },

	{"write", (PyCFunction)Trajectory_write, METH_VARARGS | METH_KEYWORDS,
		"\n"
		"Trajectory.write(coordinates, [ comment ])\n"
		"\n"
		"coordinates (ndarray)\n"
		"velocities (ndarray)\n"
		"box (ndarray)\n"
		"comment (string)\n"
		"\n" },

    {NULL}  /* Sentinel */
};




PyTypeObject TrajectoryType = {

    PyVarObject_HEAD_INIT(NULL, 0)
    "mdarray.Trajectory",          /*tp_name*/
    sizeof(Trajectory),             /*tp_basicsize*/
    0,                              /*tp_itemsize*/

    /* Methods to implement standard operations */
    (destructor)Trajectory_dealloc, /*tp_dealloc*/
    0,                              /*tp_print*/
    0,                              /*tp_getattr*/
    0,                              /*tp_setattr*/
	 0,                              /* tp_reserved */
    (reprfunc)Trajectory_repr,      /*tp_repr*/

    /* Method suites for standard classes */
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/

    /* More standard operations (here for binary compatibility) */
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/

    /* Functions to access object as input/output buffer */
    0,                         /*tp_as_buffer*/

    /* Flags to define presence of optional/expanded features */
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/

    /* Documentation string */
    "Trajectory class. Implements reading of trajectories from XYZ. Molden, "
	 "GRO and XTC. Writing is implemented for XYZ and GRO. The process is "
	 "two-step; first, the object must be created, by specifying fileName "
	 "(for reading) or topology information (for writing). Second, frames "
	 "can be read/saved repeteadly. Reading examples:\n"
    "  traj = Trajectory('my.xyz')\n"
    "  frame1 = traj.read()\n"
    "  frame2 = traj.read()\n"
    "Object of the class Trajectory contains such fields as: symbols, "
	 "aNumbers, masses, resIDs, resNames, nAtoms, nOfFrames, "
	 "lastFrame, moldenSections, fileName. Method read() returns a dictionary "
	 "with items depending on the file format, but at least 'coordinates' "
	 "are present. Writing example:\n"
    "  traj = Trajectory('my.xyz', symbols_list)\n"
    "  traj.write(coordinates1)\n"
    "  traj.write(coordinates2)\n"
    "When writing a trajectory, at least the file name and the list of "
	 "symbols must be specified. Creating an instance for reading:\n"
    "  traj = Trajectory(fileName, format='GUESS', mode='r', units='angs')\n"
    "Available formats include: XYZ, GRO, MOLDEN, XTC - guessed if not "
	 "specified.\n"
    "Mode: 'r' (default), 'w', 'a'.\n"
    "Units: 'angs' (default), 'bohr', 'nm'.\n"
    "Creating an instance for writing:\n"
    "  traj = Trajectory(filename, format='GUESS', mode='w', symbols=, "
	 "resids=, resnames=)\n"
    "symbols and resnames are lists, while resids, coodinates and velocities "
	 "are ndarrays.\n",           /* tp_doc */

    0,                       /* tp_traverse */
    0,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    Trajectory_methods,        /* tp_methods */
    Trajectory_members,        /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Trajectory_init, /* tp_init */
    0,                         /* tp_alloc */
    Trajectory_new,            /* tp_new */
	 0,                         /* tp_free */
	 0,                         /* tp_is_gc */
	 0,                         /* tp_bases */
	 0,                         /* tp_mro */
	 0,                         /* tp_cache */
	 0,                         /* tp_subclasses */
	 0,                         /* tp_weaklist */
	 0,                         /* tp_del */
	 0,                         /* tp_version_tag */
	 0                          /* tp_finalize */
};

/* End of class definition */







/* Local helper functions */

static int read_topo_from_xyz(Trajectory *self) {

    int nofatoms, pos, idx;
    char *buffer = NULL;
	char *buffpos, *token;
	size_t buflen = 0;
    int *anum;
	ARRAY_REAL *masses;
    extern Element element_table[];

    npy_intp dims[2];
    PyObject *val;

    /* Read number of atoms */
    if (_getline(&buffer, &buflen, self->fd) == -1) return -1;
    if (sscanf(buffer, "%d", &nofatoms) != 1 ) {
        PyErr_SetString(PyExc_IOError, "Incorrect atom number");
        return -1; }

    /* Read the comment line */
    if (_getline(&buffer, &buflen, self->fd) == -1) return -1;

    /* Get rid of Py_None in self->symbols */
    Py_DECREF(self->symbols);
    self->symbols = PyList_New(nofatoms);

    anum = (int*) malloc(nofatoms * sizeof(int));
    if(anum == NULL) {
        PyErr_SetFromErrno(PyExc_MemoryError);
        return -1; }

    masses = (ARRAY_REAL*) malloc(nofatoms * sizeof(ARRAY_REAL));
    if(masses == NULL) {
        PyErr_SetFromErrno(PyExc_MemoryError);
        return -1; }

    /* Atom loop */
    for(pos = 0; pos < nofatoms; pos++) {

        /* Get the whole line */
        if(_getline(&buffer, &buflen, self->fd) == -1) return -1;
        buffer[strlen(buffer)-1] = '\0';
        buffpos = buffer;

        /* Read symbol */
        token = strtok(buffpos, " \t");
        val = Py_BuildValue("s", token);
        PyList_SetItem(self->symbols, pos, val);

        idx = getElementIndexBySymbol(token);
        if (idx == -1) {
            anum[pos] = -1;
				masses[pos] = 0.0;
        } else {
            anum[pos] = element_table[idx].number;
				masses[pos] = element_table[idx].mass;
		}

        /* Free the line buffer */
    }
    free(buffer);

    /* Add atomic numbers to the dictionary */
    dims[0] = nofatoms;
    dims[1] = 1;
    Py_DECREF(self->aNumbers);
    self->aNumbers = PyArray_SimpleNewFromData(1, dims, NPY_INT, (int*) anum);
    Py_DECREF(self->masses);
    self->masses = PyArray_SimpleNewFromData(1, dims, NPY_ARRAY_REAL, (ARRAY_REAL*) masses);

    self->nAtoms = nofatoms;

    return 0;
}




/* Read MOLDEN file and get the sections present. Store offset to the *
 * particular section too. Returns number of sections parsed or -1 on *
 * error. */

static int read_molden_sections(Trajectory *self) {

    long filepos;
    char *strptr;
	char *line = NULL;
	size_t llen;
    char buffer[256];
    int i, len, nsec = 0;

	for (i = 0; i < MAX_MOLDEN_SECTIONS; i++) {
		self->moldenSect[i].offset = -1;
		strcpy(self->moldenSect[i].name, "");
	}

    rewind(self->fd);

    filepos = ftell(self->fd);
    while (getline(&line, &llen, self->fd) != -1) {

	    stripline(line);
    	make_lowercase(line);

        // Start of a section 
        if(line[0] == '[') {

            // Get the name 
            strptr = strchr(line, ']');
            len = (int)(strptr - line) - 1;
            strncpy(buffer, line+1, len);
            buffer[len] = '\0';

			self->moldenSect[nsec].offset = filepos;
			strcpy(self->moldenSect[nsec].name, buffer);

			// Out of three sections that provide coordinates,
			// if more than one is present, the order of preference
			// should be:
			// GEOMETRIES, ATOMS, FR-COORD
			if (!strcmp(buffer, "geometries")) {
				self->moldenStyle = MLGEOM;
			} else if (self->moldenStyle != MLGEOM && !strcmp(buffer, "atoms")) {
				self->moldenStyle = MLATOMS;
			} else if (self->moldenStyle != MLGEOM &&
			           self->moldenStyle != MLATOMS && !strcmp(buffer, "fr-coord")) {
					self->moldenStyle = MLFREQ;
					// Set default units for this style - maybe not perfect
					// place to do it...
    	        	self->units = BOHR;
			}
			if (++nsec > MAX_MOLDEN_SECTIONS) return -1;

        }
        
        filepos = ftell(self->fd);
    }
    free(line);

    rewind(self->fd);

    return nsec;
}


static int get_section_idx(Trajectory *self, const char name[]) {
	int idx;

	for (idx = 0; idx < MAX_MOLDEN_SECTIONS; idx++)
		if (!strcmp(self->moldenSect[idx].name, name)) return idx;
	return -1;
}


static int read_topo_from_molden(Trajectory *self) {

	char *line = NULL;
	size_t llen;
	char buffer[1000];
	char *token;
	int i, nat, idx;
	int *anum;
	long offset;
    extern Element element_table[];

	npy_intp dims[2];
	PyObject *val;

	// Make sure that the sections are done 
	if (self->moldenStyle == MLUNK) {
		PyErr_SetString(PyExc_RuntimeError, "Unidentified Molden style");
		return -1;
	}

	// Find the right section with coordinates
	switch(self->moldenStyle) {
	 	case MLGEOM:
			idx = get_section_idx(self, "geometries");
			break;
		case MLATOMS:
			idx = get_section_idx(self, "atoms");
			break;
		case MLFREQ:
			idx = get_section_idx(self, "fr-coord");
			break;
	}
	if (idx == -1) {
		PyErr_SetString(PyExc_RuntimeError, "Could not find section");
		return -1; }
	offset = self->moldenSect[idx].offset;

    // In these styles we don't know how many atoms are there,
    // so we have to count atoms first.
	if (self->moldenStyle == MLATOMS || self->moldenStyle == MLFREQ) {

        fseek(self->fd, offset, SEEK_SET);

        //self->filePosition1 = ftell(self->fd);
        // Read [section] to reach atoms
	    if (_getline(&line, &llen, self->fd) == -1) return -1;

        nat = 0;
		while (getline(&line, &llen, self->fd) != -1) {
	   	   	stripline(line);
			if (line[0] == '[') break;
			nat += 1;
		}
   		//fseek(self->fd, self->filePosition1, SEEK_SET);
        self->nAtoms = nat;
	}

	// Seek to section and read the atoms
    fseek(self->fd, offset, SEEK_SET);
    if (_getline(&line, &llen, self->fd) == -1) return -1;
    stripline(line);
    make_lowercase(line);

	// Store the position where coordinates begin
    self->filePosition1 = ftell(self->fd);

	switch(self->moldenStyle) {

	 	case MLGEOM:

			token = strstr(line, "zmat");
			if (token != NULL ) {
				PyErr_SetString(PyExc_RuntimeError, "Z-mat not supported");
				return -1;
			}
	        //self->filePosition1 = ftell(self->fd);
    	    read_topo_from_xyz(self);
			break;

	 	case MLATOMS:

	        if ( !strcmp(line, "[atoms] angs") )
    	        self->units = ANGS;
	        else if ( !strcmp(line, "[atoms] au") )
    	        self->units = BOHR;
	        else {
    	        PyErr_SetString(PyExc_RuntimeError, "Unrecognized units");
        	    return -1; }

			// No 'break' here! The next part is common for MLATOMS and MLFREQ

	 	case MLFREQ:

	        // Get rid of Py_None in self->symbols 
    	    Py_DECREF(self->symbols);
        	self->symbols = PyList_New(nat);

	        anum = (int*) malloc(nat * sizeof(int));
    	    if(anum == NULL) {
        	    PyErr_SetFromErrno(PyExc_MemoryError);
	            return -1; }

	        // Loop over atoms 
    	    for ( i = 0; i < nat; i++ ) {
				if (_getline(&line, &llen, self->fd) == -1) return -1;

	            strcpy(buffer, line);
    	        token = strtok(buffer, " \t");
        	    val = Py_BuildValue("s", token);
            	PyList_SetItem(self->symbols, i, val);

   		        // not used 
				if (self->moldenStyle == MLATOMS)
		            token = strtok(NULL, " \t");

				if (self->moldenStyle == MLATOMS) {
		            token = strtok(NULL, " \t");
	    	        anum[i] = atoi(token);
				} else {
			        idx = getElementIndexBySymbol(token);
					if (idx == -1) anum[i] = -1;
					else
				        anum[i] = element_table[idx].number;
				}

        	}

	        // Add atomic numbers to the dictionary 
    	    dims[0] = nat;
        	dims[1] = 1;
	        Py_DECREF(self->aNumbers);
	        self->aNumbers = PyArray_SimpleNewFromData(1, dims, NPY_INT, anum);
			break;

	    default:

	        PyErr_SetString(PyExc_RuntimeError, "geometry/atom section missing");
    	    return -1;
	}
    
    free(line);

    return 0;
}




static int read_topo_from_gro(Trajectory *self) {

    Py_ssize_t pos;
    int nofatoms;
    char *buffer = NULL;
	size_t buflen;
    char symbuf[100];
    int *resid;
    npy_intp dims[2];
    PyObject *val;

    // Read the comment line 
    if(_getline(&buffer, &buflen, self->fd) == -1) return -1;
    buffer[strlen(buffer)-1] = '\0';
    stripline(buffer);

    // Read number of atoms 
    if (_getline(&buffer, &buflen, self->fd) == -1) return -1;
    if( sscanf(buffer, "%d", &nofatoms) != 1 ) {
        PyErr_SetString(PyExc_IOError, "Incorrect atom number");
        return -1; }

    self->nAtoms = nofatoms;

    // Get rid of Py_None in self->symbols etc. 
    Py_DECREF(self->symbols);
    self->symbols = PyList_New(nofatoms);
    Py_DECREF(self->resNames);
    self->resNames = PyList_New(nofatoms);

    resid = (int*) malloc(nofatoms * sizeof(int));
    if(resid == NULL) {
        PyErr_SetFromErrno(PyExc_MemoryError);
        return -1; }

    // Atom loop 
    for(pos = 0; pos < nofatoms; pos++) {

        // Get the whole line 
        if (_getline(&buffer, &buflen, self->fd) == -1) return -1;

        // Read residue id 
        strncpy(symbuf, buffer, 5);
        symbuf[5] = '\0';
        stripline(symbuf);
        resid[pos] = atoi(symbuf);

        // Read residue name 
        strncpy(symbuf, buffer+5, 5);
        symbuf[5] = '\0';
        stripline(symbuf);
        val = Py_BuildValue("s", symbuf);
        PyList_SetItem(self->resNames, pos, val);

        // Read atom name 
        strncpy(symbuf, buffer+10, 5);
        symbuf[5] = '\0';
        stripline(symbuf);
        val = Py_BuildValue("s", symbuf);
        PyList_SetItem(self->symbols, pos, val);
    }

    // Free the line buffer 
    free(buffer);

    // Add residue IDs to the dictionary 
    dims[0] = nofatoms;
    dims[1] = 1;
    Py_DECREF(self->resids);
    self->resids = PyArray_SimpleNewFromData(1, dims, NPY_INT, resid);
    // **************************************************************
    // Do not free the raw array! It will be still used by Python! *
    // **************************************************************

    return 0;
}




/* 
 * WARNING: This function is used by other format than XYZ,
 * like Molden for instance, so be careful with implementation.
 */

static PyObject *read_frame_from_xyz(Trajectory *self, int doWrap, ARRAY_REAL *box) {

    PyObject *py_result, *py_coord;
	PyObject *py_extra;
	PyObject *val, *key;
    char *buffer = NULL;
	char *buffpos, *token;
	size_t buflen;
    int pos, nat;
    float factor;
    ARRAY_REAL *xyz;
	ARRAY_REAL *extra;
	unsigned short int extra_present;
    npy_intp dims[2];

	if (doWrap && box == NULL) {
   	    PyErr_SetString(PyExc_RuntimeError,
				"Requested PBC, but box information is missing");
       	Py_DECREF(py_result);
        return NULL; }

    switch(self->units) {
        case ANGS: factor = 1.0; break;
        case NM: factor = 10.0; break;
        case BOHR: factor = BOHRTOANGS; break;
    }
    /* Create the dictionary that will be returned */
    py_result = PyDict_New();

	// Number of atoms and comment are present only in these
	// types & flavours
	if (self->type == XYZ ||
		(self->type == MOLDEN && self->moldenStyle == MLGEOM)) {

	    /* Read number of atoms */
	    if (_getline(&buffer, &buflen, self->fd) == -1) {
	        Py_DECREF(py_result);
    	    return NULL;
		}

	    if (sscanf(buffer, "%d", &nat) != 1) {
        	PyErr_SetFromErrno(PyExc_IOError);
	        Py_DECREF(py_result);
    	    return NULL;
	    }

	    if (nat != self->nAtoms) {
    	    PyErr_SetString(PyExc_RuntimeError,
				"Number of atoms different than expected");
        	Py_DECREF(py_result);
	        return NULL; }

	    /* Read the comment line */
    	if (_getline(&buffer, &buflen, self->fd) == -1) {
	        Py_DECREF(py_result);
    	    return NULL; }

	    buffer[strlen(buffer)-1] = '\0';

	    val = Py_BuildValue("s", buffer);
    	key = PyUnicode_FromString("comment");
	    PyDict_SetItem(py_result, key, val);
    	Py_DECREF(key);
	    Py_DECREF(val);

	}

    /* Set-up the raw arrays for coordinates and extra data */
    xyz = (ARRAY_REAL*) malloc(3 * self->nAtoms * sizeof(ARRAY_REAL));
    if(xyz == NULL) {
        PyErr_SetFromErrno(PyExc_MemoryError);
        Py_DECREF(py_result);
        return NULL; }
    extra = (ARRAY_REAL*) malloc(self->nAtoms * sizeof(ARRAY_REAL));
    if(extra == NULL) {
        PyErr_SetFromErrno(PyExc_MemoryError);
        Py_DECREF(py_result);
        return NULL; }
    extra_present = 0;

    /* Atom loop */
    for(pos = 0; pos < self->nAtoms; pos++) {

        /* Get the whole line */
        if (_getline(&buffer, &buflen, self->fd) == -1) {
	        Py_DECREF(py_result);
            return NULL; }
        buffer[strlen(buffer)-1] = '\0';
        buffpos = buffer;

        /* Read symbol */
        token = strtok(buffpos, " \t");

		// This style has two additional entries
		if (self->type == MOLDEN && self->moldenStyle == MLATOMS) {
	        token = strtok(NULL, " \t");
    	    token = strtok(NULL, " \t");
		}

        /* Read coordinates */
        if ( (token = strtok(NULL, " \t")) == NULL) {
            PyErr_SetString(PyExc_IOError, "Missing coordinate");
            Py_DECREF(py_result);
            return NULL; }
        xyz[3*pos + 0] = atof(token) * factor;
        if ( (token = strtok(NULL, " \t")) == NULL) {
            PyErr_SetString(PyExc_IOError, "Missing coordinate");
            Py_DECREF(py_result);
            return NULL; }
        xyz[3*pos + 1] = atof(token) * factor;
        if ( (token = strtok(NULL, " \t")) == NULL) {
            PyErr_SetString(PyExc_IOError, "Missing coordinate");
            Py_DECREF(py_result);
            return NULL; }
        xyz[3*pos + 2] = atof(token) * factor;

		if (doWrap) wrapPBCsingle(xyz + (3*pos), box);

        // Read charge, if present
        token = strtok(NULL, " \t");
        if ( token != NULL ) {

            // This is bad: until now, there were no extra data
            if ( pos > 0 && !extra_present ) {
                PyErr_SetString(PyExc_IOError, "Unexpected extra data found");
                Py_DECREF(py_result);
                return NULL;
            }

            extra_present = 1;
            extra[pos] = atof(token);

        } else {

            // This is bad: we were expecting extra data here and found nothing
            if ( pos > 0 && extra_present ) {
                PyErr_SetString(PyExc_IOError, "Inconsistent extra data");
                Py_DECREF(py_result);
                return NULL;
            }
        }

    }
    free(buffer);

    /* Add coordinates to the dictionary */
    dims[0] = self->nAtoms;
    dims[1] = 3;
    py_coord = PyArray_SimpleNewFromData(2, dims, NPY_ARRAY_REAL, xyz);
    /***************************************************************
     * Do not free the raw array! It will be still used by Python! *
     ***************************************************************/

    key = PyUnicode_FromString("coordinates");
    PyDict_SetItem(py_result, key, py_coord);
    Py_DECREF(key);
    Py_DECREF(py_coord);


    /* Add extra data, if present */
    if ( extra_present ) {
        py_extra = PyArray_SimpleNewFromData(1, dims, NPY_ARRAY_REAL, extra);
        key = PyUnicode_FromString("extra");
        PyDict_SetItem(py_result, key, py_extra);
        Py_DECREF(key);
        Py_DECREF(py_extra);
    } else
        // Free the extra data ONLY if the Python object was not created!
        free(extra);

    return py_result;

}




static PyObject *read_frame_from_gro(Trajectory *self, int doWrap, ARRAY_REAL *newbox) {

    int nat, pos;
    char *buffer = NULL;
	size_t buflen;
    ARRAY_REAL *xyz, *vel, *box;
    unsigned short int velocities_present = 0;
	ARRAY_REAL wrapBox[3];

    npy_intp dims[2];

    PyObject *key, *val, *py_result, *py_coord, *py_vel, *py_box;


    // Create the dictionary that will be returned 
    py_result = PyDict_New();

    // Read the comment line 
    if (_getline(&buffer, &buflen, self->fd) == -1) return NULL;

    if (buffer[0] == '\0') {
        free(buffer);
        return NULL; }
    
    buffer[strlen(buffer)-1] = '\0';
    stripline(buffer);

    val = Py_BuildValue("s", buffer);
    key = PyUnicode_FromString("comment");
    PyDict_SetItem(py_result, key, val);
    Py_DECREF(key);
    Py_DECREF(val);

    // Read number of atoms 
    if (_getline(&buffer, &buflen, self->fd) == -1) return NULL;
    if( sscanf(buffer, "%d", &nat) != 1 || nat != self->nAtoms) {
        PyErr_SetString(PyExc_IOError, "Incorrect atom number");
        free(buffer);
        return NULL; }

    // Set-up the raw arrays for coordinates and charges 
    xyz = (ARRAY_REAL*) malloc(3 * self->nAtoms * sizeof(ARRAY_REAL));
    if(xyz == NULL) {
        PyErr_SetFromErrno(PyExc_MemoryError);
        return NULL; }
    vel = (ARRAY_REAL*) malloc(3 * self->nAtoms * sizeof(ARRAY_REAL));
    if(vel == NULL) {
        PyErr_SetFromErrno(PyExc_MemoryError);
        return NULL; }
    box = (ARRAY_REAL*) malloc(9 * sizeof(ARRAY_REAL));
    if(box == NULL) {
        PyErr_SetFromErrno(PyExc_MemoryError);
        return NULL; }

    // Atom loop 
    for(pos = 0; pos < self->nAtoms; pos++) {

        // Get the whole line 
        if (_getline(&buffer, &buflen, self->fd) == -1) return NULL;
        if(pos == 0 && strlen(buffer) > 50) velocities_present = 1;

        // Read coordinates 
        xyz[3*pos + 0] = strPartFloat(buffer, 20, 8) * 10.0;
        xyz[3*pos + 1] = strPartFloat(buffer, 28, 8) * 10.0;
        xyz[3*pos + 2] = strPartFloat(buffer, 36, 8) * 10.0;

        // Read velocities 
        if(velocities_present) {
            vel[3*pos + 0] = strPartFloat(buffer, 44, 8);
            vel[3*pos + 1] = strPartFloat(buffer, 52, 8);
            vel[3*pos + 2] = strPartFloat(buffer, 60, 8);
        }
    }

    // Get the cell line 
    if (_getline(&buffer, &buflen, self->fd) == -1) return NULL;
    box[3*0 + 0] = strPartFloat(buffer,  0, 10) * 10.0;
    box[3*1 + 1] = strPartFloat(buffer, 10, 10) * 10.0;
    box[3*2 + 2] = strPartFloat(buffer, 20, 10) * 10.0;
    if (strlen(buffer) > 31) box[3*0 + 1] = strPartFloat(buffer, 30, 10) * 10.0;
    else                     box[3*0 + 1] = 0.0;
    if (strlen(buffer) > 41) box[3*0 + 2] = strPartFloat(buffer, 40, 10) * 10.0;
    else                     box[3*0 + 2] = 0.0;
    if (strlen(buffer) > 51) box[3*1 + 0] = strPartFloat(buffer, 50, 10) * 10.0;
    else                     box[3*1 + 0] = 0.0;
    if (strlen(buffer) > 61) box[3*1 + 2] = strPartFloat(buffer, 60, 10) * 10.0;
    else                     box[3*1 + 2] = 0.0;
    if (strlen(buffer) > 71) box[3*2 + 0] = strPartFloat(buffer, 70, 10) * 10.0;
    else                     box[3*2 + 0] = 0.0;
    if (strlen(buffer) > 81) box[3*2 + 1] = strPartFloat(buffer, 80, 10) * 10.0;
    else                     box[3*2 + 1] = 0.0;
    free(buffer);

	if (doWrap) {
		if (newbox == NULL) {
			wrapBox[0] = box[0];
			wrapBox[1] = box[4];
			wrapBox[2] = box[8];
			wrapPBC(xyz, self->nAtoms, wrapBox);
		} else {
			wrapPBC(xyz, self->nAtoms, newbox);
		}
	}

    // Add coordinates to the dictionary 
    dims[0] = self->nAtoms;
    dims[1] = 3;
    py_coord = PyArray_SimpleNewFromData(2, dims, NPY_ARRAY_REAL, (ARRAY_REAL*) xyz);
    // Do not free the raw array! It will be still used by Python!

    key = PyUnicode_FromString("coordinates");
    PyDict_SetItem(py_result, key, py_coord);
    Py_DECREF(key);
    Py_DECREF(py_coord);


    // Add velocities to the dictionary 
    if(velocities_present) {
        py_vel = PyArray_SimpleNewFromData(2, dims, NPY_ARRAY_REAL, (ARRAY_REAL*) vel);
        // Do not free the raw array! It will be still used by Python!

        key = PyUnicode_FromString("velocities");
        PyDict_SetItem(py_result, key, py_vel);
        Py_DECREF(key);
        Py_DECREF(py_vel);
    } else
        free(vel);

    dims[0] = 3;
    dims[1] = 3;
    py_box = PyArray_SimpleNewFromData(2, dims, NPY_ARRAY_REAL, (ARRAY_REAL*) box);
    key = PyUnicode_FromString("box");
    PyDict_SetItem(py_result, key, py_box);
    Py_DECREF(key);
    Py_DECREF(py_box);

    return py_result;

}




#ifdef HAVE_GROMACS
static PyObject *read_frame_from_xtc(Trajectory *self, int doWrap, ARRAY_REAL *newbox) {

    PyObject *py_dict, *val, *key, *py_coord, *py_box;
    matrix mbox;
    ARRAY_REAL *box, *xyz;
    ARRAY_REAL wrapBox[3];
    float time, prec;
    npy_intp dims[2];
    gmx_bool bOK;
    int i, step;

    /* Create the dictionary that will be returned */
    py_dict = PyDict_New();

    if (!read_next_xtc(self->xd, self->nAtoms, &step, &time, mbox, self->xtcCoord, &prec, &bOK)) {
        Py_DECREF(py_dict);
        Py_RETURN_NONE;
    }

    if (!bOK) {
        PyErr_SetString(PyExc_IOError, "Corrupted frame");
        return NULL;
    }

    val = Py_BuildValue("i", step);
    key = PyUnicode_FromString("step");
    PyDict_SetItem(py_dict, key, val);
    Py_DECREF(key);
    Py_DECREF(val);

    val = Py_BuildValue("f", time);
    key = PyUnicode_FromString("time");
    PyDict_SetItem(py_dict, key, val);
    Py_DECREF(key);
    Py_DECREF(val);

    box = (ARRAY_REAL*) malloc(9 * sizeof(ARRAY_REAL));
    if(box == NULL) {
        PyErr_SetFromErrno(PyExc_MemoryError);
        return NULL; }

    /* Only orthogonal boxes; implement other later */
    box[0] = (ARRAY_REAL)mbox[0][0] * 10;
    box[1] = (ARRAY_REAL)mbox[0][1] * 10;
    box[2] = (ARRAY_REAL)mbox[0][2] * 10;
    box[3] = (ARRAY_REAL)mbox[1][0] * 10;
    box[4] = (ARRAY_REAL)mbox[1][1] * 10;
    box[5] = (ARRAY_REAL)mbox[1][2] * 10;
    box[6] = (ARRAY_REAL)mbox[2][0] * 10;
    box[7] = (ARRAY_REAL)mbox[2][1] * 10;
    box[8] = (ARRAY_REAL)mbox[2][2] * 10;

	if (doWrap) {
		if (newbox == NULL) {
			wrapBox[0] = box[0];
			wrapBox[1] = box[4];
			wrapBox[2] = box[8];
		} else {
			wrapBox[0] = newbox[0];
			wrapBox[1] = newbox[1];
			wrapBox[2] = newbox[2];
		}
	}

    dims[0] = 3;
    dims[1] = 3;
    py_box = PyArray_SimpleNewFromData(2, dims, NPY_ARRAY_REAL, (ARRAY_REAL*) box);
    key = PyUnicode_FromString("box");
    PyDict_SetItem(py_dict, key, py_box);
    Py_DECREF(key);
    Py_DECREF(py_box);

    /* Set-up the raw arrays for coordinates */
    xyz = (ARRAY_REAL*) malloc(3 * self->nAtoms * sizeof(ARRAY_REAL));
    if(xyz == NULL) {
        PyErr_SetFromErrno(PyExc_MemoryError);
        return NULL; }

    for (i = 0; i < self->nAtoms; i++) {
        /* Times 10, because converting from nm */
        xyz[i*3    ] = (ARRAY_REAL)(self->xtcCoord[i][0] * 10.0);
        xyz[i*3 + 1] = (ARRAY_REAL)(self->xtcCoord[i][1] * 10.0);
        xyz[i*3 + 2] = (ARRAY_REAL)(self->xtcCoord[i][2] * 10.0);
		wrapPBCsingle(xyz + (i*3), wrapBox);
    }

    /* Add coordinates to the dictionary */
    dims[0] = self->nAtoms;
    dims[1] = 3;
    py_coord = PyArray_SimpleNewFromData(2, dims, NPY_ARRAY_REAL, (ARRAY_REAL*) xyz);
    /***************************************************************
     * Do not free the raw array! It will be still used by Python! *
     ***************************************************************/

    key = PyUnicode_FromString("coordinates");
    PyDict_SetItem(py_dict, key, py_coord);
    Py_DECREF(key);
    Py_DECREF(py_coord);

    return py_dict;
}
#endif /* HAVE_GROMACS */



static int write_frame_to_xyz(Trajectory *self, PyObject *py_coords, char *comment) {
	int type;
	int at;
	// Double should be accurate enough for writing
	double x, y, z;
	char *sym;

	fprintf(self->fd, "%d\n", self->nAtoms);
	if( comment != NULL )
		fprintf(self->fd, "%s\n", comment);
	else
		fprintf(self->fd, "\n");

	type = PyArray_TYPE((PyArrayObject*)py_coords);

	for (at = 0; at < self->nAtoms; at++) {
		sym = PyUnicode_AsUTF8(PyList_GetItem(self->symbols, at));
		x = (double)getFromArray2D(py_coords, type, at, 0);
		y = (double)getFromArray2D(py_coords, type, at, 1);
		z = (double)getFromArray2D(py_coords, type, at, 2);
		fprintf(self->fd, "%s % 12.8f % 12.8f % 12.8f\n", sym, x, y, z);
	}
	
	return 0;
}



static int write_frame_to_gro(Trajectory *self, PyObject *py_coords,
				PyObject *py_vel, PyObject *py_box, char *comment) {

	int i, resid, type, vtype;
	// Using float here, since the accuracy of gro is low anyway
	float x, y, z, vx, vy, vz;
	float box[9];
	char *sym, *resnam;
	char empty[1] = "";
	int box_order[9][2] = { {0,0}, {1,1}, {2,2}, {0,1}, {0,2}, {1,0}, {1,2}, {2,0}, {2,1} };
	npy_intp *dims;

	if( comment != NULL )
        fprintf(self->fd, "%s\n", comment);
    else
        fprintf(self->fd, "\n");
    fprintf(self->fd, "%5d\n", self->nAtoms);

	type = PyArray_TYPE((PyArrayObject*)py_coords);
	if (py_vel != NULL)
		vtype = PyArray_TYPE((PyArrayObject*)py_vel);

    for (i = 0; i < self->nAtoms; i++) {

		if (self->resids != Py_None)
			resid = *((int*) PyArray_GETPTR1((PyArrayObject*)self->resids, i));
		else
			resid = 1;

		if (self->resNames != Py_None)
			resnam = PyUnicode_AsUTF8(PyList_GetItem(self->resNames, i));
		else
			resnam = empty;

        sym = PyUnicode_AsUTF8(PyList_GetItem(self->symbols, i));

		x = (float)getFromArray2D(py_coords, type, i, 0) / 10.0;
		y = (float)getFromArray2D(py_coords, type, i, 1) / 10.0;
		z = (float)getFromArray2D(py_coords, type, i, 2) / 10.0;

		if (py_vel != NULL) {
			vx = (float)getFromArray2D(py_vel, vtype, i, 0);
			vy = (float)getFromArray2D(py_vel, vtype, i, 1);
			vz = (float)getFromArray2D(py_vel, vtype, i, 2);
       		fprintf(self->fd, "%5d%-5s%5s%5d%8.3f%8.3f%8.3f%8.4f%8.4f%8.4f\n",
				resid, resnam, sym, i+1, x, y, z, vx, vy, vz);
		} else {
        	fprintf(self->fd, "%5d%-5s%5s%5d%8.3f%8.3f%8.3f\n",
				resid, resnam, sym, i+1, x, y, z);
		}
    }
	/* Do some testing on the array */
	if (py_box != NULL) {
		type = PyArray_TYPE((PyArrayObject*)py_box);
		for (i = 0; i < 9; i++) {
			box[i] = (float)getFromArray2D(py_box, type,
						box_order[i][0], box_order[i][1]) / 10.0;
		}
		for (i = 0; i < 3; i++)
			fprintf(self->fd, "%10.5f", box[i]);
		if (fabs(box[3]) > 1e-6 || fabs(box[4]) > 1e-6 || fabs(box[5]) > 1e-6 ||
		    fabs(box[6]) > 1e-6 || fabs(box[7]) > 1e-6 || fabs(box[8]) > 1e-6)
			for (i = 3; i < 9; i++)
				fprintf(self->fd, "%10.5f", box[i]);
		fprintf(self->fd, "\n");
	} else
		fprintf(self->fd, "%10.5f%10.5f%10.5f\n", 0.0, 0.0, 0.0);

	return 0;
}



/* End of helper functions */


