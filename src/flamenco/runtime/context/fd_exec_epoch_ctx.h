#ifndef HEADER_fd_src_flamenco_runtime_context_fd_exec_epoch_ctx_h
#define HEADER_fd_src_flamenco_runtime_context_fd_exec_epoch_ctx_h

#include "../../features/fd_features.h"
#include "../../leaders/fd_leaders.h"
#include "../fd_bank_hash_cmp.h"
#include "../fd_rent_lists.h"

/* fd_exec_epoch_ctx_t is the context that stays constant throughout
   an entire epoch. */

struct fd_exec_epoch_ctx_layout {
  ulong vote_acc_max;
  ulong footprint;

  ulong stake_votes_off;
  ulong stake_delegations_off;
  ulong next_epoch_stakes_off;
  ulong leaders_off; /* Current epoch only */
};

typedef struct fd_exec_epoch_ctx_layout fd_exec_epoch_ctx_layout_t;

typedef struct fd_runtime_public fd_runtime_public_t;

struct __attribute__((aligned(64UL))) fd_exec_epoch_ctx {
  ulong                      magic; /* ==FD_EXEC_EPOCH_CTX_MAGIC */

  fd_exec_epoch_ctx_layout_t layout;

  fd_features_t              features;
  fd_epoch_bank_t            epoch_bank;

  fd_bank_hash_cmp_t *       bank_hash_cmp;
  fd_runtime_public_t *      runtime_public;
  int                        constipate_root; /* Used for constipation in offline replay. */
  ulong                      total_epoch_stake;
};

#define FD_EXEC_EPOCH_CTX_ALIGN (alignof(fd_exec_epoch_ctx_t))
#define FD_EXEC_EPOCH_CTX_MAGIC (0x3E64F44C9F44366AUL) /* random */

FD_PROTOTYPES_BEGIN

void *
fd_exec_epoch_ctx_new( void * mem,
                       ulong  vote_acc_max );

fd_exec_epoch_ctx_t *
fd_exec_epoch_ctx_join( void * mem );

void *
fd_exec_epoch_ctx_leave( fd_exec_epoch_ctx_t * ctx );

void *
fd_exec_epoch_ctx_delete( void * mem );

void
fd_exec_epoch_ctx_epoch_bank_delete( fd_exec_epoch_ctx_t * epoch_ctx );

ulong
fd_exec_epoch_ctx_align( void );

ulong
fd_exec_epoch_ctx_footprint( ulong vote_acc_max );

/* fd_exec_epoch_ctx_bank_mem_clear empties out the existing bank
   data structures (votes, delegations, stake history, next_epoch_stakes).
   This method should be used before decoding a bank from funk so as
   to not step on the work done while decoding.
*/
void
fd_exec_epoch_ctx_bank_mem_clear( fd_exec_epoch_ctx_t * epoch_ctx );

/* fd_exec_epoch_ctx_bank_mem_setup initializes the bank
   data structures (votes, delegations, stake history, next_epoch_stakes)
   to have the correct pool initialization and layout.
*/
fd_epoch_bank_t *
fd_exec_epoch_ctx_bank_mem_setup( fd_exec_epoch_ctx_t * epoch_ctx );

/* Accessors **********************************************************/

FD_FN_CONST static inline fd_epoch_bank_t *
fd_exec_epoch_ctx_epoch_bank( fd_exec_epoch_ctx_t * ctx ) {
  return &ctx->epoch_bank;
}

FD_FN_CONST static inline fd_epoch_bank_t const *
fd_exec_epoch_ctx_epoch_bank_const( fd_exec_epoch_ctx_t const * ctx ) {
  return &ctx->epoch_bank;
}

FD_FN_PURE static inline fd_vote_accounts_pair_t_mapnode_t *
fd_exec_epoch_ctx_stake_votes_join( fd_exec_epoch_ctx_t * ctx ) {
  void * mem = (void *)((ulong)ctx + ctx->layout.stake_votes_off);
  return fd_vote_accounts_pair_t_map_join( mem );
}

FD_FN_PURE static inline fd_delegation_pair_t_mapnode_t *
fd_exec_epoch_ctx_stake_delegations_join( fd_exec_epoch_ctx_t * ctx ) {
  void * mem = (void *)((ulong)ctx + ctx->layout.stake_delegations_off);
  return fd_delegation_pair_t_map_join( mem );
}

FD_FN_PURE static inline fd_vote_accounts_pair_t_mapnode_t *
fd_exec_epoch_ctx_next_epoch_stakes_join( fd_exec_epoch_ctx_t * ctx ) {
  void * mem = (void *)((ulong)ctx + ctx->layout.next_epoch_stakes_off);
  return fd_vote_accounts_pair_t_map_join( mem );
}

FD_FN_PURE static inline fd_epoch_leaders_t *
fd_exec_epoch_ctx_leaders( fd_exec_epoch_ctx_t * ctx ) {
  return (fd_epoch_leaders_t *)((uchar *)ctx + ctx->layout.leaders_off);
}

void
fd_exec_epoch_ctx_from_prev( fd_exec_epoch_ctx_t * self,
                             fd_exec_epoch_ctx_t * prev,
                             fd_spad_t *           runtime_spad );

FD_PROTOTYPES_END

#endif /* HEADER_fd_src_flamenco_runtime_context_fd_exec_epoch_ctx_h */
