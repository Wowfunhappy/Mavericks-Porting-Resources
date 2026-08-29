// Wrapper for WebKit.framework on OS X 10.9: supplies the WebKit 2 classes
// this OS's copy lacks, and re-exports the real framework so everything else
// still resolves.
//
// Hand-written. WKWebView (10.10) is not stubbed here but implemented, over
// the WebKit 1 engine that 10.9 does have. A stub would load and then draw
// nothing, which for an app whose sign-in and home screens are web content
// means a running application with a blank window -- the failure looks like a
// rendering bug and gives no clue where it comes from.
//
// The engine underneath is 10.9's, so what renders is whatever that engine can
// render. This bridges the API, not the passage of time.
//
// Legacy WebKit's headers are not installed on 10.9, so the parts of it used
// here are declared below rather than imported.
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#import <objc/message.h>
#import <stdlib.h>

@class WebView, WebFrame, WebScriptObject, WKWebView, WKWebViewConfiguration;

@interface WebFrame : NSObject
- (void)loadRequest:(NSURLRequest *)request;
- (void)loadHTMLString:(NSString *)string baseURL:(NSURL *)baseURL;
- (WebScriptObject *)windowObject;
@end

@interface WebView : NSView
- (id)initWithFrame:(NSRect)frame frameName:(NSString *)frameName groupName:(NSString *)groupName;
- (void)setFrameLoadDelegate:(id)delegate;
- (void)setPolicyDelegate:(id)delegate;
- (void)setUIDelegate:(id)delegate;
- (WebFrame *)mainFrame;
- (NSString *)stringByEvaluatingJavaScriptFromString:(NSString *)script;
- (void)setCustomUserAgent:(NSString *)userAgent;
- (NSString *)customUserAgent;
- (NSString *)mainFrameTitle;
- (NSString *)mainFrameURL;
- (BOOL)canGoBack;
- (BOOL)canGoForward;
- (BOOL)goBack;
- (BOOL)goForward;
- (void)reload:(id)sender;
- (void)stopLoading:(id)sender;
- (void)setDrawsBackground:(BOOL)draws;
- (WebScriptObject *)windowScriptObject;
@end

@interface WebScriptObject : NSObject
- (id)evaluateWebScript:(NSString *)script;
- (void)setValue:(id)value forKey:(NSString *)key;
@end

// ---- the WebKit 2 surface ----

@interface WKUserScript : NSObject
@property (nonatomic, readonly, copy) NSString *source;
@property (nonatomic, readonly) NSInteger injectionTime;
@property (nonatomic, readonly, getter=isForMainFrameOnly) BOOL forMainFrameOnly;
- (instancetype)initWithSource:(NSString *)source
                 injectionTime:(NSInteger)injectionTime
              forMainFrameOnly:(BOOL)forMainFrameOnly;
@end

@implementation WKUserScript
@synthesize source = _source, injectionTime = _injectionTime, forMainFrameOnly = _forMainFrameOnly;
- (instancetype)initWithSource:(NSString *)source
                 injectionTime:(NSInteger)injectionTime
              forMainFrameOnly:(BOOL)forMainFrameOnly {
    if ((self = [super init])) {
        _source = [source copy];
        _injectionTime = injectionTime;
        _forMainFrameOnly = forMainFrameOnly;
    }
    return self;
}
@end

@interface WKScriptMessage : NSObject
@property (nonatomic, strong) id body;
@property (nonatomic, copy) NSString *name;
@property (nonatomic, weak) WKWebView *webView;
@end
@implementation WKScriptMessage
@end

@interface WKNavigation : NSObject @end
@implementation WKNavigation @end

@interface WKNavigationAction : NSObject
@property (nonatomic, strong) NSURLRequest *request;
@property (nonatomic) NSInteger navigationType;
@end
@implementation WKNavigationAction
@end

@interface WKUserContentController : NSObject
@property (nonatomic, readonly, copy) NSArray *userScripts;
- (void)addScriptMessageHandler:(id)handler name:(NSString *)name;
- (void)removeScriptMessageHandlerForName:(NSString *)name;
- (void)addUserScript:(WKUserScript *)script;
- (void)removeAllUserScripts;
@end

