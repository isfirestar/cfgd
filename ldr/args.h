#if !defined GLOBAL_ARGS_H
#define GLOBAL_ARGS_H

#include "compiler.h"

enum opt_invisible_indx {
    kInvisibleOptIndex_GetHelp = 'h',
    kInvisibleOptIndex_GetVersion = 'v',
    kInvisibleOptIndex_Reorganization = 0x0100, /* b:(H)0000 0000 0000 0000 0000 0001 0000 0000(L) */
    kInvisibleOptIndex_ConfigFilePath = 0x0400,
};

extern int arg_analyze_input(int argc, char **argv);
extern uint16_t arg_query_port();
extern const char *arg_query_cfgfile();

#endif
