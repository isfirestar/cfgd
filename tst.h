#if !defined NSPTST_HEAD
#define NSPTST_HEAD

#include "compiler.h"

#include <string.h>

#pragma pack(push, 1)

typedef struct {
    uint32_t op_;
    uint32_t cb_;
} nsp__tst_head_t;

#pragma pack(pop)

const unsigned char NSPDEF_OPCODE[4] = {'N', 's', 'p', 'd'};

#if _WIN32
__forceinline nsp_status_t STDCALL nsp__tst_parser(void *dat, int cb, int *pkt_cb)
#else
static inline nsp_status_t STDCALL nsp__tst_parser(void *dat, int cb, int *pkt_cb)
#endif
{
    nsp__tst_head_t *head = (nsp__tst_head_t *) dat;

    if (!head) return NSP_STATUS_FATAL;

    if (0 != memcmp(NSPDEF_OPCODE, &head->op_, sizeof (NSPDEF_OPCODE))) {
        return NSP_STATUS_FATAL;
    }

    *pkt_cb = head->cb_;
    return NSP_STATUS_SUCCESSFUL;
}

#if _WIN32
__forceinline nsp_status_t STDCALL nsp__tst_builder(void *dat, int cb)
#else
static inline nsp_status_t STDCALL nsp__tst_builder(void *dat, int cb)
#endif
{
    nsp__tst_head_t *head = (nsp__tst_head_t *) dat;

    if (!dat || cb <= 0) {
        return NSP_STATUS_FATAL;
    }

    memcpy(&head->op_, NSPDEF_OPCODE, sizeof (NSPDEF_OPCODE));
    head->cb_ = cb;
    return NSP_STATUS_SUCCESSFUL;
}

#endif
