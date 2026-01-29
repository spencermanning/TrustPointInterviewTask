import serial
import socket
import binascii
import random 

print("Start GCS")

def main():
    """Main function to run the Ground Control Station."""

    # Definitions:
        # Datagram
            # | SYNC (2 bytes) | HEADER (3 bytes) | PAYLOAD (variable) | RESERVED (4 bytes) | CRC32 (4 bytes) |
        # "Message"
        #   All payloads sent in sequence, reassembled from datagrams on spacecraft

    UDP_IP = "127.0.0.1" # localhost
    UDP_PORT = 5005
    NUM_RETRIES = 3
    
    SYNC_BYTES = [0xAB, 0xBA]

    MAX_TYPE_NUM = 0x3F  # 0b11 1111 Largest type number (6 bits)
    LAST_SEQ_NUM = 0x20 # 0x10 0000 Indicates last chunk of dissassembled message
    MAX_SEQ_NUM = 0x3F  # 0b11 1111 Largest sequence number (6 bits)
    
    LEN_LENGTH = 12   # Length field is 12 bits
    TYPE_LENGTH = 6   # Type field is 6 bits
    SEQ_LENGTH = 6    # Sequence number field is 6 bits

    RESERVED = b"\x00\x01\x02\x03"

    MAX_PAYLOAD_SIZE = 8    # Max size of payload in a single datagram
    MAX_MESSAGE_SIZE = 32   # Max size of complete reassembled message, excluding headers

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    # TODO: Determine if SO_REUSEADDR is needed here
    # sock.bind((UDP_IP, UDP_PORT))

    CMD_EXECUTE          = 0x01
    CMD_SET_PARAM        = 0x02
    CMD_GET_PARAM        = 0x03
    PING                 = 0x06
    PONG                 = 0x07
    LINK_STATUS          = 0x08
    TIME_SYNC            = 0x09
    RETRANSMIT_REQUEST   = 0x0A
    FRAGMENT_MISSING     = 0x0B

    ACK                  = 0x10
    NACK                 = 0x11

    TM_HOUSEKEEPING      = 0x20
    TM_SUBSYSTEM_STATUS  = 0x21

    DATA_SCIENCE         = 0x30
    DATA_IMAGE           = 0x31

    test_message_pairs = [
        (b"CMD1", 0x01),  # CMD_EXECUTE
        (b"CMD2", 0x02),  # CMD_SET_PARAM
        (b"CMD3", 0x03),  # CMD_GET_PARAM
        (bytes(range(200)), 0x30),  # DATA_SCIENCE
        ]
    for msg_idx, msg_pair in enumerate(test_message_pairs):
        rxdata = b""
        if msg_idx > len(test_message_pairs) - 1:
            print("All test messages sent")
            break

        input("\nPress Enter to send %s..." % test_message_pairs[msg_idx][0])

        retries = 0
        finished_message = False
        max_retries_reached = False
        # while rxdata != b"ACK" and not max_retries_reached:
        while not max_retries_reached and not finished_message:

            # Could need one more datagrams if message is long
            for payload_cnt, payload in enumerate([msg_pair[0][i:i+MAX_PAYLOAD_SIZE] for i in range(0, len(msg_pair[0]), MAX_PAYLOAD_SIZE)]):

                LEN = len(payload)
                TYPE = msg_pair[1]

                # Use the fragment index (payload_cnt) as sequence number
                SEQ = payload_cnt
                if payload_cnt == (len(msg_pair[0]) - 1) // MAX_PAYLOAD_SIZE:
                    SEQ = SEQ | LAST_SEQ_NUM  # Mark as last sequence
                print(f"\nSend sequence number: {SEQ}")

                # Build header with explicit precedence
                HEADER = ((LEN & 0xFFF) << LEN_LENGTH) | ((TYPE & MAX_TYPE_NUM) << TYPE_LENGTH) | (SEQ & MAX_SEQ_NUM)
                DATAGRAM = [bytes(SYNC_BYTES), HEADER.to_bytes(3, 'big'), payload, RESERVED]
                DATAGRAM = b"".join(DATAGRAM)
                CRC = binascii.crc32(DATAGRAM) & 0xFFFFFFFF
                DATAGRAM += CRC.to_bytes(4, 'big')
                print(f"Final datagram with CRC: {DATAGRAM}")

                sock.sendto(DATAGRAM, (UDP_IP, UDP_PORT))
                rxdata, addr = sock.recvfrom(1024)
                print(f"Received {rxdata} from {addr}")
                
                # Validate minimal length and CRC
                if len(rxdata) < 9:
                    print("Received packet too short")
                    continue

                received_crc = int.from_bytes(rxdata[-4:], 'big')
                computed_crc = binascii.crc32(rxdata[:-4]) & 0xFFFFFFFF
                if received_crc != computed_crc:
                    print(f"CRC mismatch: received 0x{received_crc:08X}, computed 0x{computed_crc:08X}")
                    continue

                # Parse 3-byte header (big-endian)
                header = int.from_bytes(rxdata[2:5], 'big')
                RX_TYPE = (header >> TYPE_LENGTH) & MAX_TYPE_NUM
                RX_SEQ = header & MAX_SEQ_NUM
                print(f"RX_TYPE: {RX_TYPE}, RX_SEQ: {RX_SEQ}")

                if RX_TYPE == ACK:
                    print("ACK received, command successful")
                    msg_idx = msg_idx + 1
                    if msg_idx >= len(test_message_pairs):
                        print("All test messages sent")
                        finished_message = True
                        break
                    continue
                elif RX_TYPE == NACK:
                    retries += 1
                    if retries >= NUM_RETRIES:
                        print("Max retries reached, aborting command")
                        max_retries_reached = True
                        break
                    print("Resending...")
                    # NOTE: msg_idx stays the same for a duplicate message send
                elif (RX_SEQ & MAX_SEQ_NUM) != (SEQ & MAX_SEQ_NUM):
                    print(f"Sequence number mismatch {RX_SEQ} vs {SEQ}, resending...")
                    # NOTE: msg_idx stays the same for a duplicate message send
                    retries += 1
                    if retries >= NUM_RETRIES:
                        print("Max retries reached, aborting command")
                        max_retries_reached = True
                        break
                    print("Resending...")
                else:
                    print("Data received")
                    msg_idx = msg_idx + 1
                    continue

if __name__ == "__main__":
    main()



# send_command.py
# request_data.py
# replay_logs.py

# Ground-side algorithms (Python)
# Fragmentation and reassembly
# Timeout + retransmission
# Sliding window (optional, bonus points)
# Log-based persistence for sent/received packets

# 6. Python Ground Implementation (what I’d actually write)
# I’d implement:
# Packet class (encode/decode)
# CRC32 using binascii.crc32
# Transport abstraction (UART/TCP/UDP simulated)
# Unit tests for encoding/decoding