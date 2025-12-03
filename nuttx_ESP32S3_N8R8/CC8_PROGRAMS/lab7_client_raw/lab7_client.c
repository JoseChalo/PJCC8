#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netutils/netlib.h>
#include <errno.h>

#ifndef IP_HDRINCL
#  define IP_HDRINCL 2
#endif

#define MAX_PACKET_SIZE 1500         /* MTU segura */
#define IFACE_NAME       "wlan0"

/* ===== Estructuras con PACKED para RAW ===== */
struct iphdr_packed
{
  uint8_t  ihl:4, version:4;
  uint8_t  tos;
  uint16_t tot_len;
  uint16_t id;
  uint16_t frag_off;
  uint8_t  ttl;
  uint8_t  protocol;
  uint16_t check;
  uint32_t saddr;
  uint32_t daddr;
} __attribute__((packed));

struct udphdr_packed
{
  uint16_t source;
  uint16_t dest;
  uint16_t len;
  uint16_t check;
} __attribute__((packed));

/* ===== Checksum general ===== */
static uint16_t check(uint16_t *buf, int len)
{
  uint32_t sum = 0;
  while (len > 1)
    {
      sum += *buf++;
      len -= 2;
    }
  if (len > 0)
    sum += *(uint8_t *)buf;

  while (sum >> 16)
    sum = (sum & 0xFFFF) + (sum >> 16);

  return (uint16_t)(~sum);
}

/* ===== Checksum UDP con pseudoheader ===== */
static uint16_t udp_check(struct iphdr_packed *iph,
                          struct udphdr_packed *udph,
                          uint8_t *payload, int payload_len)
{
  struct __attribute__((packed))
  {
    uint32_t src;
    uint32_t dst;
    uint8_t  zero;
    uint8_t  proto;
    uint16_t len;
  } pseudo;

  pseudo.src  = iph->saddr;
  pseudo.dst  = iph->daddr;
  pseudo.zero = 0;
  pseudo.proto = IPPROTO_UDP;
  pseudo.len  = htons(sizeof(struct udphdr_packed) + payload_len);

  int total_len = sizeof(pseudo) + sizeof(struct udphdr_packed) + payload_len;
  uint8_t *buf = malloc(total_len);
  if (!buf) return 0;

  memcpy(buf, &pseudo, sizeof(pseudo));
  memcpy(buf + sizeof(pseudo), udph, sizeof(struct udphdr_packed));
  memcpy(buf + sizeof(pseudo) + sizeof(struct udphdr_packed), payload, payload_len);

  uint16_t csum = check((uint16_t *)buf, total_len);
  free(buf);
  return csum;
}

/* =================== CLIENTE RAW =================== */

int main(int argc, char *argv[])
{
  if (argc < 3)
    {
      printf("Uso: lab7_client <IP> <PORT>\n");
      return -1;
    }

  const char *server_ip = argv[1];
  int port = atoi(argv[2]);

  if (port <= 0 || port > 65535)
    {
      printf("Puerto inválido!\n");
      return -1;
    }

  printf("=== Cliente RAW UDP ===\n");

  /* Obtener IP local */
  struct in_addr myip;
  if (netlib_get_ipv4addr(IFACE_NAME, &myip) < 0)
    {
      printf("Error: no se obtuvo IP en %s (DHCP no listo?)\n", IFACE_NAME);
      return -1;
    }

  char ipstr[16];
  inet_ntop(AF_INET, &myip, ipstr, sizeof(ipstr));
  printf("IP local: %s\n", ipstr);
  printf("Servidor destino: %s:%d\n", server_ip, port);

  /* Crear socket RAW */
  int sock = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
  if (sock < 0)
    {
      printf("Error: socket RAW: %d -> %s\n", errno, strerror(errno));
      return -1;
    }

  int one = 1;
  if (setsockopt(sock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0)
    {
      printf("Error IP_HDRINCL: %d -> %s\n", errno, strerror(errno));
      close(sock);
      return -1;
    }

  char input[256];

  while (1)
    {
      printf("Ingrese operación (ej: 3+2) o EXIT: ");
      fflush(stdout);

      if (!fgets(input, sizeof(input), stdin))
        continue;

      input[strcspn(input, "\n")] = '\0';
      if (!strlen(input))
        continue;

      if (strcasecmp(input, "EXIT") == 0)
        break;

      int data_len = strlen(input);
      if (data_len > 1000)   /* Protección */
        {
          printf("Payload demasiado grande!\n");
          continue;
        }

      /* Buffer del paquete */
      uint8_t packet[MAX_PACKET_SIZE] __attribute__((aligned(4)));
      memset(packet, 0, sizeof(packet));

      struct iphdr_packed  *iph  = (struct iphdr_packed *)packet;
      struct udphdr_packed *udph = (struct udphdr_packed *)(packet + sizeof(struct iphdr_packed));
      uint8_t              *data = packet + sizeof(struct iphdr_packed) + sizeof(struct udphdr_packed);

      memcpy(data, input, data_len);

      /* Header IP */
      iph->ihl = 5;
      iph->version = 4;
      iph->tos = 0;
      iph->tot_len = htons(sizeof(struct iphdr_packed) + sizeof(struct udphdr_packed) + data_len);
      iph->id = htons(54321);
      iph->frag_off = 0;
      iph->ttl = 64;
      iph->protocol = IPPROTO_UDP;
      iph->check = 0;
      iph->saddr = myip.s_addr;
      inet_pton(AF_INET, server_ip, &iph->daddr);
      iph->check = check((uint16_t *)packet, sizeof(struct iphdr_packed));

      /* Header UDP */
      udph->source = htons(12345);
      udph->dest   = htons(port);
      udph->len    = htons(sizeof(struct udphdr_packed) + data_len);
      udph->check  = 0;
      udph->check  = udp_check(iph, udph, data, data_len);

      struct sockaddr_in dest;
      memset(&dest, 0, sizeof(dest));
      dest.sin_family = AF_INET;
      dest.sin_port   = htons(port);
      inet_pton(AF_INET, server_ip, &dest.sin_addr);

      printf("Enviando RAW...\n");
      ssize_t sent = sendto(sock, packet,
                            sizeof(struct iphdr_packed) + sizeof(struct udphdr_packed) + data_len,
                            0, (struct sockaddr *)&dest, sizeof(dest));

      if (sent < 0)
        printf("Error al enviar: %d -> %s\n", errno, strerror(errno));
    }

  close(sock);
  printf("Cliente RAW finalizado.\n");
  return 0;
}
