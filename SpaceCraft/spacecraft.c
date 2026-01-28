// protocol_parser.c – byte stream → packets
// crc.c – CRC32 computation
// storage.c – persistent message storage (flash/file abstraction)
// comm.c – send/receive interface
// message_manager.c – sequencing, ACK/NACK, retries


// while (true):
//     read_bytes()
//     if valid_packet:
//         if CRC ok:
//             store_payload()
//             send_ack()
//         else:
//             send_nack()

// Spacecraft-side algorithms (C)
//      Packet framing from byte stream
//      CRC verification
//      Sequence tracking
//      Ring buffer or flash-backed queue
//      Simple retry/ACK logic
//      I’d keep algorithms O(1) or O(n) with bounded memory.

#include <stdio.h>
#include <stdlib.h>     // for EXIT_FAILURE
#include <string.h>
#include <arpa/inet.h>   // for socket(), sockaddr_in, socklen_t
#include <unistd.h>      // for close()
#include <pthread.h>
#include <mqueue.h>
#include <zlib.h>       // for crc32
#include <stdint.h>

#define SERVER_PORT 5005
#define BUFFER_SIZE 1024

mqd_t mq = -1;

void print_bytes(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\n");
}


void *savePayload(void *arg) {
    (void)arg;
    // char buf[BUFFER_SIZE];
    // ssize_t n = mq_receive(mq, buf, BUFFER_SIZE, NULL);
    
    // if (n < 0) {
    //     perror("mq_receive failed");
    //     return NULL;
    // }

    // // Simulate saving payload to persistent storage
    // printf("Saving payload: %.*s\n", (int)n, buf);
    return NULL;
}

void *cmdRecvThread(void *arg) {
    (void)arg;
    int sockfd;
    char buffer[BUFFER_SIZE];
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    uint32_t received_crc, computed_crc;

    // 1. Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // 2. Bind to localhost:SERVER_PORT
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    // server_addr.sin_addr.s_addr = INADDR_ANY;  // listen on all local IPs
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");  // listen on all local IPs
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", SERVER_PORT);

    while (1) {
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                         (struct sockaddr*)&client_addr, &addr_len);
        if (n < 0) {
            perror("recvfrom failed");
            sendto(sockfd, "NACK", 4, 0, (struct sockaddr*)&client_addr, addr_len);
            continue;
        }
        buffer[n] = '\0';  // null-terminate received string
        printf("Received: \n");
        print_bytes((uint8_t*)buffer, n);

        // Extract received CRC (last 4 bytes)
        if (n < 4) {
            printf("Packet too short for CRC\n");
            sendto(sockfd, "NACK", 4, 0, (struct sockaddr*)&client_addr, addr_len);
            continue;
        }
        received_crc = ntohl(*(uint32_t*)(buffer + n - 4));
        computed_crc = crc32(0L, (uint8_t*)buffer, n - 4);

        if (received_crc != computed_crc) {
            printf("CRC mismatch: received 0x%08X, computed 0x%08X\n", received_crc, computed_crc);
            sendto(sockfd, "NACK", 4, 0, (struct sockaddr*)&client_addr, addr_len);
            continue;
        }

        printf("CRC valid: 0x%08X\n", received_crc);

        // Save payload in case send window closes
        // mq_send(mq, buffer, n, 0);

        sendto(sockfd, "ACK", 3, 0, (struct sockaddr*)&client_addr, addr_len);

        // Echo back
        // sendto(sockfd, buffer, n, 0, (struct sockaddr*)&client_addr, addr_len);
    }

    close(sockfd);
    // return 0;

    return NULL;
}

int main() {
    pthread_t thread;

    if (pthread_create(&thread, NULL, cmdRecvThread, NULL) != 0) {
        perror("Failed to create thread");
        return EXIT_FAILURE;
    }

    // mq = mq_open("/payload_queue", O_CREAT | O_RDWR, 0644, NULL);

    pthread_join(thread, NULL);
    return 0;
}