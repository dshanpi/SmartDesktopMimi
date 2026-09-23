#include "tkl_uart.h"
#include <stdio.h>
#include <termios.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <string.h>



#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>


typedef struct {
    int               fd;
    pthread_t           tid;
    TUYA_UART_IRQ_CB    rx_cb;
    uint8_t             readchar;
    uint8_t             readbuff[1024];
} uart_dev_t;

static uart_dev_t s_uart_dev[3];


#ifndef TKL_UART_REDIRECT_LOG_TO_STDOUT
#if defined(CONFIG_TKL_UART_REDIRECT_LOG_TO_STDOUT)
#define TKL_UART_REDIRECT_LOG_TO_STDOUT 1
#else
#define TKL_UART_REDIRECT_LOG_TO_STDOUT 0
#endif
#endif

#if !TKL_UART_REDIRECT_LOG_TO_STDOUT
static const char *g_uart_devname[] = {
    "/dev/ttyAMA0",
    "/dev/ttyAMA1",
    "/dev/ttyAMA2",
};

static speed_t __baud_to_flag(uint32_t baud)
{
    switch (baud) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    case 460800: return B460800;
    case 921600: return B921600;
    default: return B115200;
    }
}

static OPERATE_RET __configure_uart(int fd, TUYA_UART_BASE_CFG_T *cfg)
{
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        return OPRT_COM_ERROR;
    }

    cfmakeraw(&tio);

    /* baudrate */
    speed_t speed = __baud_to_flag(cfg->baudrate);
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);

    /* databits */
    tio.c_cflag &= ~CSIZE;
    switch (cfg->databits) {
    case TUYA_UART_DATA_LEN_7BIT: tio.c_cflag |= CS7; break;
    case TUYA_UART_DATA_LEN_8BIT: default: tio.c_cflag |= CS8; break;
    }

    /* parity */
    tio.c_cflag &= ~(PARENB | PARODD);
    if (cfg->parity == TUYA_UART_PARITY_TYPE_ODD) {
        tio.c_cflag |= (PARENB | PARODD);
    } else if (cfg->parity == TUYA_UART_PARITY_TYPE_EVEN) {
        tio.c_cflag |= PARENB;
    }

    /* stop bits */
    if (cfg->stopbits == TUYA_UART_STOP_LEN_2BIT) {
        tio.c_cflag |= CSTOPB;
    } else {
        tio.c_cflag &= ~CSTOPB;
    }

    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_iflag = 0;
    tio.c_oflag = 0;
    tio.c_lflag = 0;

    tio.c_cc[VTIME] = 0;
    tio.c_cc[VMIN] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}
#endif

static void *__irq_handler(void *arg)
{
    uart_dev_t *uart_dev = arg;

    for (;;) {
        fd_set readfd;

        FD_ZERO(&readfd);
        FD_SET(uart_dev->fd, &readfd);
        select(uart_dev->fd + 1, &readfd, NULL, NULL, NULL);
        if (FD_ISSET(uart_dev->fd, &readfd)){
            uart_dev->rx_cb(0);
        }
    }
}

#if !TKL_UART_REDIRECT_LOG_TO_STDOUT
static void *__uart_irq_handler(void *arg)
{
    uart_dev_t *uart_dev = arg;

    for (;;) {
        fd_set readfd;

        FD_ZERO(&readfd);
        FD_SET(uart_dev->fd, &readfd);
        select(uart_dev->fd + 1, &readfd, NULL, NULL, NULL);
        if (FD_ISSET(uart_dev->fd, &readfd)) {
            uart_dev->rx_cb(0);
        }
    }
}
#endif

static void *__udp_irq_handler(void *arg)
{
    uart_dev_t *uart_dev = arg;

    for (;;) {
        fd_set readfd;
        FD_ZERO(&readfd);
        FD_SET(uart_dev->fd, &readfd);
        select(uart_dev->fd + 1, &readfd, NULL, NULL, NULL);
        if (FD_ISSET(uart_dev->fd, &readfd)){
            ssize_t readlen = recvfrom(uart_dev->fd, uart_dev->readbuff, sizeof(uart_dev->readbuff), 0, NULL, 0);
            for (int i = 0; i < readlen; i++) {
                uart_dev->readchar = uart_dev->readbuff[i];
                uart_dev->rx_cb(1);
            }
        }
    }
}

