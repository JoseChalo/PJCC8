/****************************************************************************
 * raw_client_adaptado.c
 *
 * Cliente RAW adaptado (AF_PACKET) para NuttX / ESP32-S3
 * Basado en tu propuesta; ahora:
 *  - Usa AF_PACKET / SOCK_RAW
 *  - Construye Ethernet + IP + UDP
 *  - Calcula checksum IP y UDP (pseudo-header IPv4)
 *  - Flush antes de enviar (MSG_DONTWAIT)
 *  - Polling para respuesta (~3s)
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <netpacket/packet.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>     /* solo para constantes */
#include <netinet/udp.h>

#ifndef ETH_P_ALL
#  define ETH_P_ALL 0x0003
#endif
#ifndef ETH_P_IP
#  define ETH_P_IP  0x0800
#endif

#define IFACE_NAME    "wlan0"
#define BUF_SIZE      1514
#define DEFAULT_SRC_PORT 55555
#define MAX_PAYLOAD   1024

/* Estructuras empaquetadas (para construir frame) */
struct mac_hdr_s {
    uint8_t  dest_mac[6];
    uint8_t  src_mac[6];
    uint16_t eth_type;
} __attribute__((packed));

struct ip_hdr_s {
    uint8_t  v_ihl;       /* version (4 bits) + ihl (4 bits) */
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t check;
    uint32_t saddr;
    uint32_t daddr;
} __attribute__((packed));

struct udp_hdr_s {
    uint16_t source;
    uint16_t dest;
    uint16_t len;
    uint16_t check;
} __attribute__((packed));

/* Buffer de trama (global para flush/recv) */
union frame_u {
    uint8_t buffer[BUF_SIZE];
    struct {
        struct mac_hdr_s eth;
        struct ip_hdr_s  ip;
        struct udp_hdr_s udp;
        uint8_t payload[MAX_PAYLOAD];
    } headers;
} __attribute__((packed));

static union frame_u g_pkt;

/* ---------------------- Util: checksums ---------------------- */

