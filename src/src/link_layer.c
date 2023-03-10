// Link layer protocol implementation

#include "link_layer.h"
#include "frame.h"
#include "utils.h"
#include <signal.h>

static int fd; //porta série
static struct termios oldtio;
static struct termios newtio;

static LinkLayer parameters;

static int numTries;
static int alarmTriggered;

static int frameNumber = 0;

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

#define BUF_SIZE 256
#define VERBOSE 0 

void alarmHandler(int signum) {
    alarmTriggered = 0;
    numTries++;
}

int openPort(LinkLayer connectionParameters) {
    //fd é a porta série
    fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        perror(connectionParameters.serialPort);
        exit(-1);
    }


    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = connectionParameters.baudRate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0; // Inter-character timer unused
    newtio.c_cc[VMIN] = 0;  // Blocking read until 1 char received

    // VTIME e VMIN should be changed in order to protect with a
    // timeout the reception of the following character(s)

    // Now clean the line and activate the settings for the port
    // tcflush() discards data written to the object referred to
    // by fd but not transmitted, or data received but not read,
    // depending on the value of queue_selector:
    //   TCIFLUSH - flushes data received but not read.
    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");

    return 1;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    unsigned char buffer[BUF_SIZE + 1] = {0};
    parameters = connectionParameters;
    openPort(connectionParameters);
    signal(SIGALRM, alarmHandler);
    numTries = 0;
    alarmTriggered = 0;
    frameNumber = (connectionParameters.role == LlRx);

    if (connectionParameters.role == LlTx) {
        while ((numTries < connectionParameters.nRetransmissions) && getState() != STOP) {
            if (!alarmTriggered) {
                writeCtrlFrame(fd, SET, ADDR_T);
                setState(START);
                alarm(connectionParameters.timeout);
                alarmTriggered = 1;
                if (VERBOSE) printf("Sent SET frame.\n");
            }
            read(fd, buffer, 1);
            if (stateStep(buffer[0], UA, ADDR_T)) {
                if (VERBOSE) printf("Received UA frame.\n");
                return 0;
            }
        }
    }
    else if (connectionParameters.role == LlRx) {
        while (getState() != STOP) {
            read(fd, buffer, 1);
            if (stateStep(buffer[0], SET, ADDR_T)) {
                if (VERBOSE) printf("Read SET frame.\n");
                writeCtrlFrame(fd, UA, ADDR_T);
                if (VERBOSE) printf("Sent UA frame.\n");

                return 0;
            };
        }
    }
    return -1;
}

int disconnect()
{
    unsigned char buffer[BUF_SIZE + 1] = {0};
    numTries = 0;
    alarmTriggered = 0;

    if (parameters.role == LlTx) {
        while ((numTries < parameters.nRetransmissions) && getState() != STOP) {
            if (!alarmTriggered) {
                writeCtrlFrame(fd, DISC, ADDR_T);
                if (VERBOSE) printf("Sent DISC frame.\n");
                setState(START);
                alarm(parameters.timeout);
                alarmTriggered = 1;
            }
            read(fd, buffer, 1);
            if (stateStep(buffer[0], DISC, ADDR_T) == DISCONNECT) {
                if (VERBOSE) printf("Received DISC frame.\n");
                writeCtrlFrame(fd, UA, ADDR_T);
                if (VERBOSE) printf("Sent UA frame.\n");
                break;
            }
        }
    }
    else if (parameters.role == LlRx) {
        while ((numTries < parameters.nRetransmissions) && getState() != STOP) {
            if (!alarmTriggered) {
                writeCtrlFrame(fd, DISC, ADDR_T);
                if (VERBOSE) printf("Sent DISC frame.\n");
                setState(START);
                alarm(parameters.timeout);
                alarmTriggered = 1;
            }
            read(fd, buffer, 1);
            if (stateStep(buffer[0], UA, ADDR_T) == COMPLETE) {
                if (VERBOSE) printf("Received UA frame.\n");
                break;
            }
        }
    }
    return 0;
}

