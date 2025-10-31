#include "fd_progcache_admin.h"
#include "fd_progcache_rec.h"

/* Algorithm to estimate size of cache metadata structures (rec_pool
   object pool and rec_map hashchain table).

   FIXME Carefully balance this */

static ulong
fd_progcache_est_rec_max1( ulong wksp_footprint,
                           ulong mean_cache_entry_size ) {
  return wksp_footprint / mean_cache_entry_size;
}

ulong
fd_progcache_est_rec_max( ulong wksp_footprint,
                          ulong mean_cache_entry_size ) {
  ulong est = fd_progcache_est_rec_max1( wksp_footprint, mean_cache_entry_size );
  if( FD_UNLIKELY( est>(1UL<<31) ) ) FD_LOG_ERR(( "fd_progcache_est_rec_max(wksp_footprint=%lu,mean_cache_entry_size=%lu) failed: invalid parameters", wksp_footprint, mean_cache_entry_size ));
  return fd_ulong_max( est, 2048UL );
}

fd_progcache_admin_t *
fd_progcache_admin_join( fd_progcache_admin_t * ljoin,
                         void *                 shfunk ) {
  if( FD_UNLIKELY( !ljoin ) ) {
    FD_LOG_WARNING(( "NULL ljoin" ));
    return NULL;
  }
  if( FD_UNLIKELY( !shfunk ) ) {
    FD_LOG_WARNING(( "NULL shfunk" ));
    return NULL;
  }

  memset( ljoin, 0, sizeof(fd_progcache_admin_t) );
  if( FD_UNLIKELY( !fd_funk_join( ljoin->funk, shfunk ) ) ) {
    FD_LOG_CRIT(( "fd_funk_join failed" ));
  }

  return ljoin;
}

void *
fd_progcache_admin_leave( fd_progcache_admin_t * ljoin,
                          void **                opt_shfunk ) {
  if( FD_UNLIKELY( !ljoin ) ) FD_LOG_CRIT(( "NULL ljoin" ));

  if( FD_UNLIKELY( !fd_funk_leave( ljoin->funk, opt_shfunk ) ) ) FD_LOG_CRIT(( "fd_funk_leave failed" ));

  return ljoin;
}

/* Begin transaction-level operations.  It is assumed that funk_txn data
   structures are not concurrently modified.  This includes txn_pool and
   txn_map. */

void
fd_progcache_txn_attach_child( fd_progcache_admin_t *    cache,
                               fd_funk_txn_xid_t const * xid_parent,
                               fd_funk_txn_xid_t const * xid_new ) {
  FD_LOG_INFO(( "progcache txn laddr=%p xid %lu:%lu: created with parent %lu:%lu",
                (void *)cache->funk,
                xid_new   ->ul[0], xid_new   ->ul[1],
                xid_parent->ul[0], xid_parent->ul[1] ));
  fd_funk_txn_prepare( cache->funk, xid_parent, xid_new );
}

