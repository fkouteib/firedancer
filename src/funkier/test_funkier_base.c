#include "fd_funkier.h"

FD_STATIC_ASSERT( FD_FUNKIER_SUCCESS               == 0,                              unit_test );
FD_STATIC_ASSERT( FD_FUNKIER_ERR_INVAL             ==-1,                              unit_test );
FD_STATIC_ASSERT( FD_FUNKIER_ERR_XID               ==-2,                              unit_test );
FD_STATIC_ASSERT( FD_FUNKIER_ERR_KEY               ==-3,                              unit_test );
FD_STATIC_ASSERT( FD_FUNKIER_ERR_FROZEN            ==-4,                              unit_test );
FD_STATIC_ASSERT( FD_FUNKIER_ERR_TXN               ==-5,                              unit_test );
FD_STATIC_ASSERT( FD_FUNKIER_ERR_REC               ==-6,                              unit_test );
FD_STATIC_ASSERT( FD_FUNKIER_ERR_MEM               ==-7,                              unit_test );

FD_STATIC_ASSERT( FD_FUNKIER_REC_KEY_ALIGN         ==8UL,                             unit_test );
FD_STATIC_ASSERT( FD_FUNKIER_REC_KEY_FOOTPRINT     ==40UL,                            unit_test );

FD_STATIC_ASSERT( FD_FUNKIER_REC_KEY_ALIGN         ==alignof(fd_funkier_rec_key_t),      unit_test );
FD_STATIC_ASSERT( FD_FUNKIER_REC_KEY_FOOTPRINT     ==sizeof (fd_funkier_rec_key_t),      unit_test );

FD_STATIC_ASSERT( FD_FUNKIER_TXN_XID_ALIGN         ==8UL,                             unit_test );
FD_STATIC_ASSERT( FD_FUNKIER_TXN_XID_FOOTPRINT     ==16UL,                            unit_test );

FD_STATIC_ASSERT( FD_FUNKIER_TXN_XID_ALIGN         ==alignof(fd_funkier_txn_xid_t),      unit_test );
FD_STATIC_ASSERT( FD_FUNKIER_TXN_XID_FOOTPRINT     ==sizeof (fd_funkier_txn_xid_t),      unit_test );

FD_STATIC_ASSERT( FD_FUNKIER_XID_KEY_PAIR_ALIGN    ==8UL,                             unit_test );
FD_STATIC_ASSERT( FD_FUNKIER_XID_KEY_PAIR_FOOTPRINT==56UL,                            unit_test );

FD_STATIC_ASSERT( FD_FUNKIER_XID_KEY_PAIR_ALIGN    ==alignof(fd_funkier_xid_key_pair_t), unit_test );
FD_STATIC_ASSERT( FD_FUNKIER_XID_KEY_PAIR_FOOTPRINT==sizeof (fd_funkier_xid_key_pair_t), unit_test );

static FD_TL ulong unique_tag = 0UL;

static fd_funkier_rec_key_t *
fd_funkier_rec_key_set_unique( fd_funkier_rec_key_t * key ) {
  key->ul[0] = fd_log_app_id();
  key->ul[1] = fd_log_thread_id();
  key->ul[2] = ++unique_tag;
# if FD_HAS_X86
  key->ul[3] = (ulong)fd_tickcount();
# else
  key->ul[3] = 0UL;
# endif
  key->ul[4] = ~key->ul[0];
  return key;
}

static fd_funkier_xid_key_pair_t *
fd_funkier_xid_key_pair_set_unique( fd_funkier_xid_key_pair_t * pair ) {
  pair->xid[0] = fd_funkier_generate_xid();
  fd_funkier_rec_key_set_unique( pair->key );
  return pair;
}

