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
#include <stdbool.h>

#define SERVER_PORT         5005
#define BUFFER_SIZE         1024
#define MAX_MESSAGE_SIZE    32      // Max size of complete reassembled message, excluding headers
#define LAST_SEQ_NUM        0x20    // Indicates last chunk of dissassembled message
#define MAX_SEQ_NUM         0x3F    // Largest sequence number (6 bits)

// mqd_t mq = -1;

typedef enum {
    CMD_EXECUTE          = 0x01,
    CMD_SET_PARAM        = 0x02,
    CMD_GET_PARAM        = 0x03,
    PING                 = 0x06,
    PONG                 = 0x07,
    LINK_STATUS          = 0x08,
    TIME_SYNC            = 0x09,
    RETRANSMIT_REQUEST   = 0x0A,
    FRAGMENT_MISSING     = 0x0B,

    ACK                  = 0x10,
    NACK                 = 0x11,

    TM_HOUSEKEEPING      = 0x20,
    TM_SUBSYSTEM_STATUS  = 0x21,

    DATA_SCIENCE         = 0x30,
    DATA_IMAGE           = 0x31,
} MessageType;

#define CB_BUFFER_SIZE MAX_MESSAGE_SIZE  // must be a power of 2 for simple wrap-around

typedef struct {
    uint8_t buffer[CB_BUFFER_SIZE];
    uint16_t head;   // points to next write. Indices in the array
    uint16_t tail;   // points to next read. Indices in the array
} CircularBuffer;

CircularBuffer cb;

// Initialize the buffer
void cb_init(CircularBuffer *cb) {
    cb->head = 0;
    cb->tail = 0;
}

// Check if the buffer is empty
bool cb_is_empty(CircularBuffer *cb) {
    return cb->head == cb->tail;
}

// Check if the buffer is full
bool cb_is_full(CircularBuffer *cb) {
    return ((cb->head + 1) & (CB_BUFFER_SIZE - 1)) == cb->tail;
}

// Add an element to the buffer
bool cb_push(CircularBuffer *cb, uint8_t data) {
    if (cb_is_full(cb)) return false;  // buffer full
    cb->buffer[cb->head] = data;
    cb->head = (cb->head + 1) & (CB_BUFFER_SIZE - 1);  // increment head and consider wrap around
    return true;
}

// Remove an element from the buffer
bool cb_pop(CircularBuffer *cb, uint8_t *data) {
    if (cb_is_empty(cb)) return false;  // buffer empty
    *data = cb->buffer[cb->tail];
    cb->tail = (cb->tail + 1) & (CB_BUFFER_SIZE - 1);  // increment tail and consider wrap around
    return true;
}

void *savePayload(uint8_t *payload) {

    // FIXME: Confirm the full message is being saved
    printf("Saving %s to persistent storage.\n\n", payload);

    // Save to active memory in circular buffer
    bool out = cb_push(&cb, *payload);

    if (!out) {
        printf("Circular buffer full, cannot save payload.\n");
    } else {
        printf("Payload saved to circular buffer.\n");
    }

    return NULL;
}

void print_bytes(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\n");
}

