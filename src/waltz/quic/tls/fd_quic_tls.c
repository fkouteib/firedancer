#include "fd_quic_tls.h"
#include "../../../ballet/ed25519/fd_x25519.h"
#include "../../../ballet/x509/fd_x509_mock.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>

/* fd_tls callbacks provided by fd_quic *******************************/

/* fd_quic_tls_sendmsg is called by fd_tls when fd_quic should send a
   CRYPTO frame to the peer.  Currently, we can assume that the
   encryption_level will never decrease (INITIAL => HANDSHAKE => APP) */

int
fd_quic_tls_sendmsg( void const * handshake,
                     void const * record,
                     ulong        record_sz,
                     uint         encryption_level,
                     int          flush );

/* fd_quic_tls_secrets is called by fd_tls when new encryption keys
   become available.  Currently, this is called at most two times per
   connection:  For the handshake secrets, and for the initial app-level
   secrets. */

void
fd_quic_tls_secrets( void const * handshake,
                     void const * recv_secret,
                     void const * send_secret,
                     uint         encryption_level );

/* fd_quic_tls_rand is the RNG provided to fd_tls.  Note: This is
   a layering violation ... The user should pass the CSPRNG handle to
   both fd_quic and fd_tls.  Currently, implemented via the getrandom()
   syscall ... Inefficient! */

void *
fd_quic_tls_rand( void * ctx,
                  void * buf,
                  ulong  bufsz );

/* fd_quic_tls_tp_self is called by fd_tls to retrieve fd_quic's QUIC
   transport parameters. */

ulong
fd_quic_tls_tp_self( void *  handshake,
                     uchar * quic_tp,
                     ulong   quic_tp_bufsz );

/* fd_quic_tls_tp_self is called by fd_tls to inform fd_quic of the
   peer's QUIC transport parameters. */

void
fd_quic_tls_tp_peer( void *        handshake,
                     uchar const * quic_tp,
                     ulong         quic_tp_sz );

/* fd_quic_tls lifecycle API ******************************************/

static void
fd_quic_tls_init( fd_tls_t *    tls,
                  fd_tls_sign_t signer,
                  uchar const   cert_public_key[ static 32 ] );

fd_quic_tls_t *
fd_quic_tls_new( fd_quic_tls_t *     self,
                 fd_quic_tls_cfg_t * cfg ) {

  if( FD_UNLIKELY( !self ) ) {
    FD_LOG_WARNING(( "NULL mem" ));
    return NULL;
  }
  if( FD_UNLIKELY( !cfg ) ) {
    FD_LOG_WARNING(( "NULL cfg" ));
    return NULL;
  }
  if( FD_UNLIKELY( (!cfg->secret_cb            ) |
                   (!cfg->handshake_complete_cb) |
                   (!cfg->peer_params_cb       ) ) ) {
    FD_LOG_WARNING(( "Missing callbacks" ));
    return NULL;
  }

  self->secret_cb             = cfg->secret_cb;
  self->handshake_complete_cb = cfg->handshake_complete_cb;
  self->peer_params_cb        = cfg->peer_params_cb;

  /* Initialize fd_tls */
  fd_quic_tls_init( &self->tls, cfg->signer, cfg->cert_public_key );

  return self;
}

/* fd_quic_tls_init is called as part of fd_quic_tls_new.  It sets up
   the embedded fd_tls instance. */

