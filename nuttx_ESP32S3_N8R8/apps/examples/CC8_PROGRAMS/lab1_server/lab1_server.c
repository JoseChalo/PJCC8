#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_PORT 5004
#define BUFFER_SIZE 256



static const char *process_operation(const char *input)
{
  static char response[64];
  int a, b;
  char op;

  if (strcasecmp(input, "EXIT") == 0)
    {
      snprintf(response, sizeof(response), "EXIT");
      return response;
    }

  if (sscanf(input, "%d %c %d", &a, &op, &b) != 3)
    {
      snprintf(response, sizeof(response), "Error de formato");
      return response;
    }

  switch (op)
    {
      case '+': snprintf(response, sizeof(response), "%d", a + b); break;
      case '-': snprintf(response, sizeof(response), "%d", a - b); break;
      case '*': snprintf(response, sizeof(response), "%d", a * b); break;
      case '/':
        if (b == 0)
          snprintf(response, sizeof(response), "División por cero");
        else
          snprintf(response, sizeof(response), "%d", a / b);
        break;
      case '%':
        if (b == 0)
          snprintf(response, sizeof(response), "Módulo por cero");
        else
          snprintf(response, sizeof(response), "%d", a % b);
        break;
      default:
        snprintf(response, sizeof(response), "Operación no válida");
        break;
    }

  return response;
}

int main(void)
{
  int sockfd;
  struct sockaddr_in server_addr, client_addr;
  socklen_t addr_len = sizeof(client_addr);
  char buffer[BUFFER_SIZE];

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0)
    {
      printf("Error creando socket\n");
      return -1;
    }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(SERVER_PORT);
  server_addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
      printf("Error en bind()\n");
      close(sockfd);
      return -1;
    }

  printf("=== Servidor UDP NuttX ===\n");
  printf("Escuchando en puerto %d\n", SERVER_PORT);

  while (1)
    {
      printf("Esperando mensaje del cliente........\n");
      int n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                       (struct sockaddr *)&client_addr, &addr_len);

      printf("Recibiendo mensaje del cliente........\n");

      if (n < 0)
        {
          printf("Error recibiendo\n");
          continue;
        }

      buffer[n] = '\0';
      printf("Mensaje recibido: %s\n", buffer);

      const char *resp = process_operation(buffer);

      sendto(sockfd, resp, strlen(resp), 0,
             (struct sockaddr *)&client_addr, addr_len);

      printf("Respuesta enviada: %s\n", resp);

      if (strcasecmp(buffer, "EXIT") == 0)
        break;
    }

  close(sockfd);
  return 0;
}