int prepareWrite(const unsigned char* buf, unsigned char* dest, int bufSize) {
    unsigned char copy[BUF_SIZE + 1] = {0};
    unsigned char to_stuff[BUF_SIZE + 1] = {0};

    dest[0] = FLAG; 
    dest[1] = ADDR_T; 
    dest[2] = frameNumber << I_CTRL_SHIFT; 
    dest[3] = ADDR_T ^ dest[2];

    unsigned char bcc = 0;
    // for loop does the parity over each of the bits of 'the data octets' and the 'BCC'
    for (int j = 0; j < bufSize; j++) {
        bcc ^= buf[j];
        to_stuff[j] = buf[j];
    }
    to_stuff[bufSize] = bcc;

    bufSize = stuff(to_stuff, copy, bufSize + 1); 
    //now 'copy' contains the 'to_stuff' buffer with the 'byte stuffing' engine changes
    int i = 0;
    for (; i < bufSize; i++) {
        dest[i + 4] = copy[i];
        //printf("%x\n", dest[i]);
    }
    i += 4;
    dest[i] = FLAG; //printf("%x\n", dest[i + 1]);
    //'dest' é uma trama de Informação
    return i + 1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    unsigned char tmp[BUF_SIZE + 1] = {0};
    unsigned char buffer[BUF_SIZE + 1] = {0};
    numTries = 0;


    bufSize = prepareWrite(buf, tmp, bufSize);
    //'tmp' é uma trama de Informação

    int nbytes = 0;
    alarmTriggered = 0;
    alarm(0);

    while ((numTries < parameters.nRetransmissions) && getState() != STOP) {
        if (!alarmTriggered) {
            nbytes = write(fd, tmp, bufSize);
            setState(START);
            alarm(parameters.timeout);
            alarmTriggered = 1;
            if (VERBOSE) printf("%d bytes written.\n", nbytes);
        }
        read(fd, buffer, 1);
        int step = stateStep(buffer[0], RR | (!frameNumber << R_CTRL_SHIFT), ADDR_T);
        if (step == COMPLETE) {
            if (VERBOSE) printf("Received RR frame.\n");
            frameNumber = !frameNumber;
            return nbytes;
        }
        else if (step == REJECTED) {
            if (VERBOSE) printf("Received REJ frame.\n");
            alarmTriggered = 0;
            if (numTries > 0) numTries = 0;
            numTries++;
        }
    }
    return -1;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet) {
    unsigned char buffer[BUF_SIZE + 1] = {0};
    int size = 0;
    int step = 0;
    setState(START);
    while (getState() != STOP) {
        if (read(fd, buffer, 1)) {
            unsigned char expected = (!frameNumber) << I_CTRL_SHIFT;
            step = stateStep(buffer[0], expected, ADDR_T);
            if (step == COMPLETE || step == DUPLICATE) {
                if (step == COMPLETE) {
                    size = getData(packet);
                    if (VERBOSE) printf("Sent RR frame.\n");
                    writeCtrlFrame(fd, RR | (frameNumber << R_CTRL_SHIFT), ADDR_T);
                    frameNumber = !frameNumber;
                }
                else if (step == DUPLICATE) {
                    writeCtrlFrame(fd, RR | (!frameNumber << R_CTRL_SHIFT), ADDR_T);
                }
                return size;
            }
            else if (step == REJECTED) {
                writeCtrlFrame(fd, REJ | (frameNumber << R_CTRL_SHIFT), ADDR_T);
                if (VERBOSE) printf("Sent REJ frame.\n");
            }
            else if (step == DISCONNECT) {
                if (VERBOSE) printf("Received DISC frame.\n");
                disconnect();
                return -1;
            }
            step = 0;
        }
    }
    return size;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    unsigned char buffer[BUF_SIZE + 1] = {0};
    numTries = 0;
    alarmTriggered = 0;

    if (parameters.role == LlTx) {
        while ((numTries < parameters.nRetransmissions) && getState() != STOP) {
            if (!alarmTriggered) {
                writeCtrlFrame(fd, DISC, ADDR_T);
                if (VERBOSE) printf("Sent DISC frame.\n");
                setState(START);
                alarm(parameters.timeout);
                alarmTriggered = 1;
            }
            read(fd, buffer, 1);
            if (stateStep(buffer[0], DISC, ADDR_T)) {
                if (VERBOSE) printf("Received DISC frame.\n");
                writeCtrlFrame(fd, UA, ADDR_T);
                if (VERBOSE) printf("Sent UA frame.\n");
                break;
            }
        }
    }
    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}