static void
fd_quic_tls_init( fd_tls_t *    tls,
                  fd_tls_sign_t signer,
                  uchar const   cert_public_key[ static 32 ] ) {
  tls = fd_tls_new( tls );
  *tls = (fd_tls_t) {
    .quic = 1,
    .rand = {
      .ctx     = NULL,
      .rand_fn = fd_quic_tls_rand
    },
    .sign = signer,
    .secrets_fn = fd_quic_tls_secrets,
    .sendmsg_fn = fd_quic_tls_sendmsg,

    .quic_tp_self_fn = fd_quic_tls_tp_self,
    .quic_tp_peer_fn = fd_quic_tls_tp_peer,
  };

  /* Generate X25519 key */
  if( FD_UNLIKELY( !fd_rng_secure( tls->kex_private_key, 32UL ) ) )
    FD_LOG_ERR(( "fd_rng_secure failed: %s", fd_io_strerror( errno ) ));
  fd_x25519_public( tls->kex_public_key, tls->kex_private_key );

  /* Set up Ed25519 key */
  fd_memcpy( tls->cert_public_key, cert_public_key, 32UL );

  /* Generate X.509 cert */
  fd_x509_mock_cert( tls->cert_x509, tls->cert_public_key );
  tls->cert_x509_sz = FD_X509_MOCK_CERT_SZ;

  /* Set ALPN protocol ID
     (Technically, don't need to copy the length prefix but we'll do
      so anyways.) */
  tls->alpn[ 0 ] = 0x0a;
  memcpy( tls->alpn+1, "solana-tpu", 11UL );
  tls->alpn_sz = 11UL;
}

void *
fd_quic_tls_delete( fd_quic_tls_t * self ) {
  if( FD_UNLIKELY( !self ) ) {
    FD_LOG_WARNING(( "NULL self" ));
    return NULL;
  }
  return self;
}

fd_quic_tls_hs_t *
fd_quic_tls_hs_new( fd_quic_tls_hs_t * self,
                    fd_quic_tls_t *    quic_tls,
                    void *             context,
                    int                is_server,
                    fd_quic_transport_params_t const * self_transport_params,
                    ulong              now ) {
  // clear the handshake bits
  fd_memset( self, 0, sizeof(fd_quic_tls_hs_t) );

  // set properties on self
  self->quic_tls  = quic_tls;
  self->is_server = is_server;
  self->context   = context;

  /* initialize handshake data */

  /* init free list */
  self->hs_data_free_idx = 0u; /* head points at first */
  for( ushort j = 0u; j < FD_QUIC_TLS_HS_DATA_CNT; ++j ) {
    if( j < FD_QUIC_TLS_HS_DATA_CNT-1u ) {
      self->hs_data[j].next_idx = (ushort)(j+1u); /* each point to next */
    } else {
      self->hs_data[j].next_idx = FD_QUIC_TLS_HS_DATA_UNUSED ;
    }
  }

  /* no data pending */
  for( unsigned j = 0; j < 4; ++j ) {
    self->hs_data_pend_idx[j]     = FD_QUIC_TLS_HS_DATA_UNUSED;
    self->hs_data_pend_end_idx[j] = FD_QUIC_TLS_HS_DATA_UNUSED;
  }

  /* clear hs_data_buf */
  self->hs_data_buf_ptr = 0;

  /* all handshake offsets start at zero */
  fd_memset( self->hs_data_offset, 0, sizeof( self->hs_data_offset ) );

  /* Set QUIC transport params */
  self->self_transport_params = *self_transport_params;

  if( is_server ) {
    fd_tls_estate_srv_new( &self->hs.srv );
  } else {
    fd_tls_estate_cli_new( &self->hs.cli );
    long res = fd_tls_client_handshake( &quic_tls->tls, &self->hs.cli, NULL, 0UL, 0 );
    if( FD_UNLIKELY( res<0L ) ) {
      self->alert = (uint)-res;
    }
  }

  self->birthtime = now;

  return self;
}

void
fd_quic_tls_hs_delete( fd_quic_tls_hs_t * self ) {
  if( !self ) return;

  if( self->is_server )
    fd_tls_estate_srv_delete( &self->hs.srv );
  else
    fd_tls_estate_cli_delete( &self->hs.cli );
}

