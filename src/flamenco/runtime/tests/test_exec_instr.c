#define FD_SCRATCH_USE_HANDHOLDING 1
#define WKSP_TAG 2

#include "fd_exec_instr_test.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../../nanopb/pb_firedancer.h"
#include "../../nanopb/pb_decode.h"
#include "generated/invoke.pb.h"

#include <stdio.h>
#include "../fd_acc_mgr.h"


static int
run_test( fd_exec_instr_test_runner_t * runner,
          char const *                  path ) {

  /* Read file content to memory */

  int file = open( path, O_RDONLY );
  struct stat st;
  if( FD_UNLIKELY( 0!=fstat( file, &st ) ) ) {
    FD_LOG_WARNING(( "fstat(%s): %s", path, fd_io_strerror( errno ) ));
    return 0;
  }
  ulong file_sz = (ulong)st.st_size;
  uchar * buf = fd_scratch_alloc( 1, file_sz );
  FD_TEST( 0==fd_io_read( file, buf, file_sz, file_sz, &file_sz ) );
  FD_TEST( 0==close( file ) );

  /* Deserialize (unfortunately uses libc malloc) */

  pb_istream_t istream = pb_istream_from_buffer( buf, file_sz );
  fd_exec_test_instr_fixture_t fixture[1] = {0};
  int decode_ok = pb_decode_ex( &istream, &fd_exec_test_instr_fixture_t_msg, fixture, PB_DECODE_NOINIT );
  if( FD_UNLIKELY( !decode_ok ) ) {
    FD_LOG_WARNING(( "%s: failed to decode (%s)", path, PB_GET_ERROR(&istream) ));
    pb_release( &fd_exec_test_instr_fixture_t_msg, fixture );
    return 0;
  }

  /* Run test */

  char program_id_str[ FD_BASE58_ENCODED_32_SZ ];
  FD_LOG_DEBUG(( "Running test %s (%s)", path, fd_acct_addr_cstr( program_id_str, fixture->input.program_id ) ));
  int ok = fd_exec_instr_fixture_run( runner, fixture, path );
  if( ok ) FD_LOG_INFO   (( "OK   %s", path ));
  else     FD_LOG_WARNING(( "FAIL %s", path ));

  pb_release( &fd_exec_test_instr_fixture_t_msg, fixture );
  return ok;
}

int
main( int     argc,
      char ** argv ) {
  fd_boot( &argc, &argv );
  fd_flamenco_boot( NULL, NULL );

  /* TODO switch to leap API and set up a thread pool once available */
  ulong cpu_idx = fd_tile_cpu_id( fd_tile_idx() );
  if( cpu_idx>=fd_shmem_cpu_cnt() ) cpu_idx = 0UL;
  fd_wksp_t * wksp = fd_wksp_new_anonymous( FD_SHMEM_NORMAL_PAGE_SZ, 65536UL * 8UL, fd_shmem_cpu_idx( fd_shmem_numa_idx( cpu_idx ) ), "wksp", 0UL );

  ulong   scratch_fmem[ 64UL ] __attribute((aligned(FD_SCRATCH_FMEM_ALIGN)));
  uchar * scratch_smem = malloc( 1 << 30 ); // 1 GB

  ulong   spad_mem_max = FD_RUNTIME_TRANSACTION_EXECUTION_FOOTPRINT_FUZZ;
  uchar * spad_mem     = fd_wksp_alloc_laddr( wksp, FD_SPAD_ALIGN, spad_mem_max, 3 ); /* 1342191744 B */

  fd_scratch_attach( scratch_smem, scratch_fmem, 1UL<<30, 64UL );

  // Setup usage tracking
  fd_wksp_usage_t usage[1];
  ulong tags[1] = { WKSP_TAG };
  fd_wksp_usage( wksp, tags, 1, usage );
  ulong initial_usage = usage->used_sz;

  ulong fail_cnt = 0UL;
  for( int j=1; j<argc; j++ ) {
    // Init runner
    void * runner_mem = fd_wksp_alloc_laddr( wksp, fd_exec_instr_test_runner_align(), fd_exec_instr_test_runner_footprint(), 2 );
    fd_exec_instr_test_runner_t * runner = fd_exec_instr_test_runner_new( runner_mem, spad_mem, 2 );

    // Run the test
    FD_TEST( fd_scratch_frame_used()==0UL );
    fd_scratch_push();
    fail_cnt += !run_test( runner, argv[j] );
    fd_scratch_pop();

    // Free runner
    fd_wksp_free_laddr( fd_exec_instr_test_runner_delete( runner ) );

    // Calculate usage
    fd_wksp_usage( wksp, tags, 1, usage );
    FD_TEST( usage->used_sz == initial_usage );
  }

  /* TODO verify that there are no leaked libc allocs and vallocs */

  FD_TEST( fd_scratch_frame_used()==0UL );
  free( scratch_smem );
  fd_wksp_free_laddr( spad_mem );
  fd_wksp_delete_anonymous( wksp );
  fd_halt();
  return fail_cnt>0UL;
}
