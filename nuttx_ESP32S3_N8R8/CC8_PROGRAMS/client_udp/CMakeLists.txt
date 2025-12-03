nuttx_add_application(
  NAME "client_udp"
  SRCS "client_udp.c"
  STACKSIZE 2048
  PRIORITY 100
)