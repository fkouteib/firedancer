#ifndef HEADER_fd_src_discof_store_fd_store_h
#define HEADER_fd_src_discof_store_fd_store_h

#include "../../flamenco/runtime/fd_blockstore.h"
#include "../../flamenco/runtime/fd_runtime.h"

#include "util.h"
#include "fd_pending_slots.h"

#define FD_STORE_SLOT_PREPARE_CONTINUE            (0)
#define FD_STORE_SLOT_PREPARE_NEED_ORPHAN         (1)
#define FD_STORE_SLOT_PREPARE_NEED_REPAIR         (2)
#define FD_STORE_SLOT_PREPARE_NEED_PARENT_EXEC    (3)
#define FD_STORE_SLOT_PREPARE_ALREADY_EXECUTED    (4)

/* The standard amount of time that we wait before repeating a slot */
#define FD_REPAIR_BACKOFF_TIME ( (long)150e6 )

struct fd_repair_backoff {
  ulong slot;
  long last_backoff_duration;
  long last_repair_time;
};
typedef struct fd_repair_backoff fd_repair_backoff_t;
static const fd_acct_addr_t chkdup_null_addr = {{ 0 }};

#define MAP_NAME              fd_repair_backoff_map
#define MAP_T                 fd_repair_backoff_t
#define MAP_KEY_T             ulong
#define MAP_KEY               slot
#define MAP_KEY_NULL          FD_SLOT_NULL
#define MAP_KEY_INVAL(k)      MAP_KEY_EQUAL(k, FD_SLOT_NULL)
#define MAP_KEY_EQUAL(k0,k1)  (k0==k1)
#define MAP_KEY_EQUAL_IS_SLOW 0
#define MAP_MEMOIZE           0
#define MAP_KEY_HASH(key)     ((uint)fd_ulong_hash( key ))
#define MAP_LG_SLOT_CNT       14
#include "../../util/tmpl/fd_map.c"


struct __attribute__((aligned(128UL))) fd_store {
  long now;            /* Current time */

  /* metadata */
  ulong snapshot_slot; /* the snapshot slot */
  ulong first_turbine_slot;  /* the first turbine slot we received on startup */
  ulong curr_turbine_slot;
  ulong curr_pack_slot;
  ulong root;
  ulong expected_shred_version;

  fd_repair_backoff_t repair_backoff_map[ 1UL<<15UL ];
  /* external joins */
  fd_blockstore_t *     blockstore;
  fd_valloc_t           valloc;

  /* internal joins */
  fd_pending_slots_t * pending_slots;
};
typedef struct fd_store fd_store_t;

FD_PROTOTYPES_BEGIN

FD_FN_CONST static inline ulong
fd_store_align( void ) {
  return alignof( fd_store_t );
}

FD_FN_CONST static inline ulong
fd_store_footprint( void ) {
  return sizeof( fd_store_t ) + fd_pending_slots_footprint();
}

void *
fd_store_new( void * mem, ulong lo_wmark_slot );

fd_store_t *
fd_store_join( void * store );

void *
fd_store_leave( fd_store_t const * store );

void *
fd_store_delete( void * store );

void
fd_store_expected_shred_version( fd_store_t * store, ulong expected_shred_version );

int
fd_store_slot_prepare( fd_store_t *   store,
                       ulong          slot,
                       ulong *        repair_slot_out );

int
fd_store_set_pack_slot( fd_store_t *   store,
                        ulong          slot );

int
fd_store_shred_insert( fd_store_t * store,
                       fd_shred_t const * shred );

void
fd_store_add_pending( fd_store_t * store,
                      ulong slot,
                      long delay,
                      int should_backoff,
                      int reset_backoff );
void
fd_store_set_root( fd_store_t * store,
                   ulong        root );

ulong
fd_store_slot_repair( fd_store_t * store,
                      ulong slot,
                      fd_repair_request_t * out_repair_reqs,
                      ulong out_repair_reqs_sz );

void
fd_store_shred_update_with_shred_from_turbine( fd_store_t * store,
                                               fd_shred_t const * shred );

FD_PROTOTYPES_END

#endif /* HEADER_fd_src_discof_store_fd_store_h */
