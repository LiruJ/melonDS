/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2013
 * Guillaume Subiron, Yann Bordenave, Serigne Modou Wagne.
 */

#include "slirp.h"
#include "ip6_icmp.h"

#define NDP_Interval \
    g_rand_int_range(slirp->grand, NDP_MinRtrAdvInterval, NDP_MaxRtrAdvInterval)

/* The message sent when emulating PING */
/* Be nice and tell them it's just a pseudo-ping packet */
static const char icmp6_ping_msg[] =
    "This is a pseudo-PING packet used by Slirp to emulate ICMPV6 ECHO-REQUEST "
    "packets.\n";

void icmp6_post_init(Slirp *slirp)
{
    if (!slirp->in6_enabled) {
        return;
    }

    slirp->ra_timer =
        slirp_timer_new(slirp, SLIRP_TIMER_RA, NULL);
    slirp->cb->timer_mod(slirp->ra_timer,
                         slirp->cb->clock_get_ns(slirp->opaque) / SCALE_MS +
                             NDP_Interval,
                         slirp->opaque);
}

void icmp6_cleanup(Slirp *slirp)
{
    if (!slirp->in6_enabled) {
        return;
    }

    slirp->cb->timer_free(slirp->ra_timer, slirp->opaque);
}

/* Send ICMP packet to the Internet, and save it to so_m */
static int icmp6_send(struct socket *so, struct mbuf *m, int hlen)
{
    Slirp *slirp = m->slirp;

    struct sockaddr_in6 addr;

    /*
     * The behavior of reading SOCK_DGRAM+IPPROTO_ICMP sockets is inconsistent
     * between host OSes.  On Linux, only the ICMP header and payload is
     * included.  On macOS/Darwin, the socket acts like a raw socket and
     * includes the IP header as well.  On other BSDs, SOCK_DGRAM+IPPROTO_ICMP
     * sockets aren't supported at all, so we treat them like raw sockets.  It
     * isn't possible to detect this difference at runtime, so we must use an
     * #ifdef to determine if we need to remove the IP header.
     */
#if defined(BSD) && !defined(__GNU__)
    so->so_type = IPPROTO_IPV6;
#else
    so->so_type = IPPROTO_ICMPV6;
#endif

    so->s = slirp_socket(AF_INET6, SOCK_DGRAM, IPPROTO_ICMPV6);
    if (so->s == -1) {
        if (errno == EAFNOSUPPORT
         || errno == EPROTONOSUPPORT
         || errno == EACCES) {
            /* Kernel doesn't support or allow ping sockets. */
            so->so_type = IPPROTO_IPV6;
            so->s = slirp_socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
        }
    }
    if (so->s == -1) {
        return -1;
    }
    so->slirp->cb->register_poll_fd(so->s, so->slirp->opaque);

    if (slirp_bind_outbound(so, AF_INET6) != 0) {
        // bind failed - close socket
        closesocket(so->s);
        so->s = -1;
        return -1;
    }

    M_DUP_DEBUG(slirp, m, 0, 0);
    struct ip6 *ip = mtod(m, struct ip6 *);

    so->so_m = m;
    so->so_faddr6 = ip->ip_dst;
    so->so_laddr6 = ip->ip_src;
    so->so_state = SS_ISFCONNECTED;
    so->so_expire = curtime + SO_EXPIRE;

    addr.sin6_family = AF_INET6;
    addr.sin6_addr = so->so_faddr6;

    slirp_insque(so, &so->slirp->icmp);

    if (sendto(so->s, m->m_data + hlen, m->m_len - hlen, 0,
               (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        DEBUG_MISC("icmp6_input icmp sendto tx errno = %d-%s", errno,
                   strerror(errno));
        icmp6_send_error(m, ICMP6_UNREACH, ICMP6_UNREACH_NO_ROUTE);
        icmp_detach(so);
    }

    return 0;
}

static void icmp6_send_echoreply(struct mbuf *m, Slirp *slirp, struct ip6 *ip,
                                 struct icmp6 *icmp)
{
    struct mbuf *t = m_get(slirp);
    t->m_len = sizeof(struct ip6) + ntohs(ip->ip_pl);
    memcpy(t->m_data, m->m_data, t->m_len);

    /* IPv6 Packet */
    struct ip6 *rip = mtod(t, struct ip6 *);
    rip->ip_dst = ip->ip_src;
    rip->ip_src = ip->ip_dst;

    /* ICMPv6 packet */
    t->m_data += sizeof(struct ip6);
    struct icmp6 *ricmp = mtod(t, struct icmp6 *);
    ricmp->icmp6_type = ICMP6_ECHO_REPLY;
    ricmp->icmp6_cksum = 0;

    /* Checksum */
    t->m_data -= sizeof(struct ip6);
    ricmp->icmp6_cksum = ip6_cksum(t);

    ip6_output(NULL, t, 0);
}

void icmp6_forward_error(struct mbuf *m, uint8_t type, uint8_t code, struct in6_addr *src)
{
    Slirp *slirp = m->slirp;
    struct mbuf *t;
    struct ip6 *ip = mtod(m, struct ip6 *);
    char addrstr[INET6_ADDRSTRLEN];

    DEBUG_CALL("icmp6_send_error");
    DEBUG_ARG("type = %d, code = %d", type, code);

    if (IN6_IS_ADDR_MULTICAST(&ip->ip_src) || in6_zero(&ip->ip_src)) {
        /* TODO icmp error? */
        return;
    }

    t = m_get(slirp);

    /* IPv6 packet */
    struct ip6 *rip = mtod(t, struct ip6 *);
    rip->ip_src = *src;
    rip->ip_dst = ip->ip_src;
    inet_ntop(AF_INET6, &rip->ip_dst, addrstr, INET6_ADDRSTRLEN);
    DEBUG_ARG("target = %s", addrstr);

    rip->ip_nh = IPPROTO_ICMPV6;
    const int error_data_len = MIN(
        m->m_len, slirp->if_mtu - (sizeof(struct ip6) + ICMP6_ERROR_MINLEN));
    rip->ip_pl = htons(ICMP6_ERROR_MINLEN + error_data_len);
    t->m_len = sizeof(struct ip6) + ntohs(rip->ip_pl);

    /* ICMPv6 packet */
    t->m_data += sizeof(struct ip6);
    struct icmp6 *ricmp = mtod(t, struct icmp6 *);
    ricmp->icmp6_type = type;
    ricmp->icmp6_code = code;
    ricmp->icmp6_cksum = 0;

    switch (type) {
    case ICMP6_UNREACH:
    case ICMP6_TIMXCEED:
        ricmp->icmp6_err.unused = 0;
        break;
    case ICMP6_TOOBIG:
        ricmp->icmp6_err.mtu = htonl(slirp->if_mtu);
        break;
    case ICMP6_PARAMPROB:
        /* TODO: Handle this case */
        break;
    default:
        g_assert_not_reached();
    }
    t->m_data += ICMP6_ERROR_MINLEN;
    memcpy(t->m_data, m->m_data, error_data_len);

    /* Checksum */
    t->m_data -= ICMP6_ERROR_MINLEN;
    t->m_data -= sizeof(struct ip6);
    ricmp->icmp6_cksum = ip6_cksum(t);

    ip6_output(NULL, t, 0);
}

void icmp6_send_error(struct mbuf *m, uint8_t type, uint8_t code)
{
    struct in6_addr src = LINKLOCAL_ADDR;
    icmp6_forward_error(m, type, code, &src);
}

/*
 * Reflect the ip packet back to the source
 */
void icmp6_reflect(struct mbuf *m)
{
    register struct ip6 *ip = mtod(m, struct ip6 *);
    int hlen = sizeof(struct ip6);
    register struct icmp6 *icp;

    /*
     * Send an icmp packet back to the ip level,
     * after supplying a checksum.
     */
    m->m_data += hlen;
    m->m_len -= hlen;
    icp = mtod(m, struct icmp6 *);

    icp->icmp6_type = ICMP6_ECHO_REPLY;

    m->m_data -= hlen;
    m->m_len += hlen;

    icp->icmp6_cksum = 0;
    icp->icmp6_cksum = ip6_cksum(m);

    ip->ip_hl = MAXTTL;
    { /* swap */
        struct in6_addr icmp_dst;
        icmp_dst = ip->ip_dst;
        ip->ip_dst = ip->ip_src;
        ip->ip_src = icmp_dst;
    }

    ip6_output((struct socket *)NULL, m, 0);
}

void icmp6_receive(struct socket *so)
{
    struct mbuf *m = so->so_m;
    int hlen = sizeof(struct ip6);
    uint8_t error_code;
    struct icmp6 *icp;
    int id, seq, len;

    m->m_data += hlen;
    m->m_len -= hlen;
    icp = mtod(m, struct icmp6 *);

    id = icp->icmp6_id;
    seq = icp->icmp6_seq;
    len = recv(so->s, icp, M_ROOM(m), 0);

    icp->icmp6_id = id;
    icp->icmp6_seq = seq;

    m->m_data -= hlen;
    m->m_len += hlen;

    if (len == -1 || len == 0) {
        if (errno == ENETUNREACH) {
            error_code = ICMP6_UNREACH_NO_ROUTE;
        } else {
            error_code = ICMP6_UNREACH_ADDRESS;
        }
        DEBUG_MISC(" udp icmp rx errno = %d-%s", errno, strerror(errno));
        icmp6_send_error(so->so_m, ICMP_UNREACH, error_code);
    } else {
        icmp6_reflect(so->so_m);
        so->so_m = NULL; /* Don't m_free() it again! */
    }
    icmp_detach(so);
}

/*
 * Send NDP Router Advertisement
 */
static void ndp_send_ra(Slirp *slirp)
{
    DEBUG_CALL("ndp_send_ra");

    /* Build IPv6 packet */
    struct mbuf *t = m_get(slirp);
    struct ip6 *rip = mtod(t, struct ip6 *);
    size_t pl_size = 0;
    struct in6_addr addr;
    uint32_t scope_id;

    rip->ip_src = (struct in6_addr)LINKLOCAL_ADDR;
    rip->ip_dst = (struct in6_addr)ALLNODES_MULTICAST;
    rip->ip_nh = IPPROTO_ICMPV6;

    /* Build ICMPv6 packet */
    t->m_data += sizeof(struct ip6);
    struct icmp6 *ricmp = mtod(t, struct icmp6 *);
    ricmp->icmp6_type = ICMP6_NDP_RA;
    ricmp->icmp6_code = 0;
    ricmp->icmp6_cksum = 0;

    /* NDP */
    ricmp->icmp6_nra.chl = NDP_AdvCurHopLimit;
    ricmp->icmp6_nra.M = NDP_AdvManagedFlag;
    ricmp->icmp6_nra.O = NDP_AdvOtherConfigFlag;
    ricmp->icmp6_nra.reserved = 0;
    ricmp->icmp6_nra.lifetime = htons(NDP_AdvDefaultLifetime);
    ricmp->icmp6_nra.reach_time = htonl(NDP_AdvReachableTime);
    ricmp->icmp6_nra.retrans_time = htonl(NDP_AdvRetransTime);
    t->m_data += ICMP6_NDP_RA_MINLEN;
    pl_size += ICMP6_NDP_RA_MINLEN;

    /* Source link-layer address (NDP option) */
    struct ndpopt *opt = mtod(t, struct ndpopt *);
    opt->ndpopt_type = NDPOPT_LINKLAYER_SOURCE;
    opt->ndpopt_len = NDPOPT_LINKLAYER_LEN / 8;
    in6_compute_ethaddr(rip->ip_src, opt->ndpopt_linklayer);
    t->m_data += NDPOPT_LINKLAYER_LEN;
    pl_size += NDPOPT_LINKLAYER_LEN;

    /* Prefix information (NDP option) */
    struct ndpopt *opt2 = mtod(t, struct ndpopt *);
    opt2->ndpopt_type = NDPOPT_PREFIX_INFO;
    opt2->ndpopt_len = NDPOPT_PREFIXINFO_LEN / 8;
    opt2->ndpopt_prefixinfo.prefix_length = slirp->vprefix_len;
    opt2->ndpopt_prefixinfo.L = 1;
    opt2->ndpopt_prefixinfo.A = 1;
    opt2->ndpopt_prefixinfo.reserved1 = 0;
    opt2->ndpopt_prefixinfo.valid_lt = htonl(NDP_AdvValidLifetime);
    opt2->ndpopt_prefixinfo.pref_lt = htonl(NDP_AdvPrefLifetime);
    opt2->ndpopt_prefixinfo.reserved2 = 0;
    opt2->ndpopt_prefixinfo.prefix = slirp->vprefix_addr6;
    t->m_data += NDPOPT_PREFIXINFO_LEN;
    pl_size += NDPOPT_PREFIXINFO_LEN;

    /* Prefix information (NDP option) */
    if (get_dns6_addr(&addr, &scope_id) >= 0) {
        /* Host system does have an IPv6 DNS server, announce our proxy.  */
        struct ndpopt *opt3 = mtod(t, struct ndpopt *);
        opt3->ndpopt_type = NDPOPT_RDNSS;
        opt3->ndpopt_len = NDPOPT_RDNSS_LEN / 8;
        opt3->ndpopt_rdnss.reserved = 0;
        opt3->ndpopt_rdnss.lifetime = htonl(2 * NDP_MaxRtrAdvInterval);
        opt3->ndpopt_rdnss.addr = slirp->vnameserver_addr6;
        t->m_data += NDPOPT_RDNSS_LEN;
        pl_size += NDPOPT_RDNSS_LEN;
    }

    rip->ip_pl = htons(pl_size);
    t->m_data -= sizeof(struct ip6) + pl_size;
    t->m_len = sizeof(struct ip6) + pl_size;

    /* ICMPv6 Checksum */
    ricmp->icmp6_cksum = ip6_cksum(t);

    ip6_output(NULL, t, 0);
}

void ra_timer_handler(Slirp *slirp, void *unused)
{
    slirp->cb->timer_mod(slirp->ra_timer,
                         slirp->cb->clock_get_ns(slirp->opaque) / SCALE_MS +
                             NDP_Interval,
                         slirp->opaque);
    ndp_send_ra(slirp);
}

/*
 * Send NDP Neighbor Solitication
 */
void ndp_send_ns(Slirp *slirp, struct in6_addr addr)
{
    char addrstr[INET6_ADDRSTRLEN];

    inet_ntop(AF_INET6, &addr, addrstr, INET6_ADDRSTRLEN);

    DEBUG_CALL("ndp_send_ns");
    DEBUG_ARG("target = %s", addrstr);

    /* Build IPv6 packet */
    struct mbuf *t = m_get(slirp);
    struct ip6 *rip = mtod(t, struct ip6 *);
    rip->ip_src = slirp->vhost_addr6;
    rip->ip_dst = (struct in6_addr)SOLICITED_NODE_PREFIX;
    memcpy(&rip->ip_dst.s6_addr[13], &addr.s6_addr[13], 3);
    rip->ip_nh = IPPROTO_ICMPV6;
    rip->ip_pl = htons(ICMP6_NDP_NS_MINLEN + NDPOPT_LINKLAYER_LEN);
    t->m_len = sizeof(struct ip6) + ntohs(rip->ip_pl);

    /* Build ICMPv6 packet */
    t->m_data += sizeof(struct ip6);
    struct icmp6 *ricmp = mtod(t, struct icmp6 *);
    ricmp->icmp6_type = ICMP6_NDP_NS;
    ricmp->icmp6_code = 0;
    ricmp->icmp6_cksum = 0;

    /* NDP */
    ricmp->icmp6_nns.reserved = 0;
    ricmp->icmp6_nns.target = addr;

    /* Build NDP option */
    t->m_data += ICMP6_NDP_NS_MINLEN;
    struct ndpopt *opt = mtod(t, struct ndpopt *);
    opt->ndpopt_type = NDPOPT_LINKLAYER_SOURCE;
    opt->ndpopt_len = NDPOPT_LINKLAYER_LEN / 8;
    in6_compute_ethaddr(slirp->vhost_addr6, opt->ndpopt_linklayer);

    /* ICMPv6 Checksum */
    t->m_data -= ICMP6_NDP_NA_MINLEN;
    t->m_data -= sizeof(struct ip6);
    ricmp->icmp6_cksum = ip6_cksum(t);

    ip6_output(NULL, t, 1);
}

/*
 * Send NDP Neighbor Advertisement
 */
static void ndp_send_na(Slirp *slirp, struct ip6 *ip, struct icmp6 *icmp)
{
    /* Build IPv6 packet */
    struct mbuf *t = m_get(slirp);
    struct ip6 *rip = mtod(t, struct ip6 *);
    rip->ip_src = icmp->icmp6_nns.target;
    if (in6_zero(&ip->ip_src)) {
        rip->ip_dst = (struct in6_addr)ALLNODES_MULTICAST;
    } else {
        rip->ip_dst = ip->ip_src;
    }
    rip->ip_nh = IPPROTO_ICMPV6;
    rip->ip_pl = htons(ICMP6_NDP_NA_MINLEN + NDPOPT_LINKLAYER_LEN);
    t->m_len = sizeof(struct ip6) + ntohs(rip->ip_pl);

    /* Build ICMPv6 packet */
    t->m_data += sizeof(struct ip6);
    struct icmp6 *ricmp = mtod(t, struct icmp6 *);
    ricmp->icmp6_type = ICMP6_NDP_NA;
    ricmp->icmp6_code = 0;
    ricmp->icmp6_cksum = 0;

    /* NDP */
    ricmp->icmp6_nna.R = NDP_IsRouter;
    ricmp->icmp6_nna.S = !IN6_IS_ADDR_MULTICAST(&rip->ip_dst);
    ricmp->icmp6_nna.O = 1;
    ricmp->icmp6_nna.reserved_1 = 0;
    ricmp->icmp6_nna.reserved_2 = 0;
    ricmp->icmp6_nna.reserved_3 = 0;
    ricmp->icmp6_nna.target = icmp->icmp6_nns.target;

    /* Build NDP option */
    t->m_data += ICMP6_NDP_NA_MINLEN;
    struct ndpopt *opt = mtod(t, struct ndpopt *);
    opt->ndpopt_type = NDPOPT_LINKLAYER_TARGET;
    opt->ndpopt_len = NDPOPT_LINKLAYER_LEN / 8;
    in6_compute_ethaddr(ricmp->icmp6_nna.target, opt->ndpopt_linklayer);

    /* ICMPv6 Checksum */
    t->m_data -= ICMP6_NDP_NA_MINLEN;
    t->m_data -= sizeof(struct ip6);
    ricmp->icmp6_cksum = ip6_cksum(t);

    ip6_output(NULL, t, 0);
}

/*
 * Process a NDP message
 */
static void ndp_input(struct mbuf *m, Slirp *slirp, struct ip6 *ip,
                      struct icmp6 *icmp)
{
    g_assert(M_ROOMBEFORE(m) >= ETH_HLEN);

    m->m_len += ETH_HLEN;
    m->m_data -= ETH_HLEN;
    struct ethhdr *eth = mtod(m, struct ethhdr *);
    m->m_len -= ETH_HLEN;
    m->m_data += ETH_HLEN;

    switch (icmp->icmp6_type) {
    case ICMP6_NDP_RS:
        DEBUG_CALL(" type = Router Solicitation");
        if (ip->ip_hl == 255 && icmp->icmp6_code == 0 &&
            ntohs(ip->ip_pl) >= ICMP6_NDP_RS_MINLEN) {
            /* Gratuitous NDP */
            ndp_table_add(slirp, ip->ip_src, eth->h_source);

            ndp_send_ra(slirp);
        }
        break;

    case ICMP6_NDP_RA:
        DEBUG_CALL(" type = Router Advertisement");
        slirp->cb->guest_error("Warning: guest sent NDP RA, but shouldn't",
                               slirp->opaque);
        break;

    case ICMP6_NDP_NS:
        DEBUG_CALL(" type = Neighbor Solicitation");
        if (ip->ip_hl == 255 && icmp->icmp6_code == 0 &&
            !IN6_IS_ADDR_MULTICAST(&icmp->icmp6_nns.target) &&
            ntohs(ip->ip_pl) >= ICMP6_NDP_NS_MINLEN &&
            (!in6_zero(&ip->ip_src) ||
             in6_solicitednode_multicast(&ip->ip_dst))) {
            if (in6_equal_host(&icmp->icmp6_nns.target)) {
                /* Gratuitous NDP */
                ndp_table_add(slirp, ip->ip_src, eth->h_source);
                ndp_send_na(slirp, ip, icmp);
            }
        }
        break;

    case ICMP6_NDP_NA:
        DEBUG_CALL(" type = Neighbor Advertisement");
        if (ip->ip_hl == 255 && icmp->icmp6_code == 0 &&
            ntohs(ip->ip_pl) >= ICMP6_NDP_NA_MINLEN &&
            !IN6_IS_ADDR_MULTICAST(&icmp->icmp6_nna.target) &&
            (!IN6_IS_ADDR_MULTICAST(&ip->ip_dst) || icmp->icmp6_nna.S == 0)) {
            ndp_table_add(slirp, icmp->icmp6_nna.target, eth->h_source);
        }
        break;

    case ICMP6_NDP_REDIRECT:
        DEBUG_CALL(" type = Redirect");
        slirp->cb->guest_error(
            "Warning: guest sent NDP REDIRECT, but shouldn't", slirp->opaque);
        break;
    }
}

/*
 * Process a received ICMPv6 message.
 */
void icmp6_input(struct mbuf *m)
{
    Slirp *slirp = m->slirp;
    /* NDP reads the ethernet header for gratuitous NDP */
    M_DUP_DEBUG(slirp, m, 1, ETH_HLEN);

    struct icmp6 *icmp;
    struct ip6 *ip = mtod(m, struct ip6 *);
    int hlen = sizeof(struct ip6);

    DEBUG_CALL("icmp6_input");
    DEBUG_ARG("m = %p", m);
    DEBUG_ARG("m_len = %d", m->m_len);

    if (ntohs(ip->ip_pl) < ICMP6_MINLEN) {
    freeit:
        m_free(m);
        goto end_error;
    }

    if (ip6_cksum(m)) {
        goto freeit;
    }

    m->m_len -= hlen;
    m->m_data += hlen;
    icmp = mtod(m, struct icmp6 *);
    m->m_len += hlen;
    m->m_data -= hlen;

    DEBUG_ARG("icmp6_type = %d", icmp->icmp6_type);
    switch (icmp->icmp6_type) {
    case ICMP6_ECHO_REQUEST:
        if (in6_equal_host(&ip->ip_dst)) {
            icmp6_send_echoreply(m, slirp, ip, icmp);
        } else if (slirp->restricted) {
            goto freeit;
        } else {
            struct socket *so;
            struct sockaddr_storage addr;
            int ttl;

            so = socreate(slirp, IPPROTO_ICMPV6);
            if (icmp6_send(so, m, hlen) == 0) {
                /* We could send this as ICMP, good! */
                return;
            }

            /* We could not send this as ICMP, try to send it on UDP echo
             * service (7), wishfully hoping that it is open there. */

            if (udp_attach(so, AF_INET6) == -1) {
                DEBUG_MISC("icmp6_input udp_attach errno = %d-%s", errno,
                           strerror(errno));
                sofree(so);
                m_free(m);
                goto end_error;
            }
            so->so_m = m;
            so->so_ffamily = AF_INET6;
            so->so_faddr6 = ip->ip_dst;
            so->so_fport = htons(7);
            so->so_lfamily = AF_INET6;
            so->so_laddr6 = ip->ip_src;
            so->so_lport = htons(9);
            so->so_state = SS_ISFCONNECTED;

            /* Send the packet */
            addr = so->fhost.ss;
            if (sotranslate_out(so, &addr) < 0) {
                icmp6_send_error(m, ICMP6_UNREACH, ICMP6_UNREACH_NO_ROUTE);
                udp_detach(so);
                return;
            }

            /*
             * Check for TTL
             */
            ttl = ip->ip_hl-1;
            if (ttl <= 0) {
                DEBUG_MISC("udp ttl exceeded");
                icmp6_send_error(m, ICMP6_TIMXCEED, ICMP6_TIMXCEED_INTRANS);
                udp_detach(so);
                break;
            }
            setsockopt(so->s, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &ttl, sizeof(ttl));

            if (sendto(so->s, icmp6_ping_msg, strlen(icmp6_ping_msg), 0,
                       (struct sockaddr *)&addr, sockaddr_size(&addr)) == -1) {
                DEBUG_MISC("icmp6_input udp sendto tx errno = %d-%s", errno,
                           strerror(errno));
                icmp6_send_error(m, ICMP6_UNREACH, ICMP6_UNREACH_NO_ROUTE);
                udp_detach(so);
            }
        } /* if (in6_equal_host(&ip->ip_dst)) */
        break;

    case ICMP6_NDP_RS:
    case ICMP6_NDP_RA:
    case ICMP6_NDP_NS:
    case ICMP6_NDP_NA:
    case ICMP6_NDP_REDIRECT:
        ndp_input(m, slirp, ip, icmp);
        m_free(m);
        break;

    case ICMP6_UNREACH:
    case ICMP6_TOOBIG:
    case ICMP6_TIMXCEED:
    case ICMP6_PARAMPROB:
        /* XXX? report error? close socket? */
    default:
        m_free(m);
        break;
    }

end_error:
    /* m is m_free()'d xor put in a socket xor or given to ip_send */
    return;
}
