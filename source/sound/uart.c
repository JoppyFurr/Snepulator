/*
 * Snepulator
 *
 * Utility to pass sn76489 and YM2413 register writes
 * to real chips over UART. A micro-controller needs
 * to interpret the written bytes and pass them along
 * to the appropriate chip.
 *
 * Data takes the following format, as command followed by data:
 *
 *     0000 0000: Gets the micro-controller into a known state where
 *                it is ready to receive commands. Should be sent once
 *                during initialization.
 *
 *     0000 0001: Silence the SN76489 and reset the YM2413. Should be
 *                sent whenever stopping playback as otherwise a
 *                sustained constant tone may continue to play.
 *
 *     01xx xxxx: The following byte should be written to the SN76489.
 *
 *     10aa aaaa: Write to YM2413, the register address is in the lower
 *                six bits. The following byte contains the data.
 *
 *     11xx xxxx: Unused
 *
 * The UART configuration is 28800 8N1.
 */
#define _DEFAULT_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <asm/termbits.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int uart_fd = -1;


#define RING_SIZE 32
static uint16_t uart_ring [RING_SIZE];
static uint64_t write_index = 0;
static uint64_t read_index = 0;

static pthread_t uart_write_pthread;
static pthread_mutex_t uart_mutex = PTHREAD_MUTEX_INITIALIZER;


/*
 * So that the main emulation thread isn't being slowed
 * down waiting for UART transfers, lets do them in a
 * separate thread.
 *
 * Writes are passed by a ring of 16-bit messages.
 */
static void *uart_write_thread (void *dummy)
{
    while (true)
    {
        if (uart_fd >= 0)
        {
            bool more = false;

            do
            {
                bool write_pending = false;
                uint16_t message;

                /* Lock while accessing the ring */
                pthread_mutex_lock (&uart_mutex);
                if (read_index < write_index)
                {
                    message = uart_ring [read_index % RING_SIZE];
                    write_pending = true;
                    read_index++;

                    if (read_index < write_index)
                    {
                        more = true;
                    }
                    else
                    {
                        more = false;
                    }
                }
                pthread_mutex_unlock (&uart_mutex);

                if (write_pending)
                {
                    int ret = write (uart_fd, &message, 2);
                    if (ret != 2)
                    {
                        fprintf (stderr, "UART Write returns %d, expected 2.\n", ret);
                    }
                }

            } while (more);
        }
        usleep (100);
    }

    return NULL;
}


/*
 * Queue one byte to the UART.
 */
static void uart_write (uint8_t command, uint8_t data)
{
    pthread_mutex_lock (&uart_mutex);
    int32_t available = RING_SIZE - (write_index - read_index);

    if (available > 0)
    {
        uint8_t *message_ptr = (uint8_t *) &uart_ring [write_index % RING_SIZE];
        message_ptr [0] = command;
        message_ptr [1] = data;
        write_index++;
    }
    else
    {
        fprintf (stderr, "UART Ring full!.\n");
    }
    pthread_mutex_unlock (&uart_mutex);
}


/*
 * Write to an sn76489 register.
 */
void uart_write_sn76489 (uint8_t data)
{
    uart_write (0x40, data);
}


/*
 * Write to a ym2413 register.
 * Assumes the register address fits in six bits.
 */
void uart_write_ym2413 (uint8_t addr, uint8_t data)
{
    addr &= 0x3f;
    uart_write (0x80 | addr, data);
}


/*
 * Open the UART. If something goes wrong, uart_fd will be set to -1.
 */
void uart_open (const char *path)
{
    struct termios2 uart_attributes;
    bool first = true;

    write_index = 0;
    read_index = 0;

    if (first)
    {
        if (pthread_create (&uart_write_pthread, NULL, uart_write_thread, NULL) != 0)
        {
            fprintf (stderr, "Unable to create uart thread.");
        }
        first = false;
    }

    /* Close any previous UART before opening a new one. */
    if (uart_fd >= 0)
    {
        close (uart_fd);
    }

    uart_fd = open (path, O_RDWR);
    if (uart_fd < 0)
    {
        fprintf (stderr, "Cannot open UART %s: %s.\n", path, strerror (errno));
        return;
    }

    if (ioctl (uart_fd, TCGETS2, &uart_attributes) == -1)
    {
        fprintf (stderr, "Cannot get uart attributes: %s.\n", strerror (errno));
        close (uart_fd);
        uart_fd = -1;
        return;
    }

    /* We want a raw UART. Data we send should arrive at the other end just as we sent it. */
    uart_attributes.c_cflag &= CSIZE;
    uart_attributes.c_cflag |= CS8;         /* 8 */
    uart_attributes.c_cflag &= ~PARENB;     /* N */
    uart_attributes.c_cflag &= ~CSTOPB;     /* 1 */
    uart_attributes.c_cflag &= ~CRTSCTS;    /* Disable flow-control */
    uart_attributes.c_cflag |= CREAD;       /* Enable reading */
    uart_attributes.c_cflag |= CLOCAL;      /* Ignore modem lines */
    uart_attributes.c_cflag &= ~CBAUD;
    uart_attributes.c_cflag |= CBAUDEX;    /* Use custom baud rate */

    uart_attributes.c_ispeed = 28800;       /* 28.8 k */
    uart_attributes.c_ospeed = 28800;

    uart_attributes.c_lflag &= ~ICANON;     /* Disable canonical mode */
    uart_attributes.c_lflag &= ~(ECHO | ECHOE | ECHONL); /* Disable echo */
    uart_attributes.c_lflag &= ~ISIG;       /* Disable control characters */

    uart_attributes.c_iflag &= ~(IXON | IXOFF | IXANY); /* Disable software flow-control */
    uart_attributes.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP |
                                 INLCR |IGNCR | ICRNL); /* Disable handling of special bytes (Rx) */

    uart_attributes.c_oflag &= ~(OPOST | ONLCR); /* Disable handling of special bytes (Tx) */

    if (ioctl(uart_fd, TCSETS2, &uart_attributes) == -1)
    {
        fprintf (stderr, "Cannot set uart attributes: %s.\n", strerror (errno));
        close (uart_fd);
        uart_fd = -1;
        return;
    }

    /* Reset the sound chips to a know state */
    uart_write (0x00, 0x01);
    usleep (100000);
}