int
fd_quic_tls_process( fd_quic_tls_hs_t * self ) {

  if( FD_UNLIKELY( self->hs.base.state==FD_TLS_HS_FAIL ) ) return FD_QUIC_FAILED;
  if( self->hs.base.state==FD_TLS_HS_CONNECTED ) return FD_QUIC_SUCCESS;

  /* Process all fully received messages */

  uint enc_level = self->rx_enc_level;
  for(;;) {
    uchar const * buf   = self->rx_hs_buf;
    ulong         off   = self->rx_off;
    ulong         avail = self->rx_sz - off;
    if( avail<4 ) break;

    /* Peek the message size from fd_tls_msg_hdr_t
       ?? AA BB CC => 0xCCBBAA?? => 0x??AABBCC => 0x00AABBCC */
    uint msg_sz = fd_uint_bswap( FD_LOAD( uint, buf+off ) ) & 0xFFFFFFU;
    if( avail<msg_sz+4 ) break;

    long res = fd_tls_handshake( &self->quic_tls->tls, &self->hs, buf+off, avail, enc_level );

    if( FD_UNLIKELY( res<0L ) ) {
      int alert = (int)-res;
      self->alert = (uint)alert;
      return FD_QUIC_FAILED;
    }
    if( FD_UNLIKELY( res==0UL ) ) {
      FD_LOG_WARNING(( "preventing deadlock" ));
      return FD_QUIC_FAILED;
    }

    self->rx_off = (ushort)( off+(ulong)res );
  }

  switch( self->hs.base.state ) {
  case FD_TLS_HS_CONNECTED:
    /* handshake completed */
    self->quic_tls->handshake_complete_cb( self, self->context );
    return FD_QUIC_SUCCESS;
  case FD_TLS_HS_FAIL:
    /* handshake permanently failed */
    return FD_QUIC_FAILED;
  default:
    /* handshake not yet complete */
    return FD_QUIC_SUCCESS;
  }
}

/* internal callbacks */

int
fd_quic_tls_sendmsg( void const * handshake,
                     void const * data,
                     ulong        data_sz,
                     uint         enc_level,
                     int          flush FD_PARAM_UNUSED ) {

  uint buf_sz = FD_QUIC_TLS_HS_DATA_SZ;
  if( data_sz > buf_sz ) {
    return 0;
  }

  /* Safe because the fd_tls_estate_{srv,cli}_t object is the first
     element of fd_quic_tls_hs_t */
  fd_quic_tls_hs_t * hs = (fd_quic_tls_hs_t *)handshake;

  /* add handshake data to handshake for retrieval by user */

  /* find free handshake data */
  ushort hs_data_idx = hs->hs_data_free_idx;
  if( hs_data_idx == FD_QUIC_TLS_HS_DATA_UNUSED ) {
    /* no free structures left. fail */
    return 0;
  }

  /* allocate enough space from hs data buffer */
  uint ptr           = hs->hs_data_buf_ptr;
  uint alloc_data_sz = fd_uint_align_up( (uint)data_sz, FD_QUIC_TLS_HS_DATA_ALIGN );
  if( ptr + alloc_data_sz > FD_QUIC_TLS_HS_DATA_SZ ) {
    /* not enough space */
    return 0;
  }

  /* success */

  fd_quic_tls_hs_data_t * hs_data = &hs->hs_data[hs_data_idx];
  uchar *                 buf     = &hs->hs_data_buf[ptr];

  /* update free list */
  hs->hs_data_free_idx = hs_data->next_idx;

  /* write back new buf ptr */
  hs->hs_data_buf_ptr = ptr + alloc_data_sz;

  /* copy data into buffer, and update metadata in hs_data */
  fd_memcpy( buf, data, data_sz );
  hs_data->enc_level    = enc_level;
  hs_data->data         = buf;
  hs_data->data_sz      = (uint)data_sz;
  hs_data->offset       = hs->hs_data_offset[enc_level];

  /* offset adjusted ready for more data */
  hs->hs_data_offset[enc_level] += (uint)data_sz;

  /* add to end of pending list */
  hs_data->next_idx = FD_QUIC_TLS_HS_DATA_UNUSED;
  ulong pend_end_idx = hs->hs_data_pend_end_idx[enc_level];
  if( pend_end_idx == FD_QUIC_TLS_HS_DATA_UNUSED  ) {
    /* pending list is empty */
    hs->hs_data_pend_end_idx[enc_level] = hs->hs_data_pend_idx[enc_level] = hs_data_idx;
  } else {
    /* last element must point to next */
    hs->hs_data[pend_end_idx].next_idx  = hs_data_idx;
    hs->hs_data_pend_end_idx[enc_level] = hs_data_idx;
  }

  return 1;
}