/**
 * @brief uart init
 * 
 * @param[in] port_id: uart port id, id index starts at 0
 *                     in linux platform, 
 *                         high 16 bits aslo means uart type, 
 *                                   it's value must be one of the TUYA_UART_TYPE_E type
 *                         the low 16bit - means uart port id
 *                         you can input like this TUYA_UART_PORT_ID(TUYA_UART_SYS, 2)
 * @param[in] cfg: uart config
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_uart_init(uint32_t port_id, TUYA_UART_BASE_CFG_T *cfg)
{
    uint32_t port_num = TUYA_UART_GET_PORT_NUMBER(port_id);

    if (port_num >= (sizeof(s_uart_dev) / sizeof(s_uart_dev[0]))) {
        return OPRT_INVALID_PARM;
    }

#if TKL_UART_REDIRECT_LOG_TO_STDOUT
    if (0 == port_num) {
        struct termios term_orig;
        struct termios term_vi;

        /*
         * Dummy/redirect UART on Linux:
         * - RX: read from stdin (interactive console)
         * - TX: write to stdout (see tkl_uart_write)
         */
        s_uart_dev[port_num].fd = open("/dev/stdin", O_RDONLY);
        if (0 > s_uart_dev[port_num].fd) {
            return OPRT_COM_ERROR;
        }
        
        tcgetattr(s_uart_dev[port_num].fd, &term_orig);
        term_vi = term_orig;
        term_vi.c_lflag &= (~ICANON & ~ECHO);   // leave ISIG ON- allow intr's
        term_vi.c_iflag &= (~IXON & ~ICRNL);
        tcsetattr(s_uart_dev[port_num].fd, TCSANOW, &term_vi);

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&s_uart_dev[port_num].tid ,&attr, __irq_handler, &s_uart_dev[port_num]);
        pthread_attr_destroy(&attr);

    } else if (1 == port_num) {
        s_uart_dev[port_num].fd = socket(AF_INET, SOCK_DGRAM, 0);
        fcntl(s_uart_dev[port_num].fd, F_SETFD, FD_CLOEXEC);

        int port = 7878;
        const char *ip = "172.16.208.90"; // IP地址字符串
        struct sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = inet_addr(ip); 

        if (bind(s_uart_dev[port_num].fd, (struct sockaddr*)&address, sizeof(address)) == -1) {
            perror("bind error");
            exit(1);
        }

        pthread_create(&s_uart_dev[port_num].tid , NULL, __udp_irq_handler, &s_uart_dev[port_num]);
    }
#else
    if (port_num >= (sizeof(g_uart_devname) / sizeof(g_uart_devname[0]))) {
        return OPRT_INVALID_PARM;
    }

    const char *devname = g_uart_devname[port_num];
    int fd = open(devname, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open uart");
        return OPRT_COM_ERROR;
    }

    fcntl(fd, F_SETFD, FD_CLOEXEC);

    if (__configure_uart(fd, cfg) != OPRT_OK) {
        close(fd);
        return OPRT_COM_ERROR;
    }

    s_uart_dev[port_num].fd = fd;

    pthread_create(&s_uart_dev[port_num].tid, NULL, __uart_irq_handler, &s_uart_dev[port_num]);
#endif

    return OPRT_OK;
}

/**
 * @brief uart deinit
 * 
 * @param[in] port_id: uart port id, id index starts at 0
 *                     in linux platform, 
 *                         high 16 bits aslo means uart type, 
 *                                   it's value must be one of the TUYA_UART_TYPE_E type
 *                         the low 16bit - means uart port id
 *                         you can input like this TUYA_UART_PORT_ID(TUYA_UART_SYS, 2)
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_uart_deinit(uint32_t port_id)
{
    uint32_t port_num = TUYA_UART_GET_PORT_NUMBER(port_id);

    close(s_uart_dev[port_num].fd);

    if (1 == port_num) {
        pthread_cancel(s_uart_dev[port_num].tid);
        pthread_join(s_uart_dev[port_num].tid, 0);
    }
#if !TKL_UART_REDIRECT_LOG_TO_STDOUT
    else {
        pthread_cancel(s_uart_dev[port_num].tid);
        pthread_join(s_uart_dev[port_num].tid, 0);
    }
#endif

    return OPRT_OK;
}

/**
 * @brief uart write data
 * 
 * @param[in] port_id: uart port id, id index starts at 0
 *                     in linux platform, 
 *                         high 16 bits aslo means uart type, 
 *                                   it's value must be one of the TUYA_UART_TYPE_E type
 *                         the low 16bit - means uart port id
 *                         you can input like this TUYA_UART_PORT_ID(TUYA_UART_SYS, 2)
 * @param[in] data: write buff
 * @param[in] len:  buff len
 *
 * @return return > 0: number of data written; return <= 0: write errror
 */
int tkl_uart_write(uint32_t port_id, void *buff, uint16_t len)
{
#if TKL_UART_REDIRECT_LOG_TO_STDOUT
    uint32_t port_num = TUYA_UART_GET_PORT_NUMBER(port_id);

    if (0 == port_num) {
        return write(STDOUT_FILENO, buff, len);
    } else if (1 == port_num) {
        int port = 7878;
        const char *ip = "172.16.61.117"; // IP地址字符串
        struct sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = inet_addr(ip); 

        return sendto(s_uart_dev[port_num].fd, buff, len, 0, (struct sockaddr *)&address, sizeof(address));
    }

    return -1;
#else
    uint32_t port_num = TUYA_UART_GET_PORT_NUMBER(port_id);
    return write(s_uart_dev[port_num].fd, buff, len);
#endif
}