static void
fd_progcache_txn_cancel_one( fd_progcache_admin_t * cache,
                             fd_funk_txn_t *        txn ) {
  FD_LOG_INFO(( "progcache txn laddr=%p xid %lu:%lu: cancel", (void *)txn, txn->xid.ul[0], txn->xid.ul[1] ));

  fd_funk_t * funk = cache->funk;
  if( FD_UNLIKELY( !fd_funk_txn_idx_is_null( txn->child_head_cidx ) ||
                   !fd_funk_txn_idx_is_null( txn->child_tail_cidx ) ) ) {
    FD_LOG_CRIT(( "fd_progcache_txn_cancel failed: txn at %p with xid %lu:%lu has children (data corruption?)",
                  (void *)txn, txn->xid.ul[0], txn->xid.ul[1] ));
  }

  /* Phase 1: Drain users from transaction */

  fd_rwlock_write( txn->lock );
  FD_VOLATILE( txn->state ) = FD_FUNK_TXN_STATE_CANCEL;

  /* Phase 2: Remove records */

  while( !fd_funk_rec_idx_is_null( txn->rec_head_idx ) ) {
    fd_funk_rec_t * rec = &funk->rec_pool->ele[ txn->rec_head_idx ];
    uint next_idx = rec->next_idx;
    rec->next_idx = FD_FUNK_REC_IDX_NULL;
    if( FD_LIKELY( !fd_funk_rec_idx_is_null( next_idx ) ) ) {
      funk->rec_pool->ele[ next_idx ].prev_idx = FD_FUNK_REC_IDX_NULL;
    }

    fd_funk_val_flush( rec, funk->alloc, funk->wksp );

    fd_funk_rec_query_t query[1];
    int remove_err = fd_funk_rec_map_remove( funk->rec_map, &rec->pair, NULL, query, FD_MAP_FLAG_BLOCKING );
    if( FD_UNLIKELY( remove_err ) ) FD_LOG_CRIT(( "fd_funk_rec_map_remove failed: %i-%s", remove_err, fd_map_strerror( remove_err ) ));

    fd_funk_rec_pool_release( funk->rec_pool, rec, 1 );

    txn->rec_head_idx = next_idx;
    if( fd_funk_rec_idx_is_null( next_idx ) ) txn->rec_tail_idx = FD_FUNK_REC_IDX_NULL;
  }

  /* Phase 3: Remove transaction from fork graph */

  uint self_cidx = fd_funk_txn_cidx( (ulong)( txn-funk->txn_pool->ele ) );
  uint prev_cidx = txn->sibling_prev_cidx; ulong prev_idx = fd_funk_txn_idx( prev_cidx );
  uint next_cidx = txn->sibling_next_cidx; ulong next_idx = fd_funk_txn_idx( next_cidx );
  if( !fd_funk_txn_idx_is_null( next_idx ) ) {
    funk->txn_pool->ele[ next_idx ].sibling_prev_cidx = prev_cidx;
  }
  if( !fd_funk_txn_idx_is_null( prev_idx ) ) {
    funk->txn_pool->ele[ prev_idx ].sibling_next_cidx = next_cidx;
  }
  if( !fd_funk_txn_idx_is_null( fd_funk_txn_idx( txn->parent_cidx ) ) ) {
    fd_funk_txn_t * parent = &funk->txn_pool->ele[ fd_funk_txn_idx( txn->parent_cidx ) ];
    if( parent->child_head_cidx==self_cidx ) parent->child_head_cidx = next_cidx;
    if( parent->child_tail_cidx==self_cidx ) parent->child_tail_cidx = prev_cidx;
  } else {
    if( funk->shmem->child_head_cidx==self_cidx ) funk->shmem->child_head_cidx = next_cidx;
    if( funk->shmem->child_tail_cidx==self_cidx ) funk->shmem->child_tail_cidx = prev_cidx;
  }

  /* Phase 4: Remove transcation from index */

  fd_funk_txn_map_query_t query[1];
  int remove_err = fd_funk_txn_map_remove( funk->txn_map, &txn->xid, NULL, query, FD_MAP_FLAG_BLOCKING );
  if( FD_UNLIKELY( remove_err!=FD_MAP_SUCCESS ) ) {
    FD_LOG_CRIT(( "fd_progcache_txn_cancel failed: fd_funk_txn_map_remove(%lu:%lu) failed: %i-%s",
                  txn->xid.ul[0], txn->xid.ul[1], remove_err, fd_map_strerror( remove_err ) ));
  }

  /* Phase 5: Free transaction object */

  fd_rwlock_unwrite( txn->lock );
  FD_VOLATILE( txn->state ) = FD_FUNK_TXN_STATE_FREE;
  fd_funk_txn_pool_release( funk->txn_pool, txn, 1 );
}

/* Cancels txn and all children */

static void
fd_progcache_txn_cancel_tree( fd_progcache_admin_t * cache,
                              fd_funk_txn_t *        txn ) {
  for(;;) {
    ulong child_idx = fd_funk_txn_idx( txn->child_head_cidx );
    if( fd_funk_txn_idx_is_null( child_idx ) ) break;
    fd_funk_txn_t * child = &cache->funk->txn_pool->ele[ child_idx ];
    fd_progcache_txn_cancel_tree( cache, child );
  }
  fd_progcache_txn_cancel_one( cache, txn );
}

/* Cancels all left/right siblings */

