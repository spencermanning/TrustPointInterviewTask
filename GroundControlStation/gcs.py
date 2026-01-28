import serial
import socket
import binascii
import random 

print("Start GCS")

def main():

    UDP_IP = "127.0.0.1" # localhost
    UDP_PORT = 5005
    NUM_RETRIES = 3

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # sock.bind((UDP_IP, UDP_PORT))

    while True:
        data = b""
        input("\nPress Enter to send command: ")
        # MESSAGE = input("Send a message: ")

        retries = 0
        while data != b"ACK":
            SYNC_BYTES = [0xAB, 0xBA]
            LEN = 5                         # Example length
            # LEN = random.randint(1, 0xFFF)  # Random length for testing
            TYPE = 1                        # Example type
            SEQ = 0                         # Example sequence number
            PAYLOAD = b"CMD"                # Example payload
            RESERVED = b"\x00\x01\x02\x03"
            
            HEADER = (LEN & 0xFFF) << 12 | (TYPE & 0x3F) << 6 | SEQ & 0x3F
            MESSAGE = [bytes(SYNC_BYTES), HEADER.to_bytes(2, 'big'), PAYLOAD, RESERVED] # TODO: Make sure big is correct
            MESSAGE = b"".join(MESSAGE)
            CRC = binascii.crc32(MESSAGE) & 0xFFFFFFFF
            MESSAGE += CRC.to_bytes(4, 'big')
            print(f"Final message with CRC: {MESSAGE}")

            sock.sendto(MESSAGE, (UDP_IP, UDP_PORT))
            
            data, addr = sock.recvfrom(1024)
            print(f"Received {data} from {addr}")
            
            if data == b"ACK":
                print("ACK received, command successful")
                break
            elif data == b"NACK":
                retries += 1
                if retries >= NUM_RETRIES:
                    print("Max retries reached, aborting command")
                    break
                print("Resending...")
            else:
                print("Unexpected response, aborting command")
                break

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