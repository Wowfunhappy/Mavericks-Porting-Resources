#include <dns_sd.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/select.h>

extern const unsigned char kDNSServiceAttrAllowFailover;

extern DNSServiceErrorType DNSServiceGetAddrInfoEx(
    DNSServiceRef *sdRef,
    DNSServiceFlags flags,
    uint32_t interfaceIndex,
    DNSServiceProtocol protocol,
    const char *hostname,
    const void *attribute,
    DNSServiceGetAddrInfoReply callBack,
    void *context);

static void reply(DNSServiceRef sdRef, DNSServiceFlags flags,
                  uint32_t interfaceIndex, DNSServiceErrorType errorCode,
                  const char *hostname, const struct sockaddr *address,
                  uint32_t ttl, void *context)
{
    (void)sdRef;
    (void)flags;
    (void)interfaceIndex;
    (void)hostname;
    (void)ttl;
    if (*(int *)context == 0)
        *(int *)context = (errorCode == kDNSServiceErr_NoError && address) ? 1 : -1;
}

int main(void)
{
    DNSServiceRef ref = NULL;
    int callback_result = 0;
    DNSServiceErrorType err = DNSServiceGetAddrInfoEx(
        &ref, 0, 0, kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6,
        "localhost", &kDNSServiceAttrAllowFailover, reply, &callback_result);

    if (err != kDNSServiceErr_NoError) {
        fprintf(stderr, "DNSServiceGetAddrInfoEx returned %d\n", (int)err);
        return 1;
    }
    if (!ref) {
        fputs("DNSServiceGetAddrInfoEx returned no service ref\n", stderr);
        return 1;
    }

    {
        int fd = DNSServiceRefSockFD(ref);
        fd_set readfds;
        struct timeval timeout = { 5, 0 };
        int ready;

        if (fd < 0) {
            fputs("DNSServiceRefSockFD returned no socket\n", stderr);
            DNSServiceRefDeallocate(ref);
            return 1;
        }
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        ready = select(fd + 1, &readfds, NULL, NULL, &timeout);
        if (ready <= 0 || !FD_ISSET(fd, &readfds)) {
            fputs("timed out waiting for localhost DNS reply\n", stderr);
            DNSServiceRefDeallocate(ref);
            return 1;
        }
    }

    err = DNSServiceProcessResult(ref);
    DNSServiceRefDeallocate(ref);
    if (err != kDNSServiceErr_NoError || callback_result != 1) {
        fprintf(stderr, "localhost DNS callback failed (process=%d, callback=%d)\n",
                (int)err, callback_result);
        return 1;
    }
    puts("DNSServiceGetAddrInfoEx fallback: PASS");
    return 0;
}
