ifdef FD_HAS_HOSTED
$(call add-hdrs,fd_rpc_client.h)
$(call add-objs,fd_rpc_client,fddev_shared)
$(call make-unit-test,test_rpc_client,test_rpc_client,fddev_shared fd_util fd_ballet)
$(call make-unit-test,dump_rpc_client,dump_rpc_client,fddev_shared fd_util fd_ballet)
$(call run-unit-test,test_rpc_client,)
endif
