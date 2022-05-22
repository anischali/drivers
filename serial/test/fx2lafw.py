from fcntl import ioctl
import fcntl, struct, os, sys

version = [chr(0), chr(0)]

fd = os.open(sys.argv[1], os.O_RDWR)

fcntl.ioctl(fd, 0x0, version, 2)

print(version)