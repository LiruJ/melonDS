/* SPDX-License-Identifier: BSD-3-Clause */
/* bootp/dhcp defines */

#ifndef SLIRP_BOOTP_H
#define SLIRP_BOOTP_H

#define BOOTP_SERVER 67
#define BOOTP_CLIENT 68

#define BOOTP_REQUEST 1
#define BOOTP_REPLY 2

#define RFC1533_COOKIE 99, 130, 83, 99
#define RFC1533_PAD 0
#define RFC1533_NETMASK 1
#define RFC1533_TIMEOFFSET 2
#define RFC1533_GATEWAY 3
#define RFC1533_TIMESERVER 4
#define RFC1533_IEN116NS 5
#define RFC1533_DNS 6
#define RFC1533_LOGSERVER 7
#define RFC1533_COOKIESERVER 8
#define RFC1533_LPRSERVER 9
#define RFC1533_IMPRESSSERVER 10
#define RFC1533_RESOURCESERVER 11
#define RFC1533_HOSTNAME 12
#define RFC1533_BOOTFILESIZE 13
#define RFC1533_MERITDUMPFILE 14
#define RFC1533_DOMAINNAME 15
#define RFC1533_SWAPSERVER 16
#define RFC1533_ROOTPATH 17
#define RFC1533_EXTENSIONPATH 18
#define RFC1533_IPFORWARDING 19
#define RFC1533_IPSOURCEROUTING 20
#define RFC1533_IPPOLICYFILTER 21
#define RFC1533_IPMAXREASSEMBLY 22
#define RFC1533_IPTTL 23
#define RFC1533_IPMTU 24
#define RFC1533_IPMTUPLATEAU 25
#define RFC1533_INTMTU 26
#define RFC1533_INTLOCALSUBNETS 27
#define RFC1533_INTBROADCAST 28
#define RFC1533_INTICMPDISCOVER 29
#define RFC1533_INTICMPRESPOND 30
#define RFC1533_INTROUTEDISCOVER 31
#define RFC1533_INTROUTESOLICIT 32
#define RFC1533_INTSTATICROUTES 33
#define RFC1533_LLTRAILERENCAP 34
#define RFC1533_LLARPCACHETMO 35
#define RFC1533_LLETHERNETENCAP 36
#define RFC1533_TCPTTL 37
#define RFC1533_TCPKEEPALIVETMO 38
#define RFC1533_TCPKEEPALIVEGB 39
#define RFC1533_NISDOMAIN 40
#define RFC1533_NISSERVER 41
#define RFC1533_NTPSERVER 42
#define RFC1533_VENDOR 43
#define RFC1533_NBNS 44
#define RFC1533_NBDD 45
#define RFC1533_NBNT 46
#define RFC1533_NBSCOPE 47
#define RFC1533_XFS 48
#define RFC1533_XDM 49

#define RFC2132_REQ_ADDR 50
#define RFC2132_LEASE_TIME 51
#define RFC2132_MSG_TYPE 53
#define RFC2132_SRV_ID 54
#define RFC2132_PARAM_LIST 55
#define RFC2132_MESSAGE 56
#define RFC2132_MAX_SIZE 57
#define RFC2132_RENEWAL_TIME 58
#define RFC2132_REBIND_TIME 59
#define RFC2132_VENDOR_CLASS_ID 60
#define RFC2132_TFTP_SERVER_NAME 66

#define DHCPDISCOVER 1
#define DHCPOFFER 2
#define DHCPREQUEST 3
#define DHCPACK 5
#define DHCPNAK 6

#define RFC1533_VENDOR_MAJOR 0
#define RFC1533_VENDOR_MINOR 0

#define RFC1533_VENDOR_MAGIC 128
#define RFC1533_VENDOR_ADDPARM 129
#define RFC1533_VENDOR_ETHDEV 130
#define RFC1533_VENDOR_HOWTO 132
#define RFC1533_VENDOR_MNUOPTS 160
#define RFC1533_VENDOR_SELECTION 176
#define RFC1533_VENDOR_MOTD 184
#define RFC1533_VENDOR_NUMOFMOTD 8
#define RFC1533_VENDOR_IMG 192
#define RFC1533_VENDOR_NUMOFIMG 16

#define RFC1533_END 255
#define BOOTP_VENDOR_LEN 64
#define DHCP_OPT_LEN 312

struct bootp_t {
    struct ip ip;
    struct udphdr udp;
    uint8_t bp_op;
    uint8_t bp_htype;
    uint8_t bp_hlen;
    uint8_t bp_hops;
    uint32_t bp_xid;
    uint16_t bp_secs;
    uint16_t unused;
    struct in_addr bp_ciaddr;
    struct in_addr bp_yiaddr;
    struct in_addr bp_siaddr;
    struct in_addr bp_giaddr;
    uint8_t bp_hwaddr[16];
    uint8_t bp_sname[64];
    char bp_file[128];
    uint8_t bp_vend[];
};

typedef struct {
    uint16_t allocated;
    uint8_t macaddr[6];
} BOOTPClient;

#define NB_BOOTP_CLIENTS 16

/* Process a bootp packet from the guest */
void bootp_input(struct mbuf *m);

#endif