@implementation WKUserContentController {
    NSMutableDictionary *_handlers;
    NSMutableArray *_scripts;
}
- (instancetype)init {
    if ((self = [super init])) {
        _handlers = [NSMutableDictionary dictionary];
        _scripts = [NSMutableArray array];
    }
    return self;
}
- (NSArray *)userScripts { return [_scripts copy]; }
- (NSDictionary *)eh_handlers { return _handlers; }
- (void)addScriptMessageHandler:(id)handler name:(NSString *)name {
    if (handler && name) _handlers[name] = handler;
}
- (void)removeScriptMessageHandlerForName:(NSString *)name {
    if (name) [_handlers removeObjectForKey:name];
}
- (void)addUserScript:(WKUserScript *)script { if (script) [_scripts addObject:script]; }
- (void)removeAllUserScripts { [_scripts removeAllObjects]; }
@end

@interface WKPreferences : NSObject
@property (nonatomic) BOOL javaScriptEnabled;
@property (nonatomic) BOOL javaScriptCanOpenWindowsAutomatically;
@property (nonatomic) CGFloat minimumFontSize;
@end
@implementation WKPreferences
@end

@interface WKProcessPool : NSObject @end
@implementation WKProcessPool @end

@interface WKWebsiteDataStore : NSObject
+ (instancetype)defaultDataStore;
+ (instancetype)nonPersistentDataStore;
+ (NSSet *)allWebsiteDataTypes;
- (void)removeDataOfTypes:(NSSet *)types
            modifiedSince:(NSDate *)date
        completionHandler:(void (^)(void))completionHandler;
@end
@implementation WKWebsiteDataStore
+ (instancetype)defaultDataStore {
    static WKWebsiteDataStore *shared;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ shared = [[WKWebsiteDataStore alloc] init]; });
    return shared;
}
+ (instancetype)nonPersistentDataStore { return [[WKWebsiteDataStore alloc] init]; }
+ (NSSet *)allWebsiteDataTypes { return [NSSet set]; }
// 10.9 has no per-type website data API; the caller's completion must still run
// or it waits forever.
- (void)removeDataOfTypes:(NSSet *)types
            modifiedSince:(NSDate *)date
        completionHandler:(void (^)(void))completionHandler {
    (void)types; (void)date;
    if (completionHandler) dispatch_async(dispatch_get_main_queue(), completionHandler);
}
@end

@interface WKWebViewConfiguration : NSObject
@property (nonatomic, strong) WKUserContentController *userContentController;
@property (nonatomic, strong) WKPreferences *preferences;
@property (nonatomic, strong) WKProcessPool *processPool;
@property (nonatomic, strong) WKWebsiteDataStore *websiteDataStore;
@property (nonatomic, copy) NSString *applicationNameForUserAgent;
@end
@implementation WKWebViewConfiguration
- (instancetype)init {
    if ((self = [super init])) {
        _userContentController = [[WKUserContentController alloc] init];
        _preferences = [[WKPreferences alloc] init];
        _processPool = [[WKProcessPool alloc] init];
        _websiteDataStore = [WKWebsiteDataStore defaultDataStore];
    }
    return self;
}
@end

@interface WKSnapshotConfiguration : NSObject @end
@implementation WKSnapshotConfiguration @end
@interface WKContentRuleListStore : NSObject @end
@implementation WKContentRuleListStore @end
@interface _WKWebContentProcessInfo : NSObject @end
@implementation _WKWebContentProcessInfo @end