int
main( int     argc,
      char ** argv ) {
  fd_boot( &argc, &argv );

  FD_TEST( !strcmp( fd_funkier_strerror( FD_FUNKIER_SUCCESS    ), "success" ) );
  FD_TEST( !strcmp( fd_funkier_strerror( FD_FUNKIER_ERR_INVAL  ), "inval"   ) );
  FD_TEST( !strcmp( fd_funkier_strerror( FD_FUNKIER_ERR_XID    ), "xid"     ) );
  FD_TEST( !strcmp( fd_funkier_strerror( FD_FUNKIER_ERR_KEY    ), "key"     ) );
  FD_TEST( !strcmp( fd_funkier_strerror( FD_FUNKIER_ERR_FROZEN ), "frozen"  ) );
  FD_TEST( !strcmp( fd_funkier_strerror( FD_FUNKIER_ERR_TXN    ), "txn"     ) );
  FD_TEST( !strcmp( fd_funkier_strerror( FD_FUNKIER_ERR_REC    ), "rec"     ) );
  FD_TEST( !strcmp( fd_funkier_strerror( FD_FUNKIER_ERR_MEM    ), "mem"     ) );
  FD_TEST( !strcmp( fd_funkier_strerror( 1                  ), "unknown" ) );

  for( ulong rem=1000000UL; rem; rem-- ) {
    fd_funkier_rec_key_t a[1]; fd_funkier_rec_key_set_unique( a );
    fd_funkier_rec_key_t b[1]; fd_funkier_rec_key_set_unique( b );

    ulong hash = fd_funkier_rec_key_hash( a, 1234UL ); FD_COMPILER_FORGET( hash );
    /**/  hash = fd_funkier_rec_key_hash( b, 1234UL ); FD_COMPILER_FORGET( hash );

    FD_TEST( fd_funkier_rec_key_eq( a, a )==1 ); FD_TEST( fd_funkier_rec_key_eq( a, b )==0 );
    FD_TEST( fd_funkier_rec_key_eq( b, a )==0 ); FD_TEST( fd_funkier_rec_key_eq( b, b )==1 );

    FD_TEST( fd_funkier_rec_key_copy( b, a )==b );

    FD_TEST( fd_funkier_rec_key_eq( a, a )==1 ); FD_TEST( fd_funkier_rec_key_eq( a, b )==1 );
    FD_TEST( fd_funkier_rec_key_eq( b, a )==1 ); FD_TEST( fd_funkier_rec_key_eq( b, b )==1 );
  }

  fd_funkier_txn_xid_t z[1];
  FD_TEST( fd_funkier_txn_xid_set_root( z )==z );
  FD_TEST( fd_funkier_txn_xid_eq_root ( z )==1 );
  FD_TEST( !(z->ul[0] | z->ul[1]) );

  for( ulong rem=1000000UL; rem; rem-- ) {
    fd_funkier_txn_xid_t a[1]; a[0] = fd_funkier_generate_xid();
    fd_funkier_txn_xid_t b[1]; b[0] = fd_funkier_generate_xid();

    ulong hash = fd_funkier_txn_xid_hash( a, 1234UL ); FD_COMPILER_FORGET( hash );
    /**/  hash = fd_funkier_txn_xid_hash( b, 1234UL ); FD_COMPILER_FORGET( hash );

    FD_TEST( fd_funkier_txn_xid_eq_root( a )==0 );
    FD_TEST( fd_funkier_txn_xid_eq_root( b )==0 );
    FD_TEST( fd_funkier_txn_xid_eq_root( z )==1 );
    FD_TEST( fd_funkier_txn_xid_eq( a, a )==1 ); FD_TEST( fd_funkier_txn_xid_eq( a, b )==0 ); FD_TEST( fd_funkier_txn_xid_eq( a, z )==0 );
    FD_TEST( fd_funkier_txn_xid_eq( b, a )==0 ); FD_TEST( fd_funkier_txn_xid_eq( b, b )==1 ); FD_TEST( fd_funkier_txn_xid_eq( b, z )==0 );
    FD_TEST( fd_funkier_txn_xid_eq( z, a )==0 ); FD_TEST( fd_funkier_txn_xid_eq( z, b )==0 ); FD_TEST( fd_funkier_txn_xid_eq( z, z )==1 );
    FD_TEST( !(z->ul[0] | z->ul[1] ) );

    FD_TEST( fd_funkier_txn_xid_copy( b, a )==b );

    FD_TEST( fd_funkier_txn_xid_eq_root( a )==0 );
    FD_TEST( fd_funkier_txn_xid_eq_root( b )==0 );
    FD_TEST( fd_funkier_txn_xid_eq_root( z )==1 );
    FD_TEST( fd_funkier_txn_xid_eq( a, a )==1 ); FD_TEST( fd_funkier_txn_xid_eq( a, b )==1 ); FD_TEST( fd_funkier_txn_xid_eq( a, z )==0 );
    FD_TEST( fd_funkier_txn_xid_eq( b, a )==1 ); FD_TEST( fd_funkier_txn_xid_eq( b, b )==1 ); FD_TEST( fd_funkier_txn_xid_eq( b, z )==0 );
    FD_TEST( fd_funkier_txn_xid_eq( z, a )==0 ); FD_TEST( fd_funkier_txn_xid_eq( z, b )==0 ); FD_TEST( fd_funkier_txn_xid_eq( z, z )==1 );
    FD_TEST( !(z->ul[0] | z->ul[1] ) );

    FD_TEST( fd_funkier_txn_xid_copy( a, z )==a );

    FD_TEST( fd_funkier_txn_xid_eq_root( a )==1 );
    FD_TEST( fd_funkier_txn_xid_eq_root( b )==0 );
    FD_TEST( fd_funkier_txn_xid_eq_root( z )==1 );
    FD_TEST( fd_funkier_txn_xid_eq( a, a )==1 ); FD_TEST( fd_funkier_txn_xid_eq( a, b )==0 ); FD_TEST( fd_funkier_txn_xid_eq( a, z )==1 );
    FD_TEST( fd_funkier_txn_xid_eq( b, a )==0 ); FD_TEST( fd_funkier_txn_xid_eq( b, b )==1 ); FD_TEST( fd_funkier_txn_xid_eq( b, z )==0 );
    FD_TEST( fd_funkier_txn_xid_eq( z, a )==1 ); FD_TEST( fd_funkier_txn_xid_eq( z, b )==0 ); FD_TEST( fd_funkier_txn_xid_eq( z, z )==1 );
    FD_TEST( !(z->ul[0] | z->ul[1]) );
  }

  for( ulong rem=1000000UL; rem; rem-- ) {
    fd_funkier_xid_key_pair_t a[1]; fd_funkier_xid_key_pair_set_unique( a );
    fd_funkier_xid_key_pair_t b[1]; fd_funkier_xid_key_pair_set_unique( b );

    ulong hash = fd_funkier_xid_key_pair_hash( a, 1234UL ); FD_COMPILER_FORGET( hash );
    /**/  hash = fd_funkier_xid_key_pair_hash( b, 1234UL ); FD_COMPILER_FORGET( hash );

    FD_TEST( fd_funkier_xid_key_pair_eq( a, a )==1 ); FD_TEST( fd_funkier_xid_key_pair_eq( a, b )==0 );
    FD_TEST( fd_funkier_xid_key_pair_eq( b, a )==0 ); FD_TEST( fd_funkier_xid_key_pair_eq( b, b )==1 );

    FD_TEST( fd_funkier_xid_key_pair_copy( b, a )==b );

    FD_TEST( fd_funkier_xid_key_pair_eq( a, a )==1 ); FD_TEST( fd_funkier_xid_key_pair_eq( a, b )==1 );
    FD_TEST( fd_funkier_xid_key_pair_eq( b, a )==1 ); FD_TEST( fd_funkier_xid_key_pair_eq( b, b )==1 );

    fd_funkier_xid_key_pair_set_unique( a );
    fd_funkier_xid_key_pair_set_unique( b );

    FD_TEST( fd_funkier_xid_key_pair_init( b, a->xid, a->key )==b );
    FD_TEST( fd_funkier_xid_key_pair_eq( a, b ) );
  }

  FD_LOG_NOTICE(( "pass" ));
  fd_halt();
  return 0;
}
