/*
 * msg_x.c -- MavericksLegacySupport libSystem shim.
 * Split verbatim from the former modern_api_polyfills.c; the shared DBG
 * helper and common includes live in mav_shim_debug.h.
 */
#include "mav_shim_debug.h"

/* ── recvmsg_x / sendmsg_x (10.10+): vectorized msghdr batch. Loop on recvmsg/sendmsg. */
struct msghdr_x {
    void *msg_name;
    uint32_t msg_namelen;
    struct iovec *msg_iov;
    int msg_iovlen;
    void *msg_control;
    uint32_t msg_controllen;
    int msg_flags;
    size_t msg_datalen;
};
ssize_t recvmsg_x(int s, struct msghdr_x *msgp, unsigned int cnt, int flags) {
    DBG("recvmsg_x fd=%d cnt=%u flags=0x%x", s, cnt, flags);
    ssize_t total = 0;
    for (unsigned int i = 0; i < cnt; i++) {
        struct msghdr mh = {0};
        mh.msg_name = msgp[i].msg_name;
        mh.msg_namelen = msgp[i].msg_namelen;
        mh.msg_iov = msgp[i].msg_iov;
        mh.msg_iovlen = msgp[i].msg_iovlen;
        mh.msg_control = msgp[i].msg_control;
        mh.msg_controllen = msgp[i].msg_controllen;
        mh.msg_flags = msgp[i].msg_flags;
        ssize_t n = recvmsg(s, &mh, flags);
        if (n < 0) return total ? (ssize_t)total : -1;
        msgp[i].msg_datalen = n;
        msgp[i].msg_namelen = mh.msg_namelen;
        msgp[i].msg_controllen = mh.msg_controllen;
        msgp[i].msg_flags = mh.msg_flags;
        total++;
        if (n == 0) break;
    }
    return total;
}
ssize_t sendmsg_x(int s, struct msghdr_x *msgp, unsigned int cnt, int flags) {
    DBG("sendmsg_x fd=%d cnt=%u flags=0x%x", s, cnt, flags);
    ssize_t total = 0;
    for (unsigned int i = 0; i < cnt; i++) {
        struct msghdr mh = {0};
        mh.msg_name = msgp[i].msg_name;
        mh.msg_namelen = msgp[i].msg_namelen;
        mh.msg_iov = msgp[i].msg_iov;
        mh.msg_iovlen = msgp[i].msg_iovlen;
        mh.msg_control = msgp[i].msg_control;
        mh.msg_controllen = msgp[i].msg_controllen;
        mh.msg_flags = msgp[i].msg_flags;
        ssize_t n = sendmsg(s, &mh, flags);
        if (n < 0) return total ? (ssize_t)total : -1;
        total++;
    }
    return total;
}