// The delegate callbacks this bridge makes. Declared here only so the
// compiler knows the selectors; a real delegate conforms to WebKit's own
// protocols, and conformance is never checked -- every call is guarded by
// -respondsToSelector:.
@protocol EHNavigationDelegate <NSObject>
@optional
- (void)webView:(WKWebView *)webView didStartProvisionalNavigation:(WKNavigation *)navigation;
- (void)webView:(WKWebView *)webView didFinishNavigation:(WKNavigation *)navigation;
- (void)webView:(WKWebView *)webView didFailNavigation:(WKNavigation *)navigation withError:(NSError *)error;
- (void)webView:(WKWebView *)webView didFailProvisionalNavigation:(WKNavigation *)navigation withError:(NSError *)error;
- (void)webView:(WKWebView *)webView decidePolicyForNavigationAction:(WKNavigationAction *)action
                                                    decisionHandler:(void (^)(NSInteger))decisionHandler;
@end

@protocol EHScriptMessageHandler <NSObject>
- (void)userContentController:(WKUserContentController *)controller
      didReceiveScriptMessage:(WKScriptMessage *)message;
@end

// ---- WKWebView ----

@interface WKWebView : NSView
@property (nonatomic, weak) id<EHNavigationDelegate> navigationDelegate;
@property (nonatomic, weak) id UIDelegate;
@property (nonatomic, readonly, strong) WKWebViewConfiguration *configuration;
@property (nonatomic, copy) NSString *customUserAgent;
@property (nonatomic) BOOL allowsBackForwardNavigationGestures;
- (instancetype)initWithFrame:(NSRect)frame configuration:(WKWebViewConfiguration *)configuration;
@end

@implementation WKWebView {
    WebView *_engine;
}
@synthesize navigationDelegate = _navigationDelegate, UIDelegate = _UIDelegate,
            configuration = _configuration,
            allowsBackForwardNavigationGestures = _allowsBackForwardNavigationGestures;

// Set EH_WEBKIT_DEBUG=1 to trace what the bridge is asked to do.
static int eh_webkit_debug(void) {
    static int on = -1;
    if (on < 0) { const char *v = getenv("EH_WEBKIT_DEBUG"); on = (v && *v == '1'); }
    return on;
}