// Build Ack/Nack function (returns length of datagram)
size_t buildAckNack(uint8_t *ack_datagram, uint8_t seq_num, int is_ack) {
        size_t ack_len = 0;

        // Sync bytes
        ack_datagram[ack_len++] = 0xAB;
        ack_datagram[ack_len++] = 0xBA;

        // Build 3-byte header: LEN=0 (12 bits), TYPE (6 bits), SEQ (6 bits)
        uint32_t header = (((uint32_t)0 & 0xFFFu) << 12) |
                          ((((uint32_t)(is_ack ? ACK : NACK)) & MAX_TYPE_NUM) << TYPE_LENGTH) |
                          (((uint32_t)seq_num) & MAX_SEQ_NUM);

        // Write header as big-endian 3 bytes
        ack_datagram[ack_len++] = (header >> 16) & 0xFF;
        ack_datagram[ack_len++] = (header >> 8) & 0xFF;
        ack_datagram[ack_len++] = (header) & 0xFF;

        // Compute CRC over everything except the CRC field itself
        uint32_t ack_crc = crc32(0L, ack_datagram, ack_len);
        uint32_t ack_crc_net = htonl(ack_crc);

        // Append CRC
        memcpy(ack_datagram + ack_len, &ack_crc_net, 4);
        ack_len += 4;

        return ack_len;
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

    // TODO: Handle duplicates somewhere. Is that through a different "TYPE" message?

    while (1) {
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                         (struct sockaddr*)&client_addr, &addr_len);
        if (n < 0) {
            perror("recvfrom failed");
            size_t nack_len = buildAckNack(nack_datagram, 0, 0);
            sendto(sockfd, nack_datagram, nack_len, 0, (struct sockaddr*)&client_addr, addr_len);
            continue;
        }
        buffer[n] = '\0';  // null-terminate received string
        printf("Received: \n");
        print_bytes((uint8_t*)buffer, n);

        // Extract received CRC (last 4 bytes)
        // Minimum packet: SYNC(2) + HEADER(3) + RESERVED(4) + CRC(4) => 13 bytes
        if (n < (2 + 3 + 4 + 4)) {
            printf("Packet too short\n");
            size_t nack_len = buildAckNack(nack_datagram, 0, 0);
            sendto(sockfd, nack_datagram, nack_len, 0, (struct sockaddr*)&client_addr, addr_len);
            continue;
        }
        received_crc = ntohl(*(uint32_t*)(buffer + n - 4));
        computed_crc = crc32(0L, (uint8_t*)buffer, n - 4);

        if (received_crc != computed_crc) {
            printf("CRC mismatch: received 0x%08X, computed 0x%08X\n", received_crc, computed_crc);
            size_t nack_len = buildAckNack(nack_datagram, 0, 0);
            sendto(sockfd, nack_datagram, nack_len, 0, (struct sockaddr*)&client_addr, addr_len);
            continue;
        }

        printf("CRC valid: 0x%08X\n", received_crc);

        // Reconstruct header and extract seq/type
        uint32_t header = ((uint8_t)buffer[2] << 16) | ((uint8_t)buffer[3] << 8) | (uint8_t)buffer[4];
        uint8_t seq_num = header & MAX_SEQ_NUM;
        uint8_t *payload_start = (uint8_t*)buffer + 5;

        // Ensure payload length excludes sync (2), header (3), reserved (4), and CRC (4)
        if (n < (2 + 3 + 4 + 4)) {
            printf("Packet too short for payload\n");
            size_t nack_len = buildAckNack(nack_datagram, seq_num, 0);
            sendto(sockfd, nack_datagram, nack_len, 0, (struct sockaddr*)&client_addr, addr_len);
            continue;
        }
        size_t payload_len = n - 2 - 3 - 4 - 4;  // exclude sync, header, reserved, crc

        // Store in a reassembly buffer indexed by sequence
        static uint8_t reassembly_buf[MAX_MESSAGE_SIZE];
        static size_t reassembly_offset = 0;
        if (payload_len + reassembly_offset <= MAX_MESSAGE_SIZE) {
            memcpy(reassembly_buf + reassembly_offset, payload_start, payload_len);
            reassembly_offset += payload_len;
        } else {
            printf("Reassembly buffer overflow\n");
            // send nack
            size_t nack_len = buildAckNack(nack_datagram, seq_num, 0);
            sendto(sockfd, nack_datagram, nack_len, 0, (struct sockaddr*)&client_addr, addr_len);
            // reset for safety
            reassembly_offset = 0;
            continue;
        }

        // Check if this is the last fragment (high bit set in seq_num)
        if (seq_num & LAST_SEQ_NUM) {
            printf("Final fragment received, message complete\n");

            // Save complete payload
            savePayload(reassembly_buf);

            // Clear buffer for next message
            reassembly_offset = 0;
            reassembly_buf[0] = '\0';

        } else {
            printf("Fragment %d received, awaiting more\n", seq_num & 0x7F);
        }

        // Build and send ACK using the same 3-byte header format as data
        size_t ack_len = buildAckNack(ack_datagram, seq_num, 1);
        sendto(sockfd, ack_datagram, ack_len, 0, (struct sockaddr*)&client_addr, addr_len);

        // Parse type out of reconstructed header
        uint8_t msg_type = (header >> TYPE_LENGTH) & MAX_TYPE_NUM;

        printf("Message type: 0x%02X\n", msg_type);

        if (msg_type != ACK && msg_type != NACK) {
            // Process message based on type
            switch (msg_type) {
                case CMD_EXECUTE:
                    printf("Executing command\n");
                    // TODO: Fill in response handling and return message
                    break;
                case CMD_SET_PARAM:
                    printf("Setting parameter\n");
                    // TODO: Fill in response handling and return message
                    break;
                case CMD_GET_PARAM:
                    printf("Getting parameter\n");
                    // TODO: Fill in response handling and return message
                    break;
                default:
                    printf("Unknown message type: 0x%02X\n", msg_type);
            }
        } else {
            printf("Received ACK/NACK, no further processing\n");
        }
    }

    close(sockfd);

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