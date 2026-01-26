import serial
import socket

print("HELLO")

def main():
    print("HI")

    # UDP_IP = "192.168.1.100"
    UDP_IP = "127.0.0.1"
    # UDP_IP = "localhost"
    UDP_PORT = 5005
    MESSAGE = b"Hello over UDP"

    sock = socket.socket(socket.AF_INET, socket.HOST_DGRAM)

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