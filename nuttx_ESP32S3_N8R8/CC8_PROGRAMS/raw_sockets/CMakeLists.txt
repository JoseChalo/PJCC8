nuttx_add_application(
  NAME "raw_sockets"
  SRCS "raw_sockets.c"
  STACKSIZE 4096
  PRIORITY 100
)