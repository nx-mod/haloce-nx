/* (tools/test_mcc_fast.py) the game's basics, natively */
#ifndef MCC_FAST_SHIM_CSERIES_H
#define MCC_FAST_SHIM_CSERIES_H
#include <string.h>
#include <stdio.h>
typedef int boolean;
typedef unsigned char byte;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define csmemset memset
#define csmemcpy memcpy
#endif