/**
 * @brief enable uart rx interrupt and regist interrupt callback
 * 
 * @param[in] port_id: uart port id, id index starts at 0
 *                     in linux platform, 
 *                         high 16 bits aslo means uart type, 
 *                                   it's value must be one of the TUYA_UART_TYPE_E type
 *                         the low 16bit - means uart port id
 *                         you can input like this TUYA_UART_PORT_ID(TUYA_UART_SYS, 2)
 * @param[in] rx_cb: receive callback
 *
 * @return none
 */
void tkl_uart_rx_irq_cb_reg(uint32_t port_id, TUYA_UART_IRQ_CB rx_cb)
{
    uint32_t port_num = TUYA_UART_GET_PORT_NUMBER(port_id);
    s_uart_dev[port_num].rx_cb = rx_cb;
    return;
}

/**
 * @brief regist uart tx interrupt callback
 * If this function is called, it indicates that the data is sent asynchronously through interrupt,
 * and then write is invoked to initiate asynchronous transmission.
 *  
 * @param[in] port_id: uart port id, id index starts at 0
 *                     in linux platform, 
 *                         high 16 bits aslo means uart type, 
 *                                   it's value must be one of the TUYA_UART_TYPE_E type
 *                         the low 16bit - means uart port id
 *                         you can input like this TUYA_UART_PORT_ID(TUYA_UART_SYS, 2)
 * @param[in] rx_cb: receive callback
 *
 * @return none
 */
void tkl_uart_tx_irq_cb_reg(uint32_t port_id, TUYA_UART_IRQ_CB tx_cb)
{
    return;
}

/**
 * @brief uart read data
 * 
 * @param[in] port_id: uart port id, id index starts at 0
 *                     in linux platform, 
 *                         high 16 bits aslo means uart type, 
 *                                   it's value must be one of the TUYA_UART_TYPE_E type
 *                         the low 16bit - means uart port id
 *                         you can input like this TUYA_UART_PORT_ID(TUYA_UART_SYS, 2)
 * @param[out] data: read data
 * @param[in] len:  buff len
 * 
 * @return return >= 0: number of data read; return < 0: read errror
 */
int tkl_uart_read(uint32_t port_id, void *buff, uint16_t len)
{
#if TKL_UART_REDIRECT_LOG_TO_STDOUT
    uint32_t port_num = TUYA_UART_GET_PORT_NUMBER(port_id);

    if (0 == port_num) {
        return read(s_uart_dev[port_num].fd, buff, len);
    } else if (1 == port_num) {
        *(uint8_t *)buff = s_uart_dev[port_num].readchar;
        return 1;
    }

    return -1;
#else
    uint32_t port_num = TUYA_UART_GET_PORT_NUMBER(port_id);
    return read(s_uart_dev[port_num].fd, buff, len);
#endif
}

/**
 * @brief set uart transmit interrupt status
 * 
 * @param[in] port_id: uart port id, id index starts at 0
 *                     in linux platform, 
 *                         high 16 bits aslo means uart type, 
 *                                   it's value must be one of the TUYA_UART_TYPE_E type
 *                         the low 16bit - means uart port id
 *                         you can input like this TUYA_UART_PORT_ID(TUYA_UART_SYS, 2)
 * @param[in] enable: TRUE-enalbe tx int, FALSE-disable tx int
 * 
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_uart_set_tx_int(uint32_t port_id, BOOL_T enable)
{
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief set uart receive flowcontrol
 * 
 * @param[in] port_id: uart port id, id index starts at 0
 *                     in linux platform, 
 *                         high 16 bits aslo means uart type, 
 *                                   it's value must be one of the TUYA_UART_TYPE_E type
 *                         the low 16bit - means uart port id
 *                         you can input like this TUYA_UART_PORT_ID(TUYA_UART_SYS, 2)
 * @param[in] enable: TRUE-enalbe rx flowcontrol, FALSE-disable rx flowcontrol
 * 
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_uart_set_rx_flowctrl(uint32_t port_id, BOOL_T enable)
{
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief wait for uart data
 * 
 * @param[in] port_id: uart port id, id index starts at 0
 *                     in linux platform, 
 *                         high 16 bits aslo means uart type, 
 *                                   it's value must be one of the TUYA_UART_TYPE_E type
 *                         the low 16bit - means uart port id
 *                         you can input like this TUYA_UART_PORT_ID(TUYA_UART_SYS, 2)
 * @param[in] timeout_ms: the max wait time, unit is millisecond
 *                        -1 : block indefinitely
 *                        0  : non-block
 *                        >0 : timeout in milliseconds
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_uart_wait_for_data(uint32_t port_id, int timeout_ms)
{
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief uart control
 *
 * @param[in] uart refer to tuya_uart_t
 * @param[in] cmd control command
 * @param[in] arg command argument
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_uart_ioctl(uint32_t port_id, uint32_t cmd, void *arg)
{
    return OPRT_NOT_SUPPORTED;
}

