#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <Windows.h>
#else
#include <unistd.h>
#define O_BINARY 0
#endif

#define HEADER_LENGTH  5
#define MIN_PKT_LENGTH (HEADER_LENGTH + 2)

#define START_BYTE 0x55

#define BROADCAST_ADDR 0

#define FUNCTION_READ    1
#define FUNCTION_WRITE   2
#define FUNCTION_CONTROL 3
#define FUNCTION_REQUEST 4

#pragma pack(1)

struct Packet
{
    unsigned char  start;
    unsigned short device_addr;
    unsigned char  function;
    unsigned char  data_addr;
    unsigned char  data[5]; /* Data, followed by CRC */
};

#pragma pack()

#ifdef _WIN32

static int setparams(int fd)
{
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    DCB dcb;
    COMMTIMEOUTS to;

    memset(&dcb, 0, sizeof(dcb));

    dcb.DCBlength = sizeof(dcb);
    dcb.BaudRate  = 9600;
    dcb.ByteSize  = 8;
    dcb.Parity    = NOPARITY;
    dcb.StopBits  = ONESTOPBIT;
    dcb.fBinary   = TRUE;

    if (!SetCommState(h, &dcb)) {
        fprintf(stderr, "Failed to set serial port parameters: %08X\n", GetLastError());
        return -1;
    }

    to.ReadIntervalTimeout         = 0;
    to.ReadTotalTimeoutConstant    = 0;
    to.ReadTotalTimeoutMultiplier  = 0;
    to.WriteTotalTimeoutConstant   = 0;
    to.WriteTotalTimeoutMultiplier = 0;

    if (!SetCommTimeouts(h, &to)) {
        fprintf(stderr, "Failed to set serial port parameters: %08X\n", GetLastError());
        return -1;
    }


    return 0;
}

#else

static int setparams(int fd)
{
    fprintf(stderr, "Sorry, UNIX support isn't implemented yet\n");
    exit(255);
}

#endif

static void usage(const char* progname)
{
    printf("Usage: %s <serial_port> [new_address]\n", progname);
}

static int wrappedRead(int fd, void *buffer, int length)
{
    int r = read(fd, buffer, length);

    if (r == -1) {
        fprintf(stderr, "Error reading from serial port: %s\n", strerror(errno));
        return -1;
    }
    else if (r < length) {
        fprintf(stderr, "Malformed packet, only %d bytes read, %d expected\n", r, length);
        return -1;
    }

    return 0;
}

static unsigned short crc16(unsigned char *buffer, int length)
{
    unsigned short crc = 0xFFFF;

    for (int i = 0; i < length; i++) {
        crc = crc ^ buffer[i];
        for (int j = 0; j < 8; j++) {
            unsigned short mask = ((crc & 0x1) != 0) ? 0xA001 : 0x0000;
            crc = ((crc >> 1) & 0x7FFF) ^ mask;
        }
    }
    return crc;
}

static void set_src(struct Packet* buffer, int data_length)
{
    unsigned short crc = crc16((unsigned char *)buffer, HEADER_LENGTH + data_length);

    buffer->data[data_length] = (unsigned char)crc;
    buffer->data[data_length + 1] = (unsigned char)(crc >> 8);
}

static int sendPkt(int fd, struct Packet* buffer, int payload_length)
{
    int pkt_length = MIN_PKT_LENGTH + payload_length;
    int r;

    set_src(buffer, payload_length); /* Payload length is 3 bytes */

    r = write(fd, buffer, pkt_length);
    if (r == -1) {
        fprintf(stderr, "Error sending packet: %s\n", strerror(errno));
        return -1;
    }
    else if (r < pkt_length) {
        fprintf(stderr, "Error sending packet, only %d sent, %d expected\n", r, pkt_length);
        return -1;
    }
    
    return 0;
}

static int receivePkt(int fd, struct Packet* buffer)
{
    int data_len = 0;
    unsigned short calc_crc, rx_crc;

    if (wrappedRead(fd, buffer, MIN_PKT_LENGTH)) {
        return -1;
    }

    if (buffer->function == FUNCTION_WRITE) {
        data_len = 1;
    }
    else if (buffer->function != FUNCTION_REQUEST) {
        fprintf(stderr, "Unexpected function %d, bus is not idle!\n", buffer->function);
        return -1;
    }

    if (data_len) {
        if (wrappedRead(fd, ((char *)buffer) + MIN_PKT_LENGTH, data_len)) {
            return -1;
        }
    }

    calc_crc = crc16((unsigned char *)buffer, HEADER_LENGTH + data_len);
    rx_crc = *((unsigned short*)&buffer->data[data_len]);

    if (calc_crc != rx_crc) {
        fprintf(stderr, "Packet CRC mismatch: received %0x04X calculated 0x%04X", rx_crc, calc_crc);
        return -1;
    }

    return 0;
}

int main(int argc, const char** argv)
{
    int fd, r;
    struct Packet buffer;
    unsigned long new_addr = 0;

    if (argc < 2) {
        usage(argv[0]);
        exit(255);
    }
    if (argc > 2) {
        new_addr = strtoul(argv[2], NULL, 0);
        if (new_addr > 0xFFFF || new_addr == 0) {
            fprintf(stderr, "Invalid new address %s supplied; valid values are 1...65535\n", argv[2]);
            exit(255);
        }
    }

    fd = open(argv[1], O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "Failed to open %s: %s\n", argv[1], strerror(errno));
        exit(255);
    }

    if (setparams(fd) == -1) {
        close(fd);
        exit(255);
    }

    printf("Waiting for IDENT packet on %s...\n", argv[1]);

    r = receivePkt(fd, &buffer);
    if (r == -1) {
        close(fd);
        exit(255);
    }

    printf("Found device address 0x%04X (%u)\n", buffer.device_addr, buffer.device_addr);

    if (new_addr) {
        printf("Setting new address 0f 0x%04X (%u)...\n", new_addr, new_addr);

        buffer.start       = START_BYTE;
        buffer.device_addr = BROADCAST_ADDR;
        buffer.function    = FUNCTION_WRITE;
        buffer.data_addr   = 0;
        buffer.data[0]     = 2; /* Data is the new address, length of 2 */
        buffer.data[1]     = (unsigned char)new_addr;
        buffer.data[2]     = (unsigned char)(new_addr >> 8);
        
        if (!sendPkt(fd, &buffer, 3)) {
            printf("Packet sent, waiting for confirmation...\n");

            r = receivePkt(fd, &buffer);
            if (r != -1) {
                if (buffer.function != FUNCTION_WRITE) {
                    printf("Unexpected FUNCTION %d in reply\n", buffer.function);
                }
                else if (buffer.device_addr != new_addr) {
                    printf("Address is not accepted by the device; keeping 0x%04X (%u)\n", buffer.device_addr, buffer.device_addr);
                }
                else {
                    printf("All done, response is correct\n");
                }
            }
        }
    }

    close(fd);
    return 0;
}
