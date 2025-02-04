#include "fd_builtin_programs.h"
#include "../fd_acc_mgr.h"
#include "../fd_system_ids.h"
#include "../fd_system_ids_pp.h"
#include <time.h>

/* BuiltIn programs need "bogus" executable accounts to exist.
   These are loaded and ignored during execution.

   Bogus accounts are marked as "executable", but their data is a
   hardcoded ASCII string. */

/* https://github.com/solana-labs/solana/blob/8f2c8b8388a495d2728909e30460aa40dcc5d733/sdk/src/native_loader.rs#L19 */
void
fd_write_builtin_account( fd_exec_slot_ctx_t * slot_ctx,
                          fd_pubkey_t const    pubkey,
                          char const *         data,
                          ulong                sz ) {

  fd_acc_mgr_t *      acc_mgr = slot_ctx->acc_mgr;
  fd_funk_txn_t *     txn     = slot_ctx->funk_txn;
  FD_BORROWED_ACCOUNT_DECL(rec);

  int err = fd_acc_mgr_modify( acc_mgr, txn, &pubkey, 1, sz, rec);
  FD_TEST( !err );

  rec->meta->dlen            = sz;
  rec->meta->info.lamports   = 1UL;
  rec->meta->info.rent_epoch = 0UL;
  rec->meta->info.executable = 1;
  fd_memcpy( rec->meta->info.owner, fd_solana_native_loader_id.key, 32 );
  memcpy( rec->data, data, sz );

  slot_ctx->slot_bank.capitalization++;

  // err = fd_acc_mgr_commit( acc_mgr, rec, slot_ctx );
  FD_TEST( !err );
}

/* https://github.com/solana-labs/solana/blob/8f2c8b8388a495d2728909e30460aa40dcc5d733/runtime/src/inline_spl_token.rs#L74 */
/* TODO: move this somewhere more appropiate */
static void
write_inline_spl_native_mint_program_account( fd_exec_slot_ctx_t * slot_ctx ) {
  // really?! really!?
  fd_epoch_bank_t const * epoch_bank = fd_exec_epoch_ctx_epoch_bank( slot_ctx->epoch_ctx );
  if( epoch_bank->cluster_type != 3)
    return;

  fd_acc_mgr_t *      acc_mgr = slot_ctx->acc_mgr;
  fd_funk_txn_t *     txn     = slot_ctx->funk_txn;
  fd_pubkey_t const * key     = (fd_pubkey_t const *)&fd_solana_spl_native_mint_id;
  FD_BORROWED_ACCOUNT_DECL(rec);

  /* https://github.com/solana-labs/solana/blob/8f2c8b8388a495d2728909e30460aa40dcc5d733/runtime/src/inline_spl_token.rs#L86-L90 */
  static uchar const data[] = {
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  int err = fd_acc_mgr_modify( acc_mgr, txn, key, 1, sizeof(data), rec );
  FD_TEST( !err );

  rec->meta->dlen            = sizeof(data);
  rec->meta->info.lamports   = 1000000000UL;
  rec->meta->info.rent_epoch = 1UL;
  rec->meta->info.executable = 0;
  fd_memcpy( rec->meta->info.owner, fd_solana_spl_token_id.key, 32 );
  memcpy( rec->data, data, sizeof(data) );

  FD_TEST( !err );
}

void fd_builtin_programs_init( fd_exec_slot_ctx_t * slot_ctx ) {
  // https://github.com/anza-xyz/agave/blob/v2.0.1/runtime/src/bank/builtins/mod.rs#L33
  fd_builtin_program_t const * builtins = fd_builtins();
  for( ulong i=0UL; i<fd_num_builtins(); i++ ) {
    if( builtins[i].core_bpf_migration_config && FD_FEATURE_ACTIVE_OFFSET( slot_ctx, builtins[i].core_bpf_migration_config->enable_feature_offset ) ) {
      continue;
    } else if( builtins[i].enable_feature_offset!=NO_ENABLE_FEATURE_ID && !FD_FEATURE_ACTIVE_OFFSET( slot_ctx, builtins[i].enable_feature_offset ) ) {
      continue;
    } else {
      fd_write_builtin_account( slot_ctx, *builtins[i].pubkey, builtins[i].data, strlen(builtins[i].data) );
    }
  }

  //TODO: remove when no longer necessary
  if( FD_FEATURE_ACTIVE( slot_ctx, zk_token_sdk_enabled ) ) {
    fd_write_builtin_account( slot_ctx, fd_solana_zk_token_proof_program_id, "zk_token_proof_program", 22UL );
  }

  if( FD_FEATURE_ACTIVE( slot_ctx, zk_elgamal_proof_program_enabled ) ) {
    fd_write_builtin_account( slot_ctx, fd_solana_zk_elgamal_proof_program_id, "zk_elgamal_proof_program", 24UL );
  }

  /* Precompiles have empty account data */
  if( slot_ctx->epoch_ctx->epoch_bank.cluster_version[0]<2 ) {
    char data[1] = {1};
    fd_write_builtin_account( slot_ctx, fd_solana_keccak_secp_256k_program_id, data, 1 );
    fd_write_builtin_account( slot_ctx, fd_solana_ed25519_sig_verify_program_id, data, 1 );
    if( FD_FEATURE_ACTIVE( slot_ctx, enable_secp256r1_precompile ) )
      fd_write_builtin_account( slot_ctx, fd_solana_secp256r1_program_id, data, 1 );
  } else {
    fd_write_builtin_account( slot_ctx, fd_solana_keccak_secp_256k_program_id, "", 0 );
    fd_write_builtin_account( slot_ctx, fd_solana_ed25519_sig_verify_program_id, "", 0 );
    if( FD_FEATURE_ACTIVE( slot_ctx, enable_secp256r1_precompile ) )
      fd_write_builtin_account( slot_ctx, fd_solana_secp256r1_program_id, "", 0 );
  }

  /* Inline SPL token mint program ("inlined to avoid an external dependency on the spl-token crate") */
  write_inline_spl_native_mint_program_account( slot_ctx );
}

fd_builtin_program_t const *
fd_builtins( void ) {
  return builtin_programs;
}

ulong
fd_num_builtins( void ) {
  return BUILTIN_PROGRAMS_COUNT;
}

fd_stateless_builtin_program_t const *
fd_stateless_builtins( void ) {
  return stateless_programs_builtins;
}

ulong
fd_num_stateless_builtins( void ) {
  return STATELESS_BUILTINS_COUNT;
}
