/*   $Source: bitbucket.org:berkeleylab/gasnet.git/ofi-conduit/gasnet_core_internal.h $
 * Description: GASNet libfabric (OFI) conduit header for internal definitions in Core API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Copyright 2015, Intel Corporation
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet_internal.h>
#include <gasnet_handler.h>

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

/* ------------------------------------------------------------------------------------ */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
#define _hidx_gasnetc_auxseg_reqh             (GASNETC_HANDLER_BASE+0)
#define _hidx_gasnetc_exit_reqh               (GASNETC_HANDLER_BASE+1)
/* add new core API handlers here and to the bottom of gasnet_core.c */

#ifndef GASNETE_HANDLER_BASE
  #define GASNETE_HANDLER_BASE  64 /* reserve 64-127 for the extended API */
#elif GASNETE_HANDLER_BASE != 64
  #error "GASNETE_HANDLER_BASE mismatch between core and extended"
#endif

/* ------------------------------------------------------------------------------------ */
/* handler table (recommended impl) */
#define GASNETC_MAX_NUMHANDLERS   256
extern gasneti_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS];


/* ------------------------------------------------------------------------------------ */
/* AM category (recommended impl if supporting PSHM) */
typedef enum {
  gasnetc_Short=0,
  gasnetc_Medium=1,
  gasnetc_Long=2
} gasnetc_category_t;

#if GASNETI_STATS_OR_TRACE
#define GASNETC_TRACE_WAIT_BEGIN() \
    gasneti_tick_t _waitstart = GASNETI_TICKS_NOW_IFENABLED(C)
#else
#define GASNETC_TRACE_WAIT_BEGIN() \
    static char _dummy = (char)sizeof(_dummy)
#endif

#define GASNETC_TRACE_WAIT_END(name) \
    GASNETI_TRACE_EVENT_TIME(C,name,gasneti_ticks_now() - _waitstart)

#define GASNETC_STAT_EVENT(name) \
    _GASNETI_STAT_EVENT(C,name)
#define GASNETC_STAT_EVENT_VAL(name,val) \
    _GASNETI_STAT_EVENT_VAL(C,name,val)

/* These definitions are just to make the polling of the two different AM
 * networks easier to read and understand*/
#define OFI_POLL_ALL   1
#define OFI_POLL_REPLY 0

/* ------------------------------------------------------------------------------------ */
/* Job Spawn / Bootstrap */

extern gasneti_spawnerfn_t const *gasneti_spawner;

#define gasneti_bootstrapBarrier        (*(gasneti_spawner->Barrier))
#define gasneti_bootstrapExchange       (*(gasneti_spawner->Exchange))
#define gasneti_bootstrapBroadcast      (*(gasneti_spawner->Broadcast))
#define gasneti_bootstrapSNodeBroadcast (*(gasneti_spawner->SNodeBroadcast))
#define gasneti_bootstrapAlltoall       (*(gasneti_spawner->Alltoall))
#define gasneti_bootstrapAbort          (*(gasneti_spawner->Abort))
#define gasneti_bootstrapCleanup        (*(gasneti_spawner->Cleanup))
#define gasneti_bootstrapFini           (*(gasneti_spawner->Fini))

#endif
