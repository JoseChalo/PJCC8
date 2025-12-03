#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUFFER_SIZE 256
#define DEFAULT_PORT 5004
#define DEFAULT_SERVER "127.0.0.1"

int main(int argc, FAR char *argv[])
{
  int sock;
  struct sockaddr_in server_addr;
  char buffer[BUFFER_SIZE];
  const char *server_ip = DEFAULT_SERVER;
  int port = DEFAULT_PORT;

  printf("=== Cliente UDP NuttX ===\n");
  printf("Servidor: %s:%d\n", server_ip, port);

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    {
      printf("Error: no se pudo crear el socket\n");
      return -1;
    }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

  while (1)
    {
      printf("Ingrese operación (ej: 3 + 2) o EXIT: ");
      fflush(stdout);

      if (fgets(buffer, sizeof(buffer), stdin) == NULL)
        {
          printf("Entrada vacía, saliendo.\n");
          break;
        }

      buffer[strcspn(buffer, "\n")] = '\0';

      if (strlen(buffer) == 0)
        continue;

      // Enviar al servidor
      printf("Enviando mensaje al servidor........\n");
      ssize_t sent = sendto(sock, buffer, strlen(buffer), 0,
                            (struct sockaddr *)&server_addr,
                            sizeof(server_addr));
      if (sent < 0)
        {
          printf("Error al enviar.\n");
          continue;
        }

      if (strcasecmp(buffer, "EXIT") == 0)
        break;

      // Esperar respuesta
      ssize_t recv_len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, NULL, NULL);
      if (recv_len < 0)
        {
          printf("Error recibiendo respuesta.\n");
          continue;
        }

      buffer[recv_len] = '\0';
      printf("Respuesta del servidor: %s\n", buffer);
    }

  close(sock);
  printf("Cliente finalizado.\n");
  return 0;
}