/* Checksum 16-bit (classic) - entrada en network byte order asumida */
static uint16_t calc_checksum(void *data, int len)
{
    uint32_t sum = 0;
    uint16_t *ptr = (uint16_t *)data;

    while (len > 1) {
        sum += ntohs(*ptr++);
        len -= 2;
    }
    if (len > 0) { /* odd byte */
        uint8_t last = *((uint8_t *)ptr);
        sum += last << 8;
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return htons((uint16_t)(~sum));
}

/* UDP checksum con pseudo-header IPv4 */
static uint16_t udp_checksum_ipv4(const struct ip_hdr_s *iph,
                                  const struct udp_hdr_s *udph,
                                  const uint8_t *payload,
                                  int payload_len)
{
    /* pseudo-header: src(4), dst(4), zero(1), proto(1), udp_len(2) */
    uint32_t sum = 0;
    uint16_t word;
    const uint16_t *p16;
    int i;

    /* src addr */
    p16 = (const uint16_t *)&iph->saddr;
    sum += ntohs(p16[0]); sum += ntohs(p16[1]);
    /* dst addr */
    p16 = (const uint16_t *)&iph->daddr;
    sum += ntohs(p16[0]); sum += ntohs(p16[1]);

    sum += IPPROTO_UDP;
    sum += ntohs(udph->len);

    /* UDP header */
    p16 = (const uint16_t *)udph;
    for (i = 0; i < sizeof(struct udp_hdr_s)/2; ++i) sum += ntohs(p16[i]);

    /* payload */
    for (i = 0; i + 1 < payload_len; i += 2) {
        memcpy(&word, payload + i, 2);
        sum += ntohs(word);
    }
    if (payload_len & 1) {
        uint8_t last = payload[payload_len - 1];
        sum += (last << 8);
    }

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return htons((uint16_t)(~sum));
}

/* ---------------------- Util: lectura prompt ---------------------- */
static void leer_input(const char* prompt, char* buffer, int max_len)
{
    printf("%s", prompt);
    fflush(stdout);
    if (fgets(buffer, max_len, stdin) != NULL) {
        buffer[strcspn(buffer, "\n")] = '\0';
    } else {
        buffer[0] = '\0';
    }
}

/* ---------------------- Main ---------------------- */
int main(int argc, char *argv[])
{
    int raw_sock = -1;
    struct ifreq ifr;
    struct sockaddr_ll device;
    uint8_t my_mac[6] = {0};
    struct in_addr my_ip = {0};
    char ip_dest[32];
    char op[256];
    int dst_port = 3000;

    printf("\n=== raw_client_adaptado (AF_PACKET) ===\n");

    /* 1) Abrir socket RAW AF_PACKET */
    raw_sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (raw_sock < 0) {
        perror("socket(AF_PACKET)");
        return -1;
    }

    /* 2) Obtener índice y MAC de la interfaz */
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, IFACE_NAME, IFNAMSIZ-1);

    if (ioctl(raw_sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("SIOCGIFINDEX");
        close(raw_sock);
        return -1;
    }
    int if_index = ifr.ifr_ifindex;

    if (ioctl(raw_sock, SIOCGIFHWADDR, &ifr) < 0) {
        perror("SIOCGIFHWADDR");
        close(raw_sock);
        return -1;
    }
    memcpy(my_mac, ifr.ifr_hwaddr.sa_data, 6);

    /* 3) Obtener IP de la interfaz (SIOCGIFADDR) */
    if (ioctl(raw_sock, SIOCGIFADDR, &ifr) == 0) {
        my_ip = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
    } else {
        /* no fatal: permitimos 0.0.0.0 pero advertimos */
        printf("Advertencia: SIOCGIFADDR falló; IP local no obtenida (errno=%d)\n", errno);
        my_ip.s_addr = 0;
    }

    printf("IF=%s idx=%d MAC=%02x:%02x:%02x:%02x:%02x:%02x IP=%s\n",
           IFACE_NAME, if_index,
           my_mac[0],my_mac[1],my_mac[2],my_mac[3],my_mac[4],my_mac[5],
           inet_ntoa(my_ip));

    /* 4) Bind al device (para enviar/recibir en esa IF) */
    memset(&device, 0, sizeof(device));
    device.sll_family = AF_PACKET;
    device.sll_ifindex = if_index;
    device.sll_protocol = htons(ETH_P_ALL);

    if (bind(raw_sock, (struct sockaddr *)&device, sizeof(device)) < 0) {
        perror("bind(AF_PACKET)");
        close(raw_sock);
        return -1;
    }

    /* 5) Config usuario destino */
    leer_input("IP Servidor (default 192.168.1.141): ", ip_dest, sizeof(ip_dest));
    if (strlen(ip_dest) < 7) strcpy(ip_dest, "192.168.1.141");
    leer_input("Puerto (default 3000): ", op, sizeof(op));
    if (strlen(op) > 0) dst_port = atoi(op);
    if (dst_port <= 0) dst_port = 3000;

    printf("Destino: %s:%d\n", ip_dest, dst_port);

    /* Broadcast MAC por defecto */
    uint8_t broadcast_mac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

    /* Loop principal */
    for (;;) {
        leer_input("\nOperacion (> 5+5 o EXIT): ", op, sizeof(op));
        if (strcasecmp(op, "EXIT") == 0) break;
        if (strlen(op) == 0) continue;

        int payload_len = strlen(op);
        if (payload_len > MAX_PAYLOAD) {
            printf("Payload demasiado grande (max %d)\n", MAX_PAYLOAD);
            continue;
        }

        /* FLUSH: vaciar buffer de recepciones anteriores (MSG_DONTWAIT) */
        struct sockaddr_ll dummy;
        socklen_t dlen = sizeof(dummy);
        while (recvfrom(raw_sock, g_pkt.buffer, BUF_SIZE, MSG_DONTWAIT, (struct sockaddr*)&dummy, &dlen) > 0)
            ; /* vaciar */

        /* Construir trama en g_pkt.buffer */
        memset(&g_pkt, 0, sizeof(g_pkt));

        /* Ethernet */
        memcpy(g_pkt.headers.eth.src_mac, my_mac, 6);
        memcpy(g_pkt.headers.eth.dest_mac, broadcast_mac, 6); /* broadcast por defecto */
        g_pkt.headers.eth.eth_type = htons(ETH_P_IP);

        /* IP (sin opciones) */
        struct ip_hdr_s *iph = &g_pkt.headers.ip;
        iph->v_ihl = 0x45; /* version(4)=4, ihl(4)=5 */
        iph->tos = 0;
        iph->total_len = htons(sizeof(struct ip_hdr_s) + sizeof(struct udp_hdr_s) + payload_len);
        iph->id = htons(rand() & 0xFFFF);
        iph->frag_off = htons(0);
        iph->ttl = 64;
        iph->proto = IPPROTO_UDP;
        iph->check = 0;
        iph->saddr = my_ip.s_addr;
        if (inet_pton(AF_INET, ip_dest, &iph->daddr) != 1) {
            printf("IP destino inválida\n");
            continue;
        }
        iph->check = calc_checksum((void*)iph, sizeof(struct ip_hdr_s));

        /* UDP */
        struct udp_hdr_s *udph = &g_pkt.headers.udp;
        udph->source = htons(DEFAULT_SRC_PORT);
        udph->dest = htons(dst_port);
        udph->len = htons(sizeof(struct udp_hdr_s) + payload_len);
        udph->check = 0;

        /* payload */
        memcpy(g_pkt.headers.payload, op, payload_len);

        /* Calcular checksum UDP (pseudo-header) */
        udph->check = udp_checksum_ipv4(iph, udph, g_pkt.headers.payload, payload_len);
        if (udph->check == 0) udph->check = 0xFFFF; /* opcional: evitar 0 */

        /* Frame length (ethernet hdr + ip total_len) */
        size_t frame_len = sizeof(struct mac_hdr_s) + ntohs(iph->total_len);

        /* Enviar frame */
        ssize_t sent = send(raw_sock, g_pkt.buffer, frame_len, 0);
        if (sent < 0) {
            perror("send");
            continue;
        }
        printf("Enviado %zd bytes. Esperando respuesta (~3s)...\n", sent);

        /* Polling para respuesta dirigida a nuestro puerto (DEFAULT_SRC_PORT) */
        int received = 0;
        int attempts = 60;
        for (int i = 0; i < attempts; ++i) {
            struct sockaddr_ll from;
            socklen_t flen = sizeof(from);
            ssize_t n = recvfrom(raw_sock, g_pkt.buffer, BUF_SIZE, MSG_DONTWAIT, (struct sockaddr *)&from, &flen);
            if (n > 0) {
                /* validar tamaño mínimo */
                if ((size_t)n < sizeof(struct mac_hdr_s) + sizeof(struct ip_hdr_s) + sizeof(struct udp_hdr_s))
                    continue;

                /* comprobar IP */
                struct mac_hdr_s *reth = (struct mac_hdr_s *)g_pkt.buffer;
                if (ntohs(reth->eth_type) != ETH_P_IP) continue;

                struct ip_hdr_s *rip = (struct ip_hdr_s *)(g_pkt.buffer + sizeof(struct mac_hdr_s));
                size_t rip_hlen = (rip->v_ihl & 0x0F) * 4;
                if (rip->proto != IPPROTO_UDP) continue;

                struct udp_hdr_s *rudph = (struct udp_hdr_s *)((uint8_t*)rip + rip_hlen);
                if (rudph->dest != htons(DEFAULT_SRC_PORT)) continue; /* no es para nosotros */

                int rlen = ntohs(rudph->len) - sizeof(struct udp_hdr_s);
                if (rlen <= 0) continue;
                if (rlen > 512) rlen = 512;

                char resp[513];
                memset(resp, 0, sizeof(resp));
                memcpy(resp, (uint8_t*)rudph + sizeof(struct udp_hdr_s), rlen);
                printf(">> RESPUESTA: %s\n", resp);
                received = 1;
                break;
            }
            usleep(50000); /* 50 ms */
        }
        if (!received) printf("Tiempo agotado (~3s). Servidor no respondió.\n");
    }

    close(raw_sock);
    printf("Cliente RAW finalizado.\n");
    return 0;
}
