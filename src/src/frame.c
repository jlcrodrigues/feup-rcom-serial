#include "frame.h"

static State state = START;

int readFrame(int fd, unsigned char expected, unsigned char addr) {
    unsigned char buf[sizeof(unsigned char) + 1] = {0};
    state = START;

    while (state != STOP) {
        if (read(fd, buf, 1) == -1) return -1;
        switch (state) {
            case START:
                if (buf[0] == FLAG)
                    state = FLAG_RCV;
                break;

            case FLAG_RCV:
                if (buf[0] == addr) {
                    state = A_RCV;
                }
                else if (buf[0] == FLAG)
                    state = FLAG_RCV;
                else 
                    state = START;
                break;

            case A_RCV:
                if (buf[0] == expected) {
                    state = C_RCV;
                }
                else if (buf[0] == FLAG)
                    state = FLAG_RCV;
                else 
                    state = START;
                break;

            case C_RCV:
                if (buf[0] == (addr ^ expected))
                    state = BCC_OK;
                else if (buf[0] == FLAG)
                    state = FLAG_RCV;
                else 
                    state = START;
                break;

            case BCC_OK:
                if (buf[0] == FLAG)
                    return 1;
                else 
                    state = START;
                break;
            case STOP:
                break;
        }
    }

    return 0;
}

int writeFrame(int fd, unsigned char ctrl, unsigned char addr) {
    unsigned char buf[BUF_SIZE + 1] = {0};
    buf[0] = FLAG;
    buf[1] = addr;
    buf[2] = ctrl;
    buf[3] = addr ^ ctrl;
    buf[4] = FLAG;
    buf[5] = '\0';
    return write(fd, buf, 5);
}

void setState(State new_state) {
    state = new_state;
}