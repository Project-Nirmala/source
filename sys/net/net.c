#include <net.h>
#include <proc.h>
#include <cpu.h>
#include <mm.h>
#include <kern.h>
#include <sys/errno.h>

#define ETH_IP  0x0800
#define ETH_ARP 0x0806
#define P_ICMP  1
#define P_TCP   6
#define P_UDP   17

#define TF_FIN 1
#define TF_SYN 2
#define TF_RST 4
#define TF_PSH 8
#define TF_ACK 16

#define MSS    1400
#define RXSZ   16384
#define DQ     8
#define NSOCK  16

enum { T_CLOSED, T_SYN, T_EST, T_RST };
enum { S_STREAM = 1, S_DGRAM = 2, S_ICMP = 3 };

struct eth { uint8_t dst[6], src[6]; uint16_t type; } __packed;
struct arp { uint16_t htype, ptype; uint8_t hlen, plen; uint16_t op; uint8_t sha[6]; uint32_t spa; uint8_t tha[6]; uint32_t tpa; } __packed;
struct ip4 { uint8_t vihl, tos; uint16_t len, id, frag; uint8_t ttl, proto; uint16_t csum; uint32_t src, dst; } __packed;
struct udph { uint16_t sport, dport, len, csum; } __packed;
struct tcph { uint16_t sport, dport; uint32_t seq, ack; uint8_t off, flags; uint16_t win, csum, urg; } __packed;
struct icmph { uint8_t type, code; uint16_t csum, id, seq; } __packed;

struct dgram { uint32_t addr; uint16_t port, len; uint8_t data[]; };

struct sock {
    int      used, type, state, err, eof, connected;
    uint32_t laddr, raddr;
    uint16_t lport, rport;
    uint32_t snd_una, snd_nxt, rcv_nxt;
    uint8_t *rx;
    uint32_t rx_h, rx_n;
    struct dgram *dq[DQ];
    int      dq_h, dq_n;
};

static struct sock socks[NSOCK];
static uint8_t mymac[6];
static uint32_t my_ip, my_mask, my_gw, my_dns;
static int nic, configured;
static uint16_t ip_id, next_port = 49152;
static struct { uint32_t ip; uint8_t mac[6]; int ok; } arpc[8];
static int arp_next;

static struct { int active; uint32_t xid, ip, server, mask, gw, dns; int got_offer, got_ack; } dh;

static inline uint16_t htons(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t htonl(uint32_t x) { return __builtin_bswap32(x); }

static uint32_t csum_add(uint32_t s, const void *d, size_t n)
{
    const uint8_t *p = d;

    for (; n > 1; n -= 2, p += 2)
        s += (uint32_t)(p[0] << 8 | p[1]);
    if (n)
        s += (uint32_t)(p[0] << 8);
    return s;
}

static uint16_t csum_fin(uint32_t s)
{
    while (s >> 16)
        s = (s & 0xFFFF) + (s >> 16);
    return htons((uint16_t)(~s & 0xFFFF));
}

static uint32_t pseudo(uint32_t src, uint32_t dst, uint8_t proto, uint16_t len)
{
    return (src >> 16) + (src & 0xFFFF) + (dst >> 16) + (dst & 0xFFFF) + proto + len;
}

#define WAIT(cond, ticks, rv)                                           \
    do {                                                                \
        uint64_t _e = pit_ticks() + (uint64_t)(ticks);                  \
        int _k;                                                         \
        (rv) = 0;                                                       \
        for (;;) {                                                      \
            for (_k = 0; _k < 1000; _k++) {                             \
                net_poll();                                             \
                if (cond)                                               \
                    break;                                              \
                __asm__ volatile("pause" : : : "memory");               \
            }                                                           \
            if (cond)                                                   \
                break;                                                  \
            if (curproc->pending_sig) { (rv) = -EINTR; break; }         \
            if (pit_ticks() >= _e) { (rv) = -ETIMEDOUT; break; }        \
            proc_sleep_ticks(1);                                        \
        }                                                               \
    } while (0)

static void eth_send(const uint8_t *dst, uint16_t type, const void *pl, size_t len)
{
    static uint8_t frame[1600];
    struct eth *e = (struct eth *)frame;

    if (len > sizeof(frame) - sizeof(*e))
        return;
    memcpy(e->dst, dst, 6);
    memcpy(e->src, mymac, 6);
    e->type = htons(type);
    memcpy(frame + sizeof(*e), pl, len);
    if (len + sizeof(*e) < 60) {
        memset(frame + sizeof(*e) + len, 0, 60 - sizeof(*e) - len);
        len = 60 - sizeof(*e);
    }
    vnet_send(frame, len + sizeof(*e));
}

static const uint8_t bcast[6] = { 255, 255, 255, 255, 255, 255 };

static void arp_put(uint32_t ip, const uint8_t *mac)
{
    int i;

    for (i = 0; i < 8; i++)
        if (arpc[i].ok && arpc[i].ip == ip) {
            memcpy(arpc[i].mac, mac, 6);
            return;
        }
    i = arp_next++ & 7;
    arpc[i].ip = ip;
    arpc[i].ok = 1;
    memcpy(arpc[i].mac, mac, 6);
}

static int arp_get(uint32_t ip, uint8_t *mac)
{
    int i;

    for (i = 0; i < 8; i++)
        if (arpc[i].ok && arpc[i].ip == ip) {
            memcpy(mac, arpc[i].mac, 6);
            return 1;
        }
    return 0;
}

static void arp_send(uint16_t op, const uint8_t *tha, uint32_t tpa)
{
    struct arp a;

    a.htype = htons(1);
    a.ptype = htons(ETH_IP);
    a.hlen = 6;
    a.plen = 4;
    a.op = htons(op);
    memcpy(a.sha, mymac, 6);
    a.spa = htonl(my_ip);
    memcpy(a.tha, op == 1 ? (const uint8_t *)"\0\0\0\0\0" : tha, 6);
    a.tpa = htonl(tpa);
    eth_send(op == 1 ? bcast : tha, ETH_ARP, &a, sizeof(a));
}

static int arp_resolve(uint32_t ip, uint8_t *mac)
{
    int t, r;

    for (t = 0; t < 4; t++) {
        if (arp_get(ip, mac))
            return 0;
        arp_send(1, bcast, ip);
        WAIT(arp_get(ip, mac), 30, r);
        if (r == 0)
            return 0;
        if (r == -EINTR)
            return r;
    }
    return -ENETUNREACH;
}

static int ip_send(uint8_t proto, uint32_t dst, const void *pl, size_t len)
{
    static uint8_t pkt[1600];
    struct ip4 *h = (struct ip4 *)pkt;
    uint8_t mac[6];
    uint32_t hop;

    if (len > 1500 - sizeof(*h))
        return -EINVAL;
    memset(h, 0, sizeof(*h));
    h->vihl = 0x45;
    h->len = htons((uint16_t)(sizeof(*h) + len));
    h->id = htons(ip_id++);
    h->ttl = 64;
    h->proto = proto;
    h->src = htonl(my_ip);
    h->dst = htonl(dst);
    h->csum = csum_fin(csum_add(0, h, sizeof(*h)));
    memcpy(pkt + sizeof(*h), pl, len);
    if (dst == 0xFFFFFFFFu) {
        memcpy(mac, bcast, 6);
    } else {
        hop = (dst & my_mask) == (my_ip & my_mask) ? dst : my_gw;
        if (!hop)
            return -ENETUNREACH;
        {
            int r = arp_resolve(hop, mac);

            if (r < 0)
                return r;
        }
    }
    eth_send(mac, ETH_IP, pkt, sizeof(*h) + len);
    return 0;
}

static int udp_out(uint16_t sport, uint16_t dport, uint32_t dst, const void *pl, size_t len)
{
    uint8_t buf[1500];
    struct udph *u = (struct udph *)buf;

    if (len > 1472)
        return -EINVAL;
    u->sport = htons(sport);
    u->dport = htons(dport);
    u->len = htons((uint16_t)(sizeof(*u) + len));
    u->csum = 0;
    memcpy(buf + sizeof(*u), pl, len);
    u->csum = csum_fin(csum_add(pseudo(my_ip, dst, P_UDP, (uint16_t)(sizeof(*u) + len)), buf, sizeof(*u) + len));
    if (!u->csum)
        u->csum = 0xFFFF;
    return ip_send(P_UDP, dst, buf, sizeof(*u) + len);
}

static void dhcp_send(int type)
{
    uint8_t m[300];
    uint8_t *o;

    memset(m, 0, sizeof(m));
    m[0] = 1;
    m[1] = 1;
    m[2] = 6;
    m[4] = (uint8_t)(dh.xid >> 24);
    m[5] = (uint8_t)(dh.xid >> 16);
    m[6] = (uint8_t)(dh.xid >> 8);
    m[7] = (uint8_t)dh.xid;
    m[10] = 0x80;
    memcpy(m + 28, mymac, 6);
    m[236] = 99; m[237] = 130; m[238] = 83; m[239] = 99;
    o = m + 240;
    *o++ = 53; *o++ = 1; *o++ = (uint8_t)type;
    if (type == 3) {
        *o++ = 50; *o++ = 4;
        *o++ = (uint8_t)(dh.ip >> 24); *o++ = (uint8_t)(dh.ip >> 16); *o++ = (uint8_t)(dh.ip >> 8); *o++ = (uint8_t)dh.ip;
        *o++ = 54; *o++ = 4;
        *o++ = (uint8_t)(dh.server >> 24); *o++ = (uint8_t)(dh.server >> 16); *o++ = (uint8_t)(dh.server >> 8); *o++ = (uint8_t)dh.server;
    }
    *o++ = 55; *o++ = 3; *o++ = 1; *o++ = 3; *o++ = 6;
    *o++ = 255;
    udp_out(68, 67, 0xFFFFFFFFu, m, (size_t)(o - m));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void dhcp_input(const uint8_t *m, size_t len)
{
    const uint8_t *o, *end = m + len;
    int type = 0;
    uint32_t mask = 0, gw = 0, dns = 0, server = 0;

    if (!dh.active || len < 244 || m[0] != 2 || rd32(m + 4) != dh.xid || memcmp(m + 28, mymac, 6))
        return;
    for (o = m + 240; o + 1 < end && *o != 255;) {
        uint8_t c = *o, l;

        if (c == 0) {
            o++;
            continue;
        }
        l = o[1];
        if (o + 2 + l > end)
            break;
        if (c == 53) type = o[2];
        else if (c == 1 && l >= 4) mask = rd32(o + 2);
        else if (c == 3 && l >= 4) gw = rd32(o + 2);
        else if (c == 6 && l >= 4) dns = rd32(o + 2);
        else if (c == 54 && l >= 4) server = rd32(o + 2);
        o += 2 + l;
    }
    if (type == 2 && !dh.got_offer) {
        dh.ip = rd32(m + 16);
        dh.server = server;
        dh.got_offer = 1;
    } else if (type == 5 && dh.got_offer) {
        dh.mask = mask;
        dh.gw = gw;
        dh.dns = dns;
        dh.got_ack = 1;
    }
}

static int dhcp_run(void)
{
    int r, t;

    memset(&dh, 0, sizeof(dh));
    dh.active = 1;
    dh.xid = 0x4E495230u ^ (uint32_t)pit_ticks();
    for (t = 0; t < 3; t++) {
        dhcp_send(1);
        WAIT(dh.got_offer, 100, r);
        if (r == 0)
            break;
    }
    if (!dh.got_offer)
        goto out;
    for (t = 0; t < 3; t++) {
        dhcp_send(3);
        WAIT(dh.got_ack, 100, r);
        if (r == 0)
            break;
    }
out:
    dh.active = 0;
    if (!dh.got_ack)
        return -1;
    my_ip = dh.ip;
    my_mask = dh.mask ? dh.mask : 0xFFFFFF00u;
    my_gw = dh.gw;
    my_dns = dh.dns ? dh.dns : dh.gw;
    return 0;
}

static struct sock *sock_alloc(void)
{
    int i;

    for (i = 0; i < NSOCK; i++)
        if (!socks[i].used) {
            memset(&socks[i], 0, sizeof(socks[i]));
            socks[i].used = 1;
            return &socks[i];
        }
    return NULL;
}

static uint16_t port_alloc(void)
{
    if (next_port < 49152)
        next_port = 49152;
    return next_port++;
}

static int seq_ge(uint32_t a, uint32_t b) { return (int32_t)(a - b) >= 0; }

static void tcp_out(struct sock *s, uint8_t flags, const void *data, size_t len, int syn)
{
    uint8_t seg[1500];
    struct tcph *t = (struct tcph *)seg;
    size_t hl = syn ? 24 : 20;

    memset(t, 0, hl);
    t->sport = htons(s->lport);
    t->dport = htons(s->rport);
    t->seq = htonl(s->snd_nxt);
    t->ack = htonl(s->rcv_nxt);
    t->off = (uint8_t)(hl / 4 << 4);
    t->flags = flags;
    t->win = htons((uint16_t)(RXSZ - s->rx_n > 65535 ? 65535 : RXSZ - s->rx_n));
    if (syn) {
        seg[20] = 2; seg[21] = 4; seg[22] = MSS >> 8; seg[23] = MSS & 0xFF;
    }
    if (len)
        memcpy(seg + hl, data, len);
    t->csum = csum_fin(csum_add(pseudo(my_ip, s->raddr, P_TCP, (uint16_t)(hl + len)), seg, hl + len));
    ip_send(P_TCP, s->raddr, seg, hl + len);
}

static void tcp_rst(uint32_t src, const struct tcph *in, size_t plen)
{
    uint8_t seg[20];
    struct tcph *t = (struct tcph *)seg;
    uint32_t ack = __builtin_bswap32(in->seq) + (uint32_t)plen + ((in->flags & (TF_SYN | TF_FIN)) ? 1 : 0);

    memset(t, 0, sizeof(*t));
    t->sport = in->dport;
    t->dport = in->sport;
    t->off = 5 << 4;
    if (in->flags & TF_ACK) {
        t->seq = in->ack;
        t->flags = TF_RST;
    } else {
        t->ack = htonl(ack);
        t->flags = TF_RST | TF_ACK;
    }
    t->csum = csum_fin(csum_add(pseudo(my_ip, src, P_TCP, 20), seg, 20));
    ip_send(P_TCP, src, seg, 20);
}

static void tcp_input(uint32_t src, const uint8_t *d, size_t len)
{
    const struct tcph *t = (const struct tcph *)d;
    struct sock *s = NULL;
    size_t hl, plen;
    uint32_t seq, ack;
    const uint8_t *pl;
    int i;

    if (len < 20)
        return;
    hl = (size_t)(t->off >> 4) * 4;
    if (hl < 20 || hl > len)
        return;
    if (csum_fin(csum_add(pseudo(src, my_ip, P_TCP, (uint16_t)len), d, len)))
        return;
    plen = len - hl;
    pl = d + hl;
    seq = htonl(t->seq);
    ack = htonl(t->ack);
    for (i = 0; i < NSOCK; i++)
        if (socks[i].used && socks[i].type == S_STREAM && socks[i].state != T_CLOSED &&
            socks[i].lport == htons(t->dport) && socks[i].rport == htons(t->sport) && socks[i].raddr == src) {
            s = &socks[i];
            break;
        }
    if (!s) {
        if (!(t->flags & TF_RST))
            tcp_rst(src, t, plen);
        return;
    }
    if (t->flags & TF_RST) {
        s->err = s->state == T_SYN ? ECONNREFUSED : ECONNRESET;
        s->state = T_RST;
        return;
    }
    if (s->state == T_SYN) {
        if ((t->flags & (TF_SYN | TF_ACK)) == (TF_SYN | TF_ACK) && ack == s->snd_nxt) {
            s->rcv_nxt = seq + 1;
            s->snd_una = ack;
            s->state = T_EST;
            tcp_out(s, TF_ACK, NULL, 0, 0);
        }
        return;
    }
    if ((t->flags & TF_ACK) && seq_ge(ack, s->snd_una) && seq_ge(s->snd_nxt, ack))
        s->snd_una = ack;
    if (plen && seq == s->rcv_nxt) {
        uint32_t room = RXSZ - s->rx_n, n = plen < room ? (uint32_t)plen : room, k;

        for (k = 0; k < n; k++)
            s->rx[(s->rx_h + s->rx_n + k) % RXSZ] = pl[k];
        s->rx_n += n;
        s->rcv_nxt += n;
        if (n == plen && (t->flags & TF_FIN)) {
            s->rcv_nxt++;
            s->eof = 1;
        }
        tcp_out(s, TF_ACK, NULL, 0, 0);
    } else if (plen) {
        tcp_out(s, TF_ACK, NULL, 0, 0);
    } else if ((t->flags & TF_FIN) && seq == s->rcv_nxt) {
        s->rcv_nxt++;
        s->eof = 1;
        tcp_out(s, TF_ACK, NULL, 0, 0);
    }
}

static void udp_input(uint32_t src, const uint8_t *d, size_t len)
{
    const struct udph *u = (const struct udph *)d;
    uint16_t dport, ulen;
    int i;

    if (len < sizeof(*u))
        return;
    ulen = htons(u->len);
    if (ulen < sizeof(*u) || ulen > len)
        return;
    dport = htons(u->dport);
    if (dport == 68) {
        dhcp_input(d + sizeof(*u), ulen - sizeof(*u));
        return;
    }
    for (i = 0; i < NSOCK; i++) {
        struct sock *s = &socks[i];

        if (s->used && s->type == S_DGRAM && s->lport == dport &&
            (!s->connected || (s->raddr == src && s->rport == htons(u->sport)))) {
            struct dgram *g;

            if (s->dq_n >= DQ)
                return;
            g = kmalloc(sizeof(*g) + ulen - sizeof(*u));
            if (!g)
                return;
            g->addr = src;
            g->port = htons(u->sport);
            g->len = (uint16_t)(ulen - sizeof(*u));
            memcpy(g->data, d + sizeof(*u), g->len);
            s->dq[(s->dq_h + s->dq_n++) % DQ] = g;
            return;
        }
    }
}

static void icmp_input(uint32_t src, const uint8_t *d, size_t len)
{
    uint8_t buf[1500];
    struct icmph *c = (struct icmph *)buf;

    int i;

    if (len < sizeof(*c) || len > sizeof(buf))
        return;
    if (d[0] == 0) {
        for (i = 0; i < NSOCK; i++) {
            struct sock *s = &socks[i];
            struct dgram *g;

            if (!s->used || s->type != S_ICMP || s->dq_n >= DQ)
                continue;
            g = kmalloc(sizeof(*g) + len);
            if (!g)
                return;
            g->addr = src;
            g->port = 0;
            g->len = (uint16_t)len;
            memcpy(g->data, d, len);
            s->dq[(s->dq_h + s->dq_n++) % DQ] = g;
        }
        return;
    }
    if (d[0] != 8)
        return;
    memcpy(buf, d, len);
    c->type = 0;
    c->csum = 0;
    c->csum = csum_fin(csum_add(0, buf, len));
    ip_send(P_ICMP, src, buf, len);
}

static void ip_input(const uint8_t *p, size_t len)
{
    const struct ip4 *h = (const struct ip4 *)p;
    size_t hl, tl;
    uint32_t dst, src;

    if (len < sizeof(*h) || (h->vihl >> 4) != 4)
        return;
    hl = (size_t)(h->vihl & 15) * 4;
    tl = htons(h->len);
    if (hl < 20 || tl < hl || tl > len || (htons(h->frag) & 0x3FFF))
        return;
    if (csum_fin(csum_add(0, p, hl)))
        return;
    dst = htonl(h->dst);
    src = htonl(h->src);
    if (my_ip && dst != my_ip && dst != 0xFFFFFFFFu && dst != (my_ip | ~my_mask))
        return;
    if (h->proto == P_UDP)
        udp_input(src, p + hl, tl - hl);
    else if (h->proto == P_TCP)
        tcp_input(src, p + hl, tl - hl);
    else if (h->proto == P_ICMP && my_ip)
        icmp_input(src, p + hl, tl - hl);
}

static void arp_input(const uint8_t *p, size_t len)
{
    const struct arp *a = (const struct arp *)p;

    if (len < sizeof(*a) || htons(a->ptype) != ETH_IP || a->plen != 4)
        return;
    if (my_ip && htonl(a->spa) != 0)
        arp_put(htonl(a->spa), a->sha);
    if (htons(a->op) == 1 && my_ip && htonl(a->tpa) == my_ip)
        arp_send(2, a->sha, htonl(a->spa));
}

static void rx_frame(const uint8_t *f, size_t len)
{
    const struct eth *e = (const struct eth *)f;

    if (len < sizeof(*e))
        return;
    if (htons(e->type) == ETH_ARP)
        arp_input(f + sizeof(*e), len - sizeof(*e));
    else if (htons(e->type) == ETH_IP)
        ip_input(f + sizeof(*e), len - sizeof(*e));
}

void net_poll(void)
{
    if (nic)
        vnet_poll(rx_frame);
}

void net_init(void)
{
    if (vnet_probe(mymac)) {
        nic = 1;
        kprintf("[ok] net: virtio-net %x:%x:%x:%x:%x:%x\n", mymac[0], mymac[1], mymac[2], mymac[3], mymac[4], mymac[5]);
    }
}

int net_up(void)
{
    if (!nic)
        return -ENODEV;
    if (configured)
        return 0;
    if (dhcp_run() < 0) {
        my_ip = 0x0A00020Fu;
        my_mask = 0xFFFFFF00u;
        my_gw = 0x0A000202u;
        my_dns = 0x0A000203u;
    }
    configured = 1;
    kprintf("net: ip %u.%u.%u.%u\n", my_ip >> 24, (my_ip >> 16) & 255, (my_ip >> 8) & 255, my_ip & 255);
    return 0;
}

void net_info(struct netinfo *o)
{
    o->ip = my_ip;
    o->mask = my_mask;
    o->gw = my_gw;
    o->dns = my_dns;
    memcpy(o->mac, mymac, 6);
    o->up = (uint16_t)configured;
}

struct sock *sock_new(int type)
{
    struct sock *s;

    if (type != S_STREAM && type != S_DGRAM && type != S_ICMP)
        return NULL;
    s = sock_alloc();
    if (!s)
        return NULL;
    s->type = type;
    return s;
}

static int tcp_xmit(struct sock *s, uint8_t flags, const void *data, size_t len, int syn, int tries, int tmo)
{
    uint32_t seq = s->snd_nxt;
    int t, r = 0;

    for (t = 0; t < tries; t++) {
        s->snd_nxt = seq;
        tcp_out(s, flags, data, len, syn);
        s->snd_nxt = seq + (uint32_t)len + ((flags & (TF_SYN | TF_FIN)) ? 1 : 0);
        if (syn) {
            WAIT(s->state != T_SYN, tmo, r);
        } else {
            WAIT(s->err || seq_ge(s->snd_una, s->snd_nxt), tmo, r);
        }
        if (r == -EINTR)
            return r;
        if (s->err)
            return -s->err;
        if (r == 0)
            return 0;
    }
    return -ETIMEDOUT;
}

void sock_free(struct sock *s)
{
    int i;

    if (s->type == S_STREAM && s->state == T_EST)
        tcp_xmit(s, TF_FIN | TF_ACK, NULL, 0, 0, 2, 30);
    s->state = T_CLOSED;
    if (s->rx)
        kfree(s->rx);
    for (i = 0; i < s->dq_n; i++)
        kfree(s->dq[(s->dq_h + i) % DQ]);
    memset(s, 0, sizeof(*s));
}

int sock_bind(struct sock *s, uint16_t port)
{
    int i;

    for (i = 0; i < NSOCK; i++)
        if (socks[i].used && &socks[i] != s && socks[i].type == s->type && socks[i].lport == port && port)
            return -EADDRINUSE;
    s->lport = port ? port : port_alloc();
    return 0;
}

int sock_connect(struct sock *s, uint32_t addr, uint16_t port)
{
    int r;

    if (s->type == S_DGRAM) {
        s->raddr = addr;
        s->rport = port;
        s->connected = 1;
        return 0;
    }
    if (s->state != T_CLOSED)
        return -EISCONN;
    s->rx = kmalloc(RXSZ);
    if (!s->rx)
        return -ENOMEM;
    s->raddr = addr;
    s->rport = port;
    s->lport = port_alloc();
    s->snd_una = s->snd_nxt = (uint32_t)pit_ticks() * 64000u + 0x1000u;
    s->state = T_SYN;
    r = tcp_xmit(s, TF_SYN, NULL, 0, 1, 5, 100);
    if (r == 0 && s->state != T_EST)
        r = s->err ? -s->err : -ETIMEDOUT;
    if (r < 0) {
        s->state = T_CLOSED;
        s->err = 0;
    }
    return r;
}

long sock_send(struct sock *s, const void *buf, size_t n, uint32_t addr, uint16_t port, int hasdst)
{
    const uint8_t *p = buf;
    size_t done = 0;

    if (s->type == S_ICMP) {
        uint8_t m[1500];
        int r;

        if (!hasdst || n < 8 || n > sizeof(m))
            return -EINVAL;
        memcpy(m, buf, n);
        m[2] = m[3] = 0;
        *(uint16_t *)(m + 2) = csum_fin(csum_add(0, m, n));
        r = ip_send(P_ICMP, addr, m, n);
        return r < 0 ? r : (long)n;
    }
    if (s->type == S_DGRAM) {
        int r;

        if (!hasdst) {
            if (!s->connected)
                return -EDESTADDRREQ;
            addr = s->raddr;
            port = s->rport;
        }
        if (!s->lport)
            s->lport = port_alloc();
        r = udp_out(s->lport, port, addr, buf, n);
        return r < 0 ? r : (long)n;
    }
    if (s->state == T_RST)
        return -s->err;
    if (s->state != T_EST)
        return -ENOTCONN;
    while (done < n) {
        size_t c = n - done < MSS ? n - done : MSS;
        int r = tcp_xmit(s, TF_PSH | TF_ACK, p + done, c, 0, 6, 50);

        if (r < 0)
            return done ? (long)done : r;
        done += c;
    }
    return (long)done;
}

long sock_recv(struct sock *s, void *buf, size_t n, uint32_t *addr, uint16_t *port)
{
    int r;

    if (s->type == S_DGRAM || s->type == S_ICMP) {
        struct dgram *g;
        size_t c;

        WAIT(s->dq_n > 0, 300, r);
        if (r < 0)
            return r == -ETIMEDOUT ? -EAGAIN : r;
        g = s->dq[s->dq_h];
        s->dq_h = (s->dq_h + 1) % DQ;
        s->dq_n--;
        c = g->len < n ? g->len : n;
        memcpy(buf, g->data, c);
        if (addr)
            *addr = g->addr;
        if (port)
            *port = g->port;
        kfree(g);
        return (long)c;
    }
    if (s->state == T_CLOSED)
        return -ENOTCONN;
    WAIT(s->rx_n > 0 || s->eof || s->err, 6000, r);
    if (r < 0)
        return r;
    if (s->rx_n == 0)
        return s->err && !s->eof ? -s->err : 0;
    {
        uint8_t *o = buf;
        uint32_t c = s->rx_n < n ? s->rx_n : (uint32_t)n, k, before = s->rx_n;

        for (k = 0; k < c; k++)
            o[k] = s->rx[(s->rx_h + k) % RXSZ];
        s->rx_h = (s->rx_h + c) % RXSZ;
        s->rx_n -= c;
        if (s->state == T_EST && before > RXSZ - 2 * MSS)
            tcp_out(s, TF_ACK, NULL, 0, 0);
        return (long)c;
    }
}
