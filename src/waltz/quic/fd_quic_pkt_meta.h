#ifndef HEADER_fd_src_waltz_quic_fd_quic_pkt_meta_h
#define HEADER_fd_src_waltz_quic_fd_quic_pkt_meta_h

#include "fd_quic_common.h"

typedef struct fd_quic_pkt_meta      fd_quic_pkt_meta_t;
typedef struct fd_quic_pkt_meta_list fd_quic_pkt_meta_list_t;
typedef struct fd_quic_pkt_meta_pool fd_quic_pkt_meta_pool_t;

/* TODO convert to a union with various types of metadata overlaid */

/* fd_quic_pkt_meta_var used for tracking max_data, max_stream_data and
 * max_streams
 *
 * type:      FD_QUIC_PKT_META_TYPE_STREAM_DATA
 *            FD_QUIC_PKT_META_TYPE_OTHER
 * flags:     FD_QUIC_PKT_META_FLAGS_*
 * value:     max_data          number of bytes
 *            max_stream_data   number of bytes
 *            max_streams       number of streams
 */
union fd_quic_pkt_meta_key {
  union {
#define FD_QUIC_PKT_META_STREAM_MASK ((1UL<<62UL)-1UL)
    ulong stream_id;
    struct {
      ulong flags:62;
      ulong type:2;
#define FD_QUIC_PKT_META_TYPE_OTHER           0UL
#define FD_QUIC_PKT_META_TYPE_STREAM_DATA     1UL
    };
#define FD_QUIC_PKT_META_KEY( TYPE, FLAGS, STREAM_ID ) \
    ((fd_quic_pkt_meta_key_t)                          \
     { .stream_id = ( ( (ulong)(STREAM_ID) )    |      \
                      ( (ulong)(TYPE) << 62UL ) |      \
                      ( (ulong)(FLAGS) ) ) } )
    /* FD_QUIC_PKT_META_STREAM_ID
     * This is used to extract the stream_id, since some of the bits are used
     * for "type".
     * The more natural way "stream_id:62" caused compilation warnings and ugly
     * work-arounds */
#define FD_QUIC_PKT_META_STREAM_ID( KEY ) ( (KEY).stream_id & FD_QUIC_PKT_META_STREAM_MASK )
  };
};
typedef union fd_quic_pkt_meta_key fd_quic_pkt_meta_key_t;

struct fd_quic_pkt_meta_var {
  fd_quic_pkt_meta_key_t key;
  union {
    ulong                value;
    fd_quic_range_t      range;
  };
};
typedef struct fd_quic_pkt_meta_var fd_quic_pkt_meta_var_t;

/* the max number of pkt_meta_var entries in pkt_meta
   this limits the number of max_data, max_stream_data and max_streams
   allowed in a single quic packet */
#define FD_QUIC_PKT_META_VAR_MAX 16

/* fd_quic_pkt_meta

   tracks the metadata of data sent to the peer
   used when acks arrive to determine what is being acked specifically */
struct fd_quic_pkt_meta {
  /* stores metadata about what was sent in the identified packet */
  ulong pkt_number;  /* packet number (in pn_space) */
  uchar enc_level;   /* encryption level of packet */
  uchar pn_space;    /* packet number space (derived from enc_level) */
  uchar var_sz;      /* number of populated entries in var */

  /* does/should the referenced packet contain:
       FD_QUIC_PKT_META_FLAGS_HS_DATA             handshake data
       FD_QUIC_PKT_META_FLAGS_STREAM              stream data
       FD_QUIC_PKT_META_FLAGS_HS_DONE             handshake-done frame
       FD_QUIC_PKT_META_FLAGS_MAX_DATA            max_data frame
       FD_QUIC_PKT_META_FLAGS_MAX_STREAMS_UNIDIR  max_streams frame (unidir)
       FD_QUIC_PKT_META_FLAGS_CLOSE               close frame
       FD_QUIC_PKT_META_FLAGS_PING                set to send a PING frame

     some of these flags are mutually exclusive */
  uint                   flags;       /* flags */
# define          FD_QUIC_PKT_META_FLAGS_HS_DATA            (1u<<0u)
# define          FD_QUIC_PKT_META_FLAGS_STREAM             (1u<<1u)
# define          FD_QUIC_PKT_META_FLAGS_HS_DONE            (1u<<2u)
# define          FD_QUIC_PKT_META_FLAGS_MAX_DATA           (1u<<3u)
# define          FD_QUIC_PKT_META_FLAGS_MAX_STREAMS_UNIDIR (1u<<4u)
# define          FD_QUIC_PKT_META_FLAGS_CLOSE              (1u<<5u)
# define          FD_QUIC_PKT_META_FLAGS_PING               (1u<<6u)
  fd_quic_range_t        range;       /* CRYPTO data range; FIXME use pkt_meta var instead */
  ulong                  stream_id;   /* if this contains stream data,
                                         the stream id, else zero */

  ulong                  tx_time;     /* transmit time */
  ulong                  expiry;      /* time pkt_meta expires... this is the time the
                                         ack is expected by */

  fd_quic_pkt_meta_var_t var[FD_QUIC_PKT_META_VAR_MAX];

  fd_quic_pkt_meta_t *   next;   /* next in current list */
};


struct fd_quic_pkt_meta_list {
  fd_quic_pkt_meta_t * head;
  fd_quic_pkt_meta_t * tail;
};


struct fd_quic_pkt_meta_pool {
  fd_quic_pkt_meta_list_t free;    /* free pkt_meta */

  /* one of each of these for each enc_level */
  fd_quic_pkt_meta_list_t sent_pkt_meta[4]; /* sent pkt_meta */
};



FD_PROTOTYPES_BEGIN

/* initialize pool with existing array of pkt_meta */
void
fd_quic_pkt_meta_pool_init( fd_quic_pkt_meta_pool_t * pool,
                            fd_quic_pkt_meta_t * pkt_meta_array,
                            ulong                pkt_meta_array_sz );

/* pop from front of list */
fd_quic_pkt_meta_t *
fd_quic_pkt_meta_pop_front( fd_quic_pkt_meta_list_t * list );


/* push onto front of list */
void
fd_quic_pkt_meta_push_front( fd_quic_pkt_meta_list_t * list,
                             fd_quic_pkt_meta_t *      pkt_meta );

/* push onto back of list */
void
fd_quic_pkt_meta_push_back( fd_quic_pkt_meta_list_t * list,
                            fd_quic_pkt_meta_t *      pkt_meta );

/* remove from list
   requires the prior element */
void
fd_quic_pkt_meta_remove( fd_quic_pkt_meta_list_t * list,
                         fd_quic_pkt_meta_t *      pkt_meta_prior,
                         fd_quic_pkt_meta_t *      pkt_meta );


/* allocate a pkt_meta
   obtains a free pkt_meta from the free list, and returns it
   returns NULL if none is available */
fd_quic_pkt_meta_t *
fd_quic_pkt_meta_allocate( fd_quic_pkt_meta_pool_t * pool );


/* free a pkt_meta
   returns a pkt_meta to the free list, ready to be allocated again */
void
fd_quic_pkt_meta_deallocate( fd_quic_pkt_meta_pool_t * pool,
                             fd_quic_pkt_meta_t *      pkt_meta );

FD_PROTOTYPES_END

#endif // HEADER_fd_src_waltz_quic_fd_quic_pkt_meta_h

