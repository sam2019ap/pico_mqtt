#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0

#define MEM_ALIGNMENT 4
#define MEM_SIZE 4000

#define MEMP_NUM_TCP_PCB 4
#define MEMP_NUM_TCP_PCB_LISTEN 4
#define MEMP_NUM_UDP_PCB 4
#define MEMP_NUM_SYS_TIMEOUT 10

#define PBUF_POOL_SIZE 16
#define PBUF_POOL_BUFSIZE 1700

#define LWIP_TCP 1
#define TCP_TTL 255
#define TCP_MSS 1460
#define TCP_SND_BUF (4 * TCP_MSS)
#define TCP_WND (4 * TCP_MSS)

#define LWIP_DNS 1
#define LWIP_DHCP 1

#endif