#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    int s = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (s < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7F000001);  // 127.0.0.1

    const char msg[] = "Hola RAW!";
    int r = sendto(s, msg, sizeof(msg), 0, 
                  (struct sockaddr*)&addr, sizeof(addr));
    if (r < 0) {
        perror("sendto");
        return -2;
    }

    printf("Enviado %d bytes!\n", r);

    close(s);
    return 0;
}

/*
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int s = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (s < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7F000001);  // 127.0.0.1

    const char msg[] = "Hola RAW!";
    int r = sendto(s, msg, sizeof(msg), 0,
                   (struct sockaddr*)&addr, sizeof(addr));
    if (r < 0) {
        perror("sendto");
        close(s);
        return -2;
    }

    printf("Enviado %d bytes!\n", r);

    close(s);
    return 0;
}
*/