void
fd_quic_tls_secrets( void const * handshake,
                     void const * recv_secret,
                     void const * send_secret,
                     uint         enc_level ) {

  fd_quic_tls_hs_t * hs = (fd_quic_tls_hs_t *)handshake;

  fd_quic_tls_secret_t secret = { .enc_level = enc_level };
  memcpy( secret.read_secret,  recv_secret, 32UL );
  memcpy( secret.write_secret, send_secret, 32UL );

  hs->quic_tls->secret_cb( hs, hs->context, &secret );
}

fd_quic_tls_hs_data_t *
fd_quic_tls_get_hs_data( fd_quic_tls_hs_t * self,
                         uint               enc_level ) {
  if( !self ) return NULL;

  uint idx = self->hs_data_pend_idx[enc_level];
  if( idx == FD_QUIC_TLS_HS_DATA_UNUSED ) return NULL;

  return &self->hs_data[idx];
}

fd_quic_tls_hs_data_t *
fd_quic_tls_get_next_hs_data( fd_quic_tls_hs_t * self, fd_quic_tls_hs_data_t * hs ) {
  ushort idx = hs->next_idx;
  if( idx == (ushort)(~0u) ) return NULL;
  return self->hs_data + idx;
}

void
fd_quic_tls_pop_hs_data( fd_quic_tls_hs_t * self, uint enc_level ) {
  ushort idx = self->hs_data_pend_idx[enc_level];
  if( idx == FD_QUIC_TLS_HS_DATA_UNUSED ) return;

  fd_quic_tls_hs_data_t * hs_data = &self->hs_data[idx];

  /* pop from pending list */
  self->hs_data_pend_idx[enc_level] = hs_data->next_idx;

  /* if idx is the last, update last */
  if( hs_data->next_idx == FD_QUIC_TLS_HS_DATA_UNUSED ) {
    self->hs_data_pend_end_idx[enc_level] = FD_QUIC_TLS_HS_DATA_UNUSED;
  }
}

void
fd_quic_tls_clear_hs_data( fd_quic_tls_hs_t * self, uint enc_level ) {
  self->hs_data_pend_idx[enc_level] = FD_QUIC_TLS_HS_DATA_UNUSED;
  self->hs_data_pend_end_idx[enc_level] = FD_QUIC_TLS_HS_DATA_UNUSED;
}

void *
fd_quic_tls_rand( void * ctx,
                  void * buf,
                  ulong  bufsz ) {
  (void)ctx;
  FD_TEST( fd_rng_secure( buf, bufsz ) );
  return buf;
}

ulong
fd_quic_tls_tp_self( void *  const handshake,
                     uchar * const quic_tp,
                     ulong   const quic_tp_bufsz ) {
  fd_quic_tls_hs_t * hs = (fd_quic_tls_hs_t *)handshake;

  ulong encoded_sz = fd_quic_encode_transport_params( quic_tp, quic_tp_bufsz, &hs->self_transport_params );
  if( FD_UNLIKELY( encoded_sz==FD_QUIC_ENCODE_FAIL ) ) {
    FD_LOG_WARNING(( "fd_quic_encode_transport_params failed" ));
    return 0UL;
  }

  return encoded_sz;
}

void
fd_quic_tls_tp_peer( void *        handshake,
                     uchar const * quic_tp,
                     ulong         quic_tp_sz ) {
  /* Callback issued by fd_tls.  Bubble up callback to fd_quic_tls. */

  fd_quic_tls_hs_t * hs       = (fd_quic_tls_hs_t *)handshake;
  fd_quic_tls_t *    quic_tls = hs->quic_tls;

  quic_tls->peer_params_cb( hs->context, quic_tp, quic_tp_sz );
}