static void
fd_progcache_txn_cancel_prev_list( fd_progcache_admin_t * cache,
                                   fd_funk_txn_t *        txn ) {
  ulong self_idx = (ulong)( txn - cache->funk->txn_pool->ele );
  for(;;) {
    ulong prev_idx = fd_funk_txn_idx( txn->sibling_prev_cidx );
    if( FD_UNLIKELY( prev_idx==self_idx ) ) FD_LOG_CRIT(( "detected cycle in fork graph" ));
    if( fd_funk_txn_idx_is_null( prev_idx ) ) break;
    fd_funk_txn_t * sibling = &cache->funk->txn_pool->ele[ prev_idx ];
    fd_progcache_txn_cancel_tree( cache, sibling );
  }
}

static void
fd_progcache_txn_cancel_next_list( fd_progcache_admin_t * cache,
                                   fd_funk_txn_t *        txn ) {
  ulong self_idx = (ulong)( txn - cache->funk->txn_pool->ele );
  for(;;) {
    ulong next_idx = fd_funk_txn_idx( txn->sibling_next_cidx );
    if( FD_UNLIKELY( next_idx==self_idx ) ) FD_LOG_CRIT(( "detected cycle in fork graph" ));
    if( fd_funk_txn_idx_is_null( next_idx ) ) break;
    fd_funk_txn_t * sibling = &cache->funk->txn_pool->ele[ next_idx ];
    fd_progcache_txn_cancel_tree( cache, sibling );
  }
}

void
fd_progcache_txn_cancel( fd_progcache_admin_t * cache,
                         fd_funk_txn_xid_t const * xid ) {
  fd_funk_t * funk = cache->funk;

  fd_funk_txn_t * txn = fd_funk_txn_query( xid, funk->txn_map );
  if( FD_UNLIKELY( !txn ) ) {
    FD_LOG_CRIT(( "fd_progcache_txn_cancel failed: txn with xid %lu:%lu not found", xid->ul[0], xid->ul[1] ));
  }

  fd_progcache_txn_cancel_next_list( cache, txn );
  fd_progcache_txn_cancel_tree( cache, txn );
}

/* fd_progcache_gc_root cleans up a stale "rooted" version of a
   record. */

static void
fd_progcache_gc_root( fd_progcache_admin_t *         cache,
                      fd_funk_xid_key_pair_t const * pair ) {
  fd_funk_t * funk = cache->funk;

  /* Phase 1: Remove record from map if found */

  fd_funk_rec_query_t query[1];
  int rm_err = fd_funk_rec_map_remove( funk->rec_map, pair, NULL, query, FD_MAP_FLAG_BLOCKING );
  if( rm_err==FD_MAP_ERR_KEY ) return;
  if( FD_UNLIKELY( rm_err!=FD_MAP_SUCCESS ) ) FD_LOG_CRIT(( "fd_funk_rec_map_remove failed: %i-%s", rm_err, fd_map_strerror( rm_err ) ));

  /* Phase 2: Invalidate record */

  fd_funk_rec_t * old_rec = query->ele;
  memset( &old_rec->pair, 0, sizeof(fd_funk_xid_key_pair_t) );
  FD_COMPILER_MFENCE();

  /* Phase 3: Free record */

  old_rec->map_next = FD_FUNK_REC_IDX_NULL;
  fd_funk_val_flush( old_rec, funk->alloc, funk->wksp );
  fd_funk_rec_pool_release( funk->rec_pool, old_rec, 1 );
}

/* fd_progcache_gc_invalidation cleans up a "cache invalidate" record,
   which may not exist at the database root. */

static void
fd_progcache_gc_invalidation( fd_progcache_admin_t * cache,
                              fd_funk_rec_t *        rec ) {
  fd_funk_t * funk = cache->funk;

  /* Phase 1: Remove record from map if found */

  fd_funk_xid_key_pair_t pair = rec->pair;
  fd_funk_rec_query_t query[1];
  int rm_err = fd_funk_rec_map_remove( funk->rec_map, &pair, NULL, query, FD_MAP_FLAG_BLOCKING );
  if( rm_err==FD_MAP_ERR_KEY ) return;
  if( FD_UNLIKELY( rm_err!=FD_MAP_SUCCESS ) ) FD_LOG_CRIT(( "fd_funk_rec_map_remove failed: %i-%s", rm_err, fd_map_strerror( rm_err ) ));
  if( FD_UNLIKELY( query->ele!=rec ) ) {
    FD_LOG_CRIT(( "Found record collision in program cache: xid=%lu:%lu key=%016lx%016lx%016lx%016lx ele0=%u ele1=%u",
                  pair.xid->ul[0], pair.xid->ul[1],
                  fd_ulong_bswap( pair.key->ul[0] ),
                  fd_ulong_bswap( pair.key->ul[1] ),
                  fd_ulong_bswap( pair.key->ul[2] ),
                  fd_ulong_bswap( pair.key->ul[3] ),
                  (uint)( query->ele - funk->rec_pool->ele ),
                  (uint)( rec        - funk->rec_pool->ele ) ));
  }

  /* Phase 2: Invalidate record */

  memset( &rec->pair, 0, sizeof(fd_funk_xid_key_pair_t) );
  FD_COMPILER_MFENCE();

  /* Phase 3: Free record */

  rec->map_next = FD_FUNK_REC_IDX_NULL;
  fd_funk_val_flush( rec, funk->alloc, funk->wksp );
  fd_funk_rec_pool_release( funk->rec_pool, rec, 1 );
}

