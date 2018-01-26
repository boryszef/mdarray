#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <float.h>
#include "CUnit/Basic.h"
#include "mdarray.h"
#include "utils.h"

static FILE* tmpFile = NULL;

int init_utils(void) {
	/*if ((tmpFile = mkstemp("test_utils_")) == -1) {
		perror("init_utils");
      return -1;
   }*/
	srand((int)getpid());
	return 0;
}

int clean_utils(void) {
   /*if (!fclose(tmpFile)) {
		perror("clean_utils");
      return -1;
   }
   tmpFile = NULL;*/
   return 0;
}

void testLOWERCASE(void) {
	const char template[] = "abcd efghi\t 01234\njklmno  !@#$\% pqrst";
	char *buffer;
	int buflen;
	int i, j, pos, result;
	
	buflen = strlen(template);
	buffer = (char*) malloc((buflen+1) * sizeof(char));
	for (i = 0; i < 10; i++) {
		strcpy(buffer, template);
		// Change randomly 5 characters
		for (j = 0; j < 5; j++) {
			pos = (float)rand()/RAND_MAX*buflen;
			buffer[pos] = toupper(buffer[pos]);
		}
		result = make_lowercase(buffer);
		CU_ASSERT(result >= 0);
		CU_ASSERT(result <= 5);
		CU_ASSERT(strcmp(template, buffer) == 0);
	}
}

void testSTRIPLINE(void) {
	const char *templates[10] = {
		"\t\tabcd", "\tabcd   ", "\nabcd\t", "\nabcd", "  abcd",
		" abcd\n", "  ab\nd  ", "ab\tc\n", "\tab\td\t", "a  b" };
	char buffer[20];
	int buflen;
	int i, result;
	
	for (i = 0; i < 10; i++) {
		strcpy(buffer, templates[i]);
		buflen = stripline(buffer);
		CU_ASSERT(buflen == 4);
	}
}

void testSTRPARTFLOAT(void) {
	const char *templates[10] = {
		"\t\ta9.99bcd",
		"\tab-1.11cd   ",
		"\nab1cd\t",
		"\nab0cd",
		"  a-1bcd",
		" ab9e+9cd\n",
		"  a   -9e-9b\nd  ",
		"ab\t  33333  c\n",
		"\tab-4.44   \td\t",
		"a  0.00001b" };
	const struct {
		int start;
		int len;
		float val;
	} data[] = {
		{ 3, 4, 9.99 },
		{ 3, 5, -1.11 },
		{ 3, 1, 1.0 },
		{ 3, 1, 0.0 },
		{ 3, 2, -1.0 },
		{ 3, 4, 9.0e+9 },
		{ 3, 8, -9.0e-9 },
		{ 3, 9, 33333.0 },
		{ 3, 8, -4.44 },
		{ 3, 7, 0.00001 } };
	int i;
	float result;
	
	for (i = 0; i < 10; i++) {
		result = strPartFloat(templates[i], data[i].start, data[i].len);
		CU_ASSERT(fabs(result - data[i].val) < FLT_EPSILON);
	}
}

void testBYSYMBOL(void) {
	const struct {
		const char *sym;
		int id;
	} symbols[] = {
		{ "H", 0 }, { "He", 1 }, { "C", 5 }, { "Zn", 29 },
		{ "Uus", 116 }, { "Uuo", 117 }, { "", -1 }, { "Xyz", -1 }};
	int i, result;
	
	for (i = 0; i < 8; i++) {
		result = getElementIndexBySymbol(symbols[i].sym);
		CU_ASSERT(result == symbols[i].id);
	}
}

