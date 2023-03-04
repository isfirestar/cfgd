#include "args.h"

#include <getopt.h>
#include <stdio.h>
#include <string.h>

#include "getopt.h"

#include "jessfs.h"

#include "ifos.h"

#define DEF_TCP_PORT       (4407)

static const struct option long_options[] = {
    {"help", no_argument, NULL, kInvisibleOptIndex_GetHelp},
    {"version", no_argument, NULL, kInvisibleOptIndex_GetVersion},
    {"reorganization", no_argument, NULL, kInvisibleOptIndex_Reorganization},
    {"ServicePort", required_argument, NULL, kInvisibleOptIndex_ServicePort },
    {"ConfigFilePath", required_argument, NULL, kInvisibleOptIndex_ConfigFilePath },
    {NULL, 0, NULL, 0}
};

static struct {
    uint16_t port;
    ifos_path_buffer_t cfgfile;
} __arg_parameters = { .port = DEF_TCP_PORT };

static void arg_display_usage()
{
    static const char *usage_context =
            "NAME\n"
            "\tjess - Json Environment Simulation Service\n"
            "\n"
            "SYNOPSIS\n"
            "\tjess\n"
            "\t\t[-h|--help] display usage context and help informations\n"
            "\t\t[-v|--version] display versions of executable archive\n"
            "\t\t[-r|--reorganization] reorganization configure from /etc/agv/ base directory\n"
            "\t\t[-P|--ServicePort] change the service TCP port of this jess instance\n"
            "\t\t[-f|--ConfigFilePath] set/change the program self config file\n"
            ;

    printf("%s", usage_context);
}

static void arg_display_author_information()
{
    char author_context[512];

    sprintf(author_context,
            "jess %s"POSIX__EOL
            "Copyright (C) 2019 Zhejiang Guozi RoboticsCo., Ltd."POSIX__EOL
            "For bug reporting instructions, please see:"POSIX__EOL
            "<http://www.gzrobot.com/>."POSIX__EOL
            "Find the AGV manual and other documentation resources online at:"POSIX__EOL
            "<http://www.gzrobot.com/productCenter/>."POSIX__EOL
            "For help, type \"help\"."POSIX__EOL,
            procfs_get_version()
            );

    printf("%s", author_context);
}

void arg_query_port(uint16_t *port)
{
    if (port) {
        *port = __arg_parameters.port;
    }
}

const char *arg_query_cfgfile()
{
    return __arg_parameters.cfgfile.st;
}

int arg_analyze_input(int argc, char **argv)
{
    int retval;
    char shortopts[128];
    int opt;
    int opt_index;

    retval = 0;
    sprintf(shortopts, "vhrP:f:");
    opt = getopt_long(argc, argv, shortopts, long_options, &opt_index);
    while (opt != -1) {
        switch (opt) {
			case kInvisibleOptIndex_Reorganization:
            case 'r':
                retval |= kInvisibleOptIndex_Reorganization;
                break;
			case kInvisibleOptIndex_ServicePort:
            case 'P':
                if (optarg) {
                    __arg_parameters.port = (uint16_t)strtoul(optarg, NULL, 10);
                    retval |= kInvisibleOptIndex_ServicePort;
                }
                break;
            case kInvisibleOptIndex_ConfigFilePath:
            case 'f':
                if (optarg) {
                    abuff_strcpy(&__arg_parameters.cfgfile, optarg);
                    retval |= kInvisibleOptIndex_ConfigFilePath;
                }
                break;
            case 'v':
                retval = -1;
                arg_display_author_information();
                break;
            case 'h':
            case '?':
            case 0:
            default:
                retval = -1;
                arg_display_usage();
                break;
        }
        opt = getopt_long(argc, argv, shortopts, long_options, &opt_index);
    }

    return retval;
}
