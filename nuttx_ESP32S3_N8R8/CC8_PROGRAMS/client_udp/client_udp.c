#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUFFER_SIZE 256
#define MAX_SERVERS 10

typedef struct
{
  struct sockaddr_in addr;
} server_t;

int main(int argc, char *argv[])
{
  if (argc < 3 || (argc - 1) % 2 != 0)
    {
      printf("Uso: client_udp <IP1> <PUERTO1> [<IP2> <PUERTO2> ...]\n");
      return -1;
    }

  int sock;
  server_t servers[MAX_SERVERS];
  int total = (argc - 1) / 2;

  printf("=== Cliente UDP Multi-Servidor ===\n");
  printf("Total de servidores: %d\n", total);

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    {
      printf("Error: no se pudo crear socket UDP\n");
      return -1;
    }

  for (int i = 0; i < total; i++)
    {
      memset(&servers[i].addr, 0, sizeof(struct sockaddr_in));
      servers[i].addr.sin_family = AF_INET;
      servers[i].addr.sin_port = htons(atoi(argv[2 + 2 * i]));
      inet_pton(AF_INET, argv[1 + 2 * i], &servers[i].addr.sin_addr);
      printf("Servidor #%d => %s:%s\n", i + 1, argv[1 + 2 * i], argv[2 + 2 * i]);
    }

  char buffer[BUFFER_SIZE];

  while (1)
    {
      printf("Ingrese operación (EXIT para salir): ");
      fflush(stdout);

      if (fgets(buffer, sizeof(buffer), stdin) == NULL)
        break;

      buffer[strcspn(buffer, "\n")] = '\0';
      if (!strlen(buffer))
        continue;

      if (strcasecmp(buffer, "EXIT") == 0)
        break;

      for (int i = 0; i < total; i++)
        {
          sendto(sock, buffer, strlen(buffer), 0,
                 (struct sockaddr *)&servers[i].addr,
                 sizeof(struct sockaddr_in));
        }

      struct sockaddr_in from;
      socklen_t len = sizeof(from);
      char resp[64];

      int bytes = recvfrom(sock, resp, sizeof(resp) - 1, 0,
                           (struct sockaddr *)&from, &len);

      if (bytes > 0)
        {
          resp[bytes] = 0;
          printf("Respuesta de %s:%d => %s\n",
                 inet_ntoa(from.sin_addr),
                 ntohs(from.sin_port),
                 resp);
        }
    }

  close(sock);
  printf("Cliente terminado.\n");
  return 0;
}