/* fd_progcache_publish_recs publishes all of a progcache's records.
   It is assumed at this point that the txn has no more concurrent
   users. */

static void
fd_progcache_publish_recs( fd_progcache_admin_t * cache,
                           fd_funk_txn_t *        txn ) {
  /* Iterate record list */
  uint head = txn->rec_head_idx;
  txn->rec_head_idx = FD_FUNK_REC_IDX_NULL;
  txn->rec_tail_idx = FD_FUNK_REC_IDX_NULL;
  while( !fd_funk_rec_idx_is_null( head ) ) {
    fd_funk_rec_t * rec = &cache->funk->rec_pool->ele[ head ];

    /* Evict previous value from hash chain */
    fd_funk_xid_key_pair_t pair[1];
    fd_funk_rec_key_copy( pair->key, rec->pair.key );
    fd_funk_txn_xid_set_root( pair->xid );
    fd_progcache_gc_root( cache, pair );
    uint next = rec->next_idx;

    fd_progcache_rec_t * prec = fd_funk_val( rec, cache->funk->wksp );
    FD_TEST( prec );
    if( FD_UNLIKELY( prec->invalidate ) ) {
      /* Drop cache invalidate records */
      fd_progcache_gc_invalidation( cache, rec );
    } else {
      /* Migrate record to root */
      rec->prev_idx = FD_FUNK_REC_IDX_NULL;
      rec->next_idx = FD_FUNK_REC_IDX_NULL;
      fd_funk_txn_xid_t const root = { .ul = { ULONG_MAX, ULONG_MAX } };
      fd_funk_txn_xid_st_atomic( rec->pair.xid, &root );
    }

    head = next; /* next record */
  }
}

/* fd_progcache_txn_publish_one merges an in-prep transaction whose
   parent is the last published, into the parent. */

static void
fd_progcache_txn_publish_one( fd_progcache_admin_t *    cache,
                              fd_funk_txn_xid_t const * xid ) {
  fd_funk_t * funk = cache->funk;

  /* Phase 1: Mark transaction as "last published" */

  fd_funk_txn_t * txn = fd_funk_txn_query( xid, funk->txn_map );
  if( FD_UNLIKELY( !txn ) ) {
    FD_LOG_CRIT(( "fd_progcache_publish failed: txn with xid %lu:%lu not found", xid->ul[0], xid->ul[1] ));
  }
  FD_LOG_INFO(( "progcache txn laddr=%p xid %lu:%lu: publish", (void *)txn, txn->xid.ul[0], txn->xid.ul[1] ));
  if( FD_UNLIKELY( !fd_funk_txn_idx_is_null( fd_funk_txn_idx( txn->parent_cidx ) ) ) ) {
    FD_LOG_CRIT(( "fd_progcache_publish failed: txn with xid %lu:%lu is not a child of the last published txn", xid->ul[0], xid->ul[1] ));
  }
  fd_funk_txn_xid_st_atomic( funk->shmem->last_publish, xid );

  /* Phase 2: Drain users from transaction */

  fd_rwlock_write( txn->lock );
  FD_VOLATILE( txn->state ) = FD_FUNK_TXN_STATE_PUBLISH;

  /* Phase 3: Migrate records */

  fd_progcache_publish_recs( cache, txn );

  /* Phase 4: Remove transaction from fork graph

     Because the transaction has no more records, removing it from the
     fork graph has no visible side effects to concurrent query ops
     (always return "no found") or insert ops (refuse to write to a
     "publish" state txn). */

  { /* Adjust the parent pointers of the children to point to "last published" */
    ulong child_idx = fd_funk_txn_idx( txn->child_head_cidx );
    while( FD_UNLIKELY( !fd_funk_txn_idx_is_null( child_idx ) ) ) {
      funk->txn_pool->ele[ child_idx ].parent_cidx = fd_funk_txn_cidx( FD_FUNK_TXN_IDX_NULL );
      child_idx = fd_funk_txn_idx( funk->txn_pool->ele[ child_idx ].sibling_next_cidx );
    }
  }

  /* Phase 5: Remove transaction from index

     The transaction is now an orphan and won't get any new records. */

  fd_funk_txn_map_query_t query[1];
  int remove_err = fd_funk_txn_map_remove( funk->txn_map, xid, NULL, query, 0 );
  if( FD_UNLIKELY( remove_err!=FD_MAP_SUCCESS ) ) {
    FD_LOG_CRIT(( "fd_progcache_publish failed: fd_funk_txn_map_remove failed: %i-%s", remove_err, fd_map_strerror( remove_err ) ));
  }

  /* Phase 6: Free transaction object */

  fd_rwlock_unwrite( txn->lock );
  FD_VOLATILE( txn->state ) = FD_FUNK_TXN_STATE_FREE;
  txn->parent_cidx       = UINT_MAX;
  txn->sibling_prev_cidx = UINT_MAX;
  txn->sibling_next_cidx = UINT_MAX;
  txn->child_head_cidx   = UINT_MAX;
  txn->child_tail_cidx   = UINT_MAX;
  fd_funk_txn_pool_release( funk->txn_pool, txn, 1 );
}

