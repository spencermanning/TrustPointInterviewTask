import socket

print("Start tester")

UDP_IP = "127.0.0.1"
UDP_PORT = 5005
MESSAGE = b"Goodbye"

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.sendto(b"Hiadsf", (UDP_IP, UDP_PORT))
sock.close()
print(f"Sent Hi")