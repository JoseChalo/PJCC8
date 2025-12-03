nuttx_add_application(
  NAME "server_udp"
  SRCS "server_udp.c"
  STACKSIZE 2048
  PRIORITY 100
)