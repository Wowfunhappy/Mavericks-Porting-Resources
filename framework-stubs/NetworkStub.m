// Hand-written stub Network.framework (nw_path_monitor) for OS X 10.9.
//
// Unlike Metal, the capability here does exist on 10.9 -- "is there a usable
// route right now" is what SystemConfiguration's reachability API answers. So
// this shim implements nw_path_monitor for real against SCNetworkReachability
// rather than reporting a fixed answer. A monitor that never fired would be
// worse than absent: callers gate work on the first path update and would
// simply hang.
//
// Only the nw_path_monitor subset that real binaries bind is implemented.
// Anything else from Network.framework is deliberately absent.
#import <Foundation/Foundation.h>
#import <SystemConfiguration/SystemConfiguration.h>
#include <netinet/in.h>

// Mirrors the real enums, whose numeric values are ABI.
typedef enum { nw_path_status_invalid = 0, nw_path_status_satisfied = 1,
               nw_path_status_unsatisfied = 2, nw_path_status_satisfiable = 3 } nw_path_status_t;
typedef enum { nw_interface_type_other = 0, nw_interface_type_wifi = 1,
               nw_interface_type_cellular = 2, nw_interface_type_wired = 3,
               nw_interface_type_loopback = 4 } nw_interface_type_t;

@interface EHPath : NSObject
@property (nonatomic) nw_path_status_t status;
@property (nonatomic) BOOL wired;
@end
@implementation EHPath
@end

@interface EHPathMonitor : NSObject
@property (nonatomic, strong) id updateHandler;
@property (nonatomic) dispatch_queue_t queue;
@property (nonatomic) BOOL cancelled;
@end
@implementation EHPathMonitor
@end

// Reachability of the default route: a zero address means "anywhere", which is
// what a path monitor without a specific endpoint is asking about.
static EHPath *EHCurrentPath(void) {
    EHPath *p = [[EHPath alloc] init];
    p.status = nw_path_status_unsatisfied;
    p.wired = NO;
    struct sockaddr_in zero;
    memset(&zero, 0, sizeof zero);
    zero.sin_len = sizeof zero;
    zero.sin_family = AF_INET;
    SCNetworkReachabilityRef r =
        SCNetworkReachabilityCreateWithAddress(NULL, (const struct sockaddr *)&zero);
    if (r) {
        SCNetworkReachabilityFlags f = 0;
        if (SCNetworkReachabilityGetFlags(r, &f)) {
            BOOL reachable = (f & kSCNetworkReachabilityFlagsReachable) != 0;
            BOOL needsConnection = (f & kSCNetworkReachabilityFlagsConnectionRequired) != 0;
            if (reachable && !needsConnection) p.status = nw_path_status_satisfied;
            // 10.9 has no cellular; anything reachable is wifi or ethernet, and
            // callers overwhelmingly branch on "is this expensive", so report
            // wired, the not-expensive answer.
            p.wired = (p.status == nw_path_status_satisfied);
        }
        CFRelease(r);
    }
    return p;
}

void *nw_path_monitor_create(void) {
    return (void *)CFBridgingRetain([[EHPathMonitor alloc] init]);
}

void nw_path_monitor_set_queue(void *monitor, dispatch_queue_t queue) {
    EHPathMonitor *m = (__bridge EHPathMonitor *)monitor;
    if (m) m.queue = queue;
}

void nw_path_monitor_set_update_handler(void *monitor, void (^handler)(void *)) {
    EHPathMonitor *m = (__bridge EHPathMonitor *)monitor;
    if (m) m.updateHandler = [handler copy];
}

// The real monitor delivers an initial update as soon as it is started, then
// again on every change. Without change notifications this delivers the
// initial update only, which is what unblocks a waiting caller.
void nw_path_monitor_start(void *monitor) {
    EHPathMonitor *m = (__bridge EHPathMonitor *)monitor;
    if (!m || !m.updateHandler) return;
    void (^handler)(void *) = m.updateHandler;
    dispatch_queue_t q = m.queue ?: dispatch_get_main_queue();
    dispatch_async(q, ^{
        if (m.cancelled) return;
        EHPath *p = EHCurrentPath();
        handler((void *)CFBridgingRetain(p));
    });
}

void nw_path_monitor_cancel(void *monitor) {
    EHPathMonitor *m = (__bridge EHPathMonitor *)monitor;
    if (m) m.cancelled = YES;
}

nw_path_status_t nw_path_get_status(void *path) {
    EHPath *p = (__bridge EHPath *)path;
    return p ? p.status : nw_path_status_invalid;
}

bool nw_path_uses_interface_type(void *path, nw_interface_type_t type) {
    EHPath *p = (__bridge EHPath *)path;
    if (!p || p.status != nw_path_status_satisfied) return false;
    return type == (p.wired ? nw_interface_type_wired : nw_interface_type_wifi);
}