- (instancetype)initWithFrame:(NSRect)frame configuration:(WKWebViewConfiguration *)configuration {
    if (eh_webkit_debug()) NSLog(@"[webkit] WKWebView created %@", NSStringFromRect(frame));
    if ((self = [super initWithFrame:frame])) {
        _configuration = configuration ?: [[WKWebViewConfiguration alloc] init];
        Class engineClass = NSClassFromString(@"WebView");
        if (engineClass) {
            _engine = [[engineClass alloc] initWithFrame:[self bounds]
                                               frameName:nil
                                               groupName:nil];
            [_engine setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
            [_engine setFrameLoadDelegate:self];
            [_engine setPolicyDelegate:self];
            [_engine setUIDelegate:self];
            [self addSubview:_engine];
        }
    }
    return self;
}

- (instancetype)initWithFrame:(NSRect)frame {
    return [self initWithFrame:frame configuration:nil];
}

- (NSString *)customUserAgent { return [_engine customUserAgent]; }
- (void)setCustomUserAgent:(NSString *)agent { [_engine setCustomUserAgent:agent]; }

- (void)loadRequest:(NSURLRequest *)request {
    if (eh_webkit_debug()) NSLog(@"[webkit] loadRequest %@ (engine=%@)", [request URL], _engine ? @"yes" : @"MISSING");
    [[_engine mainFrame] loadRequest:request];
}
- (void)loadHTMLString:(NSString *)string baseURL:(NSURL *)baseURL {
    [[_engine mainFrame] loadHTMLString:string baseURL:baseURL];
}
// 10.9's engine has no sandboxed read-access concept; the extra URL is the
// permission grant, and loading the file is the whole operation.
- (void)loadFileURL:(NSURL *)url allowingReadAccessToURL:(NSURL *)readAccessURL {
    (void)readAccessURL;
    if (url) [[_engine mainFrame] loadRequest:[NSURLRequest requestWithURL:url]];
}

- (void)evaluateJavaScript:(NSString *)script completionHandler:(void (^)(id, NSError *))handler {
    NSString *result = script ? [_engine stringByEvaluatingJavaScriptFromString:script] : nil;
    if (handler) handler(result, nil);
}

- (NSString *)title { return [_engine mainFrameTitle]; }
- (NSURL *)URL {
    NSString *u = [_engine mainFrameURL];
    return u.length ? [NSURL URLWithString:u] : nil;
}
- (BOOL)canGoBack { return [_engine canGoBack]; }
- (BOOL)canGoForward { return [_engine canGoForward]; }
- (id)goBack { [_engine goBack]; return nil; }
- (id)goForward { [_engine goForward]; return nil; }
- (id)reload { [_engine reload:nil]; return nil; }
- (void)stopLoading { [_engine stopLoading:nil]; }
- (BOOL)isLoading { return NO; }
- (double)estimatedProgress { return 1.0; }

// ---- bridging the engine's callbacks to WKNavigationDelegate ----

- (void)webView:(WebView *)sender didStartProvisionalLoadForFrame:(WebFrame *)frame {
    if ([_navigationDelegate respondsToSelector:@selector(webView:didStartProvisionalNavigation:)])
        [_navigationDelegate webView:self didStartProvisionalNavigation:nil];
}

- (void)webView:(WebView *)sender didFinishLoadForFrame:(WebFrame *)frame {
    if (eh_webkit_debug()) NSLog(@"[webkit] didFinishLoad %@", [sender mainFrameURL]);
    if (frame != [sender mainFrame]) return;
    [self eh_injectUserScriptsForTime:1];
    if ([_navigationDelegate respondsToSelector:@selector(webView:didFinishNavigation:)])
        [_navigationDelegate webView:self didFinishNavigation:nil];
}

- (void)webView:(WebView *)sender didFailLoadWithError:(NSError *)error forFrame:(WebFrame *)frame {
    if ([_navigationDelegate respondsToSelector:@selector(webView:didFailNavigation:withError:)])
        [_navigationDelegate webView:self didFailNavigation:nil withError:error];
}

- (void)webView:(WebView *)sender didFailProvisionalLoadWithError:(NSError *)error forFrame:(WebFrame *)frame {
    if (eh_webkit_debug()) NSLog(@"[webkit] provisional load FAILED: %@", error);
    if ([_navigationDelegate respondsToSelector:@selector(webView:didFailProvisionalNavigation:withError:)])
        [_navigationDelegate webView:self didFailProvisionalNavigation:nil withError:error];
}

// Every new window object is a fresh JavaScript world: the message-handler
// bridge and any start-of-document user scripts have to be reinstalled.
- (void)webView:(WebView *)sender didClearWindowObject:(WebScriptObject *)window forFrame:(WebFrame *)frame {
    [self eh_installMessageBridge];
    [self eh_injectUserScriptsForTime:0];
}

- (void)webView:(WebView *)sender
    decidePolicyForNavigationAction:(NSDictionary *)info
                            request:(NSURLRequest *)request
                              frame:(WebFrame *)frame
                   decisionListener:(id)listener {
    if (![_navigationDelegate respondsToSelector:
            @selector(webView:decidePolicyForNavigationAction:decisionHandler:)]) {
        ((void (*)(id, SEL))objc_msgSend)(listener, sel_registerName("use"));
        return;
    }
    WKNavigationAction *action = [[WKNavigationAction alloc] init];
    action.request = request;
    __block BOOL decided = NO;
    void (^decisionHandler)(NSInteger) = ^(NSInteger policy) {
        if (decided) return;
        decided = YES;
        // WKNavigationActionPolicyCancel == 0, Allow == 1.
        ((void (*)(id, SEL))objc_msgSend)(listener,
            sel_registerName(policy == 0 ? "ignore" : "use"));
    };
    ((void (*)(id, SEL, id, id, id))objc_msgSend)(
        _navigationDelegate,
        @selector(webView:decidePolicyForNavigationAction:decisionHandler:),
        self, action, decisionHandler);
    if (!decided) ((void (*)(id, SEL))objc_msgSend)(listener, sel_registerName("use"));
}

// ---- script message handlers ----
//
// WKWebView delivers messages through window.webkit.messageHandlers.<name>.
// Legacy WebKit has no such thing, but it can expose an Objective-C object to
// JavaScript, so the shape is rebuilt on top of that: one bridge object, and a
// generated stub per handler name that forwards to it.

- (void)eh_installMessageBridge {
    NSDictionary *handlers = [_configuration.userContentController eh_handlers];
    if (!handlers.count) return;
    WebScriptObject *window = [_engine windowScriptObject];
    if (!window) return;
    [window setValue:self forKey:@"__ehNativeBridge"];

    NSMutableString *js = [NSMutableString stringWithString:
        @"window.webkit = window.webkit || {};"
         "window.webkit.messageHandlers = window.webkit.messageHandlers || {};"];
    for (NSString *name in handlers) {
        [js appendFormat:
            @"window.webkit.messageHandlers['%@'] = { postMessage: function(m) {"
             "  try { m = JSON.stringify(m); } catch (e) { m = String(m); }"
             "  window.__ehNativeBridge.post_message(m, '%@'); } };", name, name];
    }
    [_engine stringByEvaluatingJavaScriptFromString:js];
}

// WebScriptObject will not expose a selector unless the class opts in.
+ (BOOL)isSelectorExcludedFromWebScript:(SEL)selector {
    return selector != @selector(post_message:name:);
}
+ (NSString *)webScriptNameForSelector:(SEL)selector {
    if (selector == @selector(post_message:name:)) return @"post_message";
    return nil;
}

- (void)post_message:(NSString *)json name:(NSString *)name {
    id<EHScriptMessageHandler> handler = [_configuration.userContentController eh_handlers][name];
    if (!handler) return;
    id body = json;
    NSData *data = [json dataUsingEncoding:NSUTF8StringEncoding];
    if (data) {
        NSError *error = nil;
        id decoded = [NSJSONSerialization JSONObjectWithData:data
                                                     options:NSJSONReadingAllowFragments
                                                       error:&error];
        if (decoded) body = decoded;
    }
    WKScriptMessage *message = [[WKScriptMessage alloc] init];
    message.body = body;
    message.name = name;
    message.webView = self;
    if ([handler respondsToSelector:@selector(userContentController:didReceiveScriptMessage:)])
        [handler userContentController:_configuration.userContentController
              didReceiveScriptMessage:message];
}

- (void)eh_injectUserScriptsForTime:(NSInteger)time {
    for (WKUserScript *script in _configuration.userContentController.userScripts) {
        if (script.injectionTime != time) continue;
        if (script.source) [_engine stringByEvaluatingJavaScriptFromString:script.source];
    }
}

@end

// Data-type constants. The values are the documented ones; callers put them in
// sets and compare them.
NSString * const WKWebsiteDataTypeCookies = @"WKWebsiteDataTypeCookies";
NSString * const WKWebsiteDataTypeLocalStorage = @"WKWebsiteDataTypeLocalStorage";
NSString * const WKWebsiteDataTypeSessionStorage = @"WKWebsiteDataTypeSessionStorage";
NSString * const WKWebsiteDataTypeIndexedDBDatabases = @"WKWebsiteDataTypeIndexedDBDatabases";
NSString * const WKWebsiteDataTypeWebSQLDatabases = @"WKWebsiteDataTypeWebSQLDatabases";
NSString * const WKWebsiteDataTypeServiceWorkerRegistrations = @"WKWebsiteDataTypeServiceWorkerRegistrations";
NSString * const WKWebsiteDataTypeFetchCache = @"WKWebsiteDataTypeFetchCache";
NSString * const WKWebsiteDataTypeFileSystem = @"WKWebsiteDataTypeFileSystem";
NSString * const WKWebsiteDataTypeHashSalt = @"WKWebsiteDataTypeHashSalt";
NSString * const WKWebsiteDataTypeMediaKeys = @"WKWebsiteDataTypeMediaKeys";
NSString * const WKWebsiteDataTypeSearchFieldRecentSearches = @"WKWebsiteDataTypeSearchFieldRecentSearches";