void testGETFROM2D(void) {
	ARRAY_REAL value, diff;
	void *xyz;
	int i, nPoints = 10;
	npy_intp dims[2];
	PyObject *points = NULL;
	int type;
	const int npy_types[] = { NPY_HALF, NPY_FLOAT, NPY_DOUBLE, NPY_LONGDOUBLE };
	long double epsilon;

	dims[0] = nPoints;
	dims[1] = 3;

	for (i = 0; i < 4; i++) {
		type = npy_types[i];
		switch(type) {
			case NPY_HALF:
				epsilon = 0.0010004;
				xyz = (npy_half*) malloc(3 * nPoints * sizeof(npy_half));
				break;
			case NPY_FLOAT:
				epsilon = 1e-06;
				xyz = (float*) malloc(3 * nPoints * sizeof(float));
				break;
			case NPY_DOUBLE:
				epsilon = 1e-15;
				xyz = (double*) malloc(3 * nPoints * sizeof(double));
				break;
			case NPY_LONGDOUBLE:
				epsilon = 1e-18;
				xyz = (long double*) malloc(3 * nPoints * sizeof(long double));
				break;
		}
		if(xyz == NULL) return;

		for (i = 0; i < nPoints*3; i++) {
			switch(type) {
				case NPY_HALF:
					((npy_half*)xyz)[i] = (npy_half)rand()/RAND_MAX-0.5;
					break;
				case NPY_FLOAT:
					((float*)xyz)[i] = (float)rand()/RAND_MAX-0.5;
					break;
				case NPY_DOUBLE:
					((double*)xyz)[i] = (double)rand()/RAND_MAX-0.5;
					break;
				case NPY_LONGDOUBLE:
					((long double*)xyz)[i] = (long double)rand()/RAND_MAX-0.5;
					break;
			}
		}

		switch(type) {
			case NPY_HALF:
				points = PyArray_SimpleNewFromData(2, dims, type, (npy_half*) xyz);
				break;
			case NPY_FLOAT:
				points = PyArray_SimpleNewFromData(2, dims, type, (float*) xyz);
				break;
			case NPY_DOUBLE:
				points = PyArray_SimpleNewFromData(2, dims, type, (double*) xyz);
				break;
			case NPY_LONGDOUBLE:
				points = PyArray_SimpleNewFromData(2, dims, type, (long double*) xyz);
				break;
		}

		for (i = 0; i < nPoints; i++) {
			value = getFromArray2D(points, type, i, 0);
			printf("type %d, i %d = %f\n", type, i, value);
			switch(type) {
				case NPY_HALF:
					diff = fabs(((npy_half*)xyz)[i*3] - value);
					break;
				case NPY_FLOAT:
					diff = fabs(((float*)xyz)[i*3] - value);
					break;
				case NPY_DOUBLE:
					diff = fabs(((double*)xyz)[i*3] - value);
					break;
				case NPY_LONGDOUBLE:
					diff = fabs(((long double*)xyz)[i*3] - value);
					break;
			}
			CU_ASSERT(diff < epsilon);
		}

		Py_DECREF(points);
		points = NULL;
	}
}

int main() {
   CU_pSuite pSuite = NULL;

	/* initialize the CUnit test registry */
   if (CUE_SUCCESS != CU_initialize_registry()) return CU_get_error();

   /* add a suite to the registry */
   pSuite = CU_add_suite("Utils", init_utils, clean_utils);
   if (pSuite == NULL) {
      CU_cleanup_registry();
      return CU_get_error();
   }

   /* add the tests to the suite */
   if (CU_add_test(pSuite, "test of make_lowercase()", testLOWERCASE) == NULL) {
      CU_cleanup_registry();
      return CU_get_error();
   }

   if (CU_add_test(pSuite, "test of stripline()", testSTRIPLINE) == NULL) {
      CU_cleanup_registry();
      return CU_get_error();
   }

   if (CU_add_test(pSuite, "test of strPartFloat()", testSTRPARTFLOAT) == NULL) {
      CU_cleanup_registry();
      return CU_get_error();
   }

   if (CU_add_test(pSuite, "test of getElementIndexBySymbol()", testBYSYMBOL) == NULL) {
      CU_cleanup_registry();
      return CU_get_error();
   }

   if (CU_add_test(pSuite, "test of getFromArray2D()", testGETFROM2D) == NULL) {
      CU_cleanup_registry();
      return CU_get_error();
   }

   /* Run all tests using the CUnit Basic interface */
   CU_basic_set_mode(CU_BRM_VERBOSE);
   CU_basic_run_tests();
   CU_cleanup_registry();
   return CU_get_error();
}
