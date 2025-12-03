#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUFFER_SIZE 256
#define DEFAULT_PORT 5004

/* Función para evaluar operaciones simples */
double eval(const char *op)
{
  double a, b;
  char c;
  if (sscanf(op, "%lf %c %lf", &a, &c, &b) != 3)
    return 0.0;

  switch (c)
    {
      case '+': return a + b;
      case '-': return a - b;
      case '*': return a * b;
      case '/': return (b != 0) ? (a / b) : 0.0;
    }
  return 0.0;
}

int main(void)
{
  int sock;
  struct sockaddr_in server_addr, client_addr;
  socklen_t addr_len = sizeof(client_addr);
  char buffer[BUFFER_SIZE];

  printf("=== Servidor UDP NuttX/ESP32-S3 ===\n");

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    {
      printf("Error: socket() falló\n");
      return -1;
    }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(DEFAULT_PORT);
  server_addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
      printf("Error: bind() falló\n");
      close(sock);
      return -1;
    }

  printf("Servidor escuchando en puerto %d...\n", DEFAULT_PORT);

  while (1)
    {
      memset(buffer, 0, sizeof(buffer));

      int bytes = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                           (struct sockaddr *)&client_addr, &addr_len);

      if (bytes <= 0)
        continue;

      printf("Recibido de %s:%d => %s\n",
             inet_ntoa(client_addr.sin_addr),
             ntohs(client_addr.sin_port),
             buffer);

      double result = eval(buffer);
      char response[64];
      snprintf(response, sizeof(response), "%g", result);

      sendto(sock, response, strlen(response), 0,
             (struct sockaddr *)&client_addr, addr_len);
    }

  close(sock);
  return 0;
}