void
fd_progcache_txn_advance_root( fd_progcache_admin_t *    cache,
                               fd_funk_txn_xid_t const * xid ) {
  fd_funk_t * funk = cache->funk;

  fd_funk_txn_t * txn = fd_funk_txn_query( xid, funk->txn_map );
  if( FD_UNLIKELY( !txn ) ) {
    FD_LOG_CRIT(( "fd_progcache_txn_advance_root failed: txn with xid %lu:%lu not found", xid->ul[0], xid->ul[1] ));
  }

  if( FD_UNLIKELY( !fd_funk_txn_idx_is_null( fd_funk_txn_idx( txn->parent_cidx ) ) ) ) {
    FD_LOG_CRIT(( "fd_progcache_txn_advance_root: parent of txn %lu:%lu is not root", xid->ul[0], xid->ul[1] ));
  }

  fd_progcache_txn_cancel_prev_list( cache, txn );
  fd_progcache_txn_cancel_next_list( cache, txn );
  txn->sibling_prev_cidx = UINT_MAX;
  txn->sibling_next_cidx = UINT_MAX;

  /* Children of transaction are now children of root */
  funk->shmem->child_head_cidx = txn->child_head_cidx;
  funk->shmem->child_tail_cidx = txn->child_tail_cidx;

  fd_progcache_txn_publish_one( cache, fd_funk_txn_xid( txn ) );
}

/* reset_txn_list does a depth-first traversal of the txn tree.
   Detaches all recs from txns by emptying rec linked lists. */

static void
reset_txn_list( fd_funk_t * funk,
                ulong       txn_head_idx ) {
  fd_funk_txn_pool_t * txn_pool = funk->txn_pool;
  for( ulong idx = txn_head_idx;
       !fd_funk_txn_idx_is_null( idx );
  ) {
    fd_funk_txn_t * txn = &txn_pool->ele[ idx ];
    fd_funk_txn_state_assert( txn, FD_FUNK_TXN_STATE_ACTIVE );
    txn->rec_head_idx = FD_FUNK_REC_IDX_NULL;
    txn->rec_tail_idx = FD_FUNK_REC_IDX_NULL;
    reset_txn_list( funk, txn->child_head_cidx );
    idx = fd_funk_txn_idx( txn->sibling_next_cidx );
  }
}

/* reset_rec_map frees all records in a funk instance. */

static void
reset_rec_map( fd_funk_t * funk ) {
  fd_wksp_t *          wksp     = funk->wksp;
  fd_alloc_t *         alloc    = funk->alloc;
  fd_funk_rec_map_t *  rec_map  = funk->rec_map;
  fd_funk_rec_pool_t * rec_pool = funk->rec_pool;

  ulong chain_cnt = fd_funk_rec_map_chain_cnt( rec_map );
  for( ulong chain_idx=0UL; chain_idx<chain_cnt; chain_idx++ ) {
    for(
        fd_funk_rec_map_iter_t iter = fd_funk_rec_map_iter( rec_map, chain_idx );
        !fd_funk_rec_map_iter_done( iter );
    ) {
      fd_funk_rec_t * rec = fd_funk_rec_map_iter_ele( iter );
      ulong next = fd_funk_rec_map_private_idx( rec->map_next );;

      /* Remove rec object from map */
      fd_funk_rec_map_query_t rec_query[1];
      int err = fd_funk_rec_map_remove( rec_map, fd_funk_rec_pair( rec ), NULL, rec_query, FD_MAP_FLAG_BLOCKING );
      fd_funk_rec_key_t key; fd_funk_rec_key_copy( &key, rec->pair.key );
      if( FD_UNLIKELY( err!=FD_MAP_SUCCESS ) ) FD_LOG_CRIT(( "fd_funk_rec_map_remove failed (%i-%s)", err, fd_map_strerror( err ) ));

      /* Free rec resources */
      fd_funk_val_flush( rec, alloc, wksp );
      fd_funk_rec_pool_release( rec_pool, rec, 1 );
      iter.ele_idx = next;
    }
  }
}

void
fd_progcache_reset( fd_progcache_admin_t * cache ) {
  fd_funk_t * funk = cache->funk;
  reset_txn_list( funk, fd_funk_txn_idx( funk->shmem->child_head_cidx ) );
  reset_rec_map( funk );
}

/* clear_txn_list does a depth-first traversal of the txn tree.
   Removes all txns. */

static void
clear_txn_list( fd_funk_t * funk,
                ulong       txn_head_idx ) {
  fd_funk_txn_pool_t * txn_pool = funk->txn_pool;
  fd_funk_txn_map_t *  txn_map  = funk->txn_map;
  for( ulong idx = txn_head_idx;
       !fd_funk_txn_idx_is_null( idx );
  ) {
    fd_funk_txn_t * txn = &txn_pool->ele[ idx ];
    fd_funk_txn_state_assert( txn, FD_FUNK_TXN_STATE_ACTIVE );
    ulong next_idx  = fd_funk_txn_idx( txn->sibling_next_cidx );
    ulong child_idx = fd_funk_txn_idx( txn->child_head_cidx );
    txn->rec_head_idx      = FD_FUNK_REC_IDX_NULL;
    txn->rec_tail_idx      = FD_FUNK_REC_IDX_NULL;
    txn->child_head_cidx   = UINT_MAX;
    txn->child_tail_cidx   = UINT_MAX;
    txn->parent_cidx       = UINT_MAX;
    txn->sibling_prev_cidx = UINT_MAX;
    txn->sibling_next_cidx = UINT_MAX;
    clear_txn_list( funk, child_idx );
    fd_funk_txn_map_query_t query[1];
    int rm_err = fd_funk_txn_map_remove( txn_map, &txn->xid, NULL, query, FD_MAP_FLAG_BLOCKING );
    if( FD_UNLIKELY( rm_err!=FD_MAP_SUCCESS ) ) FD_LOG_CRIT(( "fd_funk_txn_map_remove failed (%i-%s)", rm_err, fd_map_strerror( rm_err ) ));
    txn->state = FD_FUNK_TXN_STATE_FREE;
    int free_err = fd_funk_txn_pool_release( txn_pool, txn, 1 );
    if( FD_UNLIKELY( free_err!=FD_POOL_SUCCESS ) ) FD_LOG_CRIT(( "fd_funk_txn_pool_release failed (%i)", free_err ));
    idx = next_idx;
  }
  funk->shmem->child_head_cidx = UINT_MAX;
  funk->shmem->child_tail_cidx = UINT_MAX;
}

void
fd_progcache_clear( fd_progcache_admin_t * cache ) {
  fd_funk_t * funk = cache->funk;
  clear_txn_list( funk, fd_funk_txn_idx( funk->shmem->child_head_cidx ) );
  reset_rec_map( funk );
}

void
fd_progcache_verify( fd_progcache_admin_t * cache ) {
  FD_TEST( fd_funk_verify( cache->funk )==FD_FUNK_SUCCESS );
}
