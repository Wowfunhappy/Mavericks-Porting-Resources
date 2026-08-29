// Hand-written CoreSpotlight shim for OS X 10.9.
//
// CoreSpotlight (10.13+) does not exist here, but Spotlight itself does. Apps
// use CSSearchableIndex to publish items — channels, conversations, documents —
// so the user can find them from Spotlight. This implements that against what
// 10.9 actually has: indexable files.
//
// Each searchable item becomes a .webloc in ~/Library/Caches/Metadata/<app>/,
// the location apps have always used for Spotlight-visible metadata. The file
// name carries the item's title (Spotlight indexes file names), the Finder
// comment carries its description (indexed as kMDItemFinderComment), and the
// URL means activating a result opens the app rather than a text editor.
#import <Foundation/Foundation.h>
#import <sys/xattr.h>

// Unknown selectors must not abort the process.
#define EH_STUB_FORWARDING \
    + (NSMethodSignature *)methodSignatureForSelector:(SEL)sel { \
        NSMethodSignature *known = [super methodSignatureForSelector:sel]; \
        return known ?: [NSMethodSignature signatureWithObjCTypes:"@@:@@@@@@@@"]; } \
    + (void)forwardInvocation:(NSInvocation *)invocation { } \
    - (NSMethodSignature *)methodSignatureForSelector:(SEL)sel { \
        NSMethodSignature *known = [super methodSignatureForSelector:sel]; \
        return known ?: [NSMethodSignature signatureWithObjCTypes:"@@:@@@@@@@@"]; } \
    - (void)forwardInvocation:(NSInvocation *)invocation { } \
    /* A stub must tolerate key-value coding as well as selectors. Callers \
     * configure these classes with setValue:forKey:, and KVC does not go \
     * through forwardInvocation: -- it raises instead, which is fatal: \
     *   this class is not key value coding-compliant for the key clear */ \
    - (void)setValue:(id)value forUndefinedKey:(NSString *)key { (void)value; (void)key; } \
    - (id)valueForUndefinedKey:(NSString *)key { (void)key; return nil; } \
    + (void)setValue:(id)value forUndefinedKey:(NSString *)key { (void)value; (void)key; } \
    + (id)valueForUndefinedKey:(NSString *)key { (void)key; return nil; }

#pragma mark - Attribute set

@interface CSSearchableItemAttributeSet : NSObject
@property (nonatomic, copy) NSString *title;
@property (nonatomic, copy) NSString *displayName;
@property (nonatomic, copy) NSString *contentDescription;
@property (nonatomic, copy) NSString *textContent;
@property (nonatomic, copy) NSArray *keywords;
@property (nonatomic, retain) NSURL *contentURL;
@property (nonatomic, retain) NSURL *thumbnailURL;
@property (nonatomic, retain) NSData *thumbnailData;
@property (nonatomic, copy) NSString *identifier;
@property (nonatomic, copy) NSString *relatedUniqueIdentifier;
- (id)initWithItemContentType:(NSString *)contentType;
@end

@implementation CSSearchableItemAttributeSet
EH_STUB_FORWARDING
- (id)initWithItemContentType:(NSString *)contentType { return [super init]; }
- (void)dealloc {
    [_title release]; [_displayName release]; [_contentDescription release];
    [_textContent release]; [_keywords release]; [_contentURL release];
    [_thumbnailURL release]; [_thumbnailData release]; [_identifier release];
    [_relatedUniqueIdentifier release];
    [super dealloc];
}
@end

#pragma mark - Searchable item

@interface CSSearchableItem : NSObject
@property (nonatomic, copy) NSString *uniqueIdentifier;
@property (nonatomic, copy) NSString *domainIdentifier;
@property (nonatomic, retain) NSDate *expirationDate;
@property (nonatomic, retain) CSSearchableItemAttributeSet *attributeSet;
- (id)initWithUniqueIdentifier:(NSString *)uniqueIdentifier
              domainIdentifier:(NSString *)domainIdentifier
                  attributeSet:(CSSearchableItemAttributeSet *)attributeSet;
@end

@implementation CSSearchableItem
EH_STUB_FORWARDING
- (id)initWithUniqueIdentifier:(NSString *)uniqueIdentifier
              domainIdentifier:(NSString *)domainIdentifier
                  attributeSet:(CSSearchableItemAttributeSet *)attributeSet {
    if (!(self = [super init])) return nil;
    self.uniqueIdentifier = uniqueIdentifier;
    self.domainIdentifier = domainIdentifier;
    self.attributeSet = attributeSet;
    return self;
}
- (void)dealloc {
    [_uniqueIdentifier release]; [_domainIdentifier release];
    [_expirationDate release]; [_attributeSet release];
    [super dealloc];
}
@end

#pragma mark - Index

@interface CSSearchableIndex : NSObject
+ (BOOL)isIndexingAvailable;
+ (instancetype)defaultSearchableIndex;
+ (instancetype)searchableIndexWithName:(NSString *)name;
- (void)indexSearchableItems:(NSArray *)items
           completionHandler:(void (^)(NSError *error))completionHandler;
- (void)deleteSearchableItemsWithIdentifiers:(NSArray *)identifiers
                           completionHandler:(void (^)(NSError *error))completionHandler;
- (void)deleteSearchableItemsWithDomainIdentifiers:(NSArray *)domainIdentifiers
                                 completionHandler:(void (^)(NSError *error))completionHandler;
- (void)deleteAllSearchableItemsWithCompletionHandler:(void (^)(NSError *error))completionHandler;
@end

static NSString *EHSpotlightDirectory(void) {
    NSString *appName = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleName"];
    if (![appName length]) appName = @"Application";
    NSString *base = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Caches/Metadata"];
    NSString *directory = [base stringByAppendingPathComponent:appName];
    [[NSFileManager defaultManager] createDirectoryAtPath:directory
                              withIntermediateDirectories:YES attributes:nil error:NULL];
    return directory;
}

// The identifier decides the file, so re-indexing an item replaces it rather
// than accumulating duplicates. The title is kept in the name because Spotlight
// indexes file names for every file type.
static NSString *EHFileNameForItem(CSSearchableItem *item) {
    NSString *title = [[item attributeSet] title];
    if (![title length]) title = [[item attributeSet] displayName];
    if (![title length]) title = [item uniqueIdentifier];
    NSMutableString *safe = [NSMutableString string];
    for (NSUInteger i = 0; i < [title length] && [safe length] < 60; i++) {
        unichar c = [title characterAtIndex:i];
        unichar safeChar = (c == '/' || c == ':' || c == 0) ? (unichar)'-' : c;
        [safe appendString:[NSString stringWithCharacters:&safeChar length:1]];
    }
    if (![safe length]) [safe appendString:@"item"];
    return [NSString stringWithFormat:@"%@ (%lx).webloc",
            safe, (unsigned long)[[item uniqueIdentifier] hash]];
}

// Finder comments are stored as a plist in this extended attribute, and
// Spotlight indexes them as kMDItemFinderComment.
static void EHSetFinderComment(NSString *path, NSString *comment) {
    if (![comment length]) return;
    NSData *plist = [NSPropertyListSerialization dataWithPropertyList:comment
        format:NSPropertyListBinaryFormat_v1_0 options:0 error:NULL];
    if (!plist) return;
    setxattr([path fileSystemRepresentation], "com.apple.metadata:kMDItemFinderComment",
             [plist bytes], [plist length], 0, 0);
}

@implementation CSSearchableIndex

EH_STUB_FORWARDING

+ (BOOL)isIndexingAvailable { return YES; }

+ (instancetype)defaultSearchableIndex {
    static CSSearchableIndex *shared = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ shared = [[CSSearchableIndex alloc] init]; });
    return shared;
}

+ (instancetype)searchableIndexWithName:(NSString *)name { return [self defaultSearchableIndex]; }

- (void)indexSearchableItems:(NSArray *)items
           completionHandler:(void (^)(NSError *error))completionHandler {
    NSString *directory = EHSpotlightDirectory();
    for (CSSearchableItem *item in items) {
        if (![item isKindOfClass:[CSSearchableItem class]]) continue;
        CSSearchableItemAttributeSet *attributes = [item attributeSet];
        NSURL *url = [attributes contentURL];
        if (!url) continue;   // nothing to open; not worth a file

        NSString *path = [directory stringByAppendingPathComponent:EHFileNameForItem(item)];
        NSDictionary *webloc = @{@"URL": [url absoluteString] ?: @""};
        NSData *contents = [NSPropertyListSerialization dataWithPropertyList:webloc
            format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
        if (!contents) continue;
        [contents writeToFile:path atomically:YES];

        NSMutableArray *comment = [NSMutableArray array];
        if ([[attributes contentDescription] length]) [comment addObject:[attributes contentDescription]];
        if ([[attributes textContent] length]) [comment addObject:[attributes textContent]];
        if ([[attributes keywords] count]) [comment addObject:[[attributes keywords] componentsJoinedByString:@" "]];
        EHSetFinderComment(path, [comment componentsJoinedByString:@"\n"]);

        // Index immediately rather than waiting for the next sweep.
        [NSTask launchedTaskWithLaunchPath:@"/usr/bin/mdimport" arguments:@[path]];
    }
    if (completionHandler) completionHandler(nil);
}

- (void)removeFilesMatching:(BOOL (^)(NSString *name))predicate {
    NSString *directory = EHSpotlightDirectory();
    NSFileManager *manager = [NSFileManager defaultManager];
    for (NSString *name in [manager contentsOfDirectoryAtPath:directory error:NULL]) {
        if (predicate && !predicate(name)) continue;
        [manager removeItemAtPath:[directory stringByAppendingPathComponent:name] error:NULL];
    }
}

- (void)deleteSearchableItemsWithIdentifiers:(NSArray *)identifiers
                           completionHandler:(void (^)(NSError *error))completionHandler {
    NSMutableSet *suffixes = [NSMutableSet set];
    for (NSString *identifier in identifiers)
        [suffixes addObject:[NSString stringWithFormat:@"(%lx).webloc", (unsigned long)[identifier hash]]];
    [self removeFilesMatching:^BOOL(NSString *name) {
        for (NSString *suffix in suffixes) if ([name hasSuffix:suffix]) return YES;
        return NO;
    }];
    if (completionHandler) completionHandler(nil);
}

- (void)deleteSearchableItemsWithDomainIdentifiers:(NSArray *)domainIdentifiers
                                 completionHandler:(void (^)(NSError *error))completionHandler {
    // Domains are not recorded per file; clearing everything is the safe reading.
    [self removeFilesMatching:nil];
    if (completionHandler) completionHandler(nil);
}

- (void)deleteAllSearchableItemsWithCompletionHandler:(void (^)(NSError *error))completionHandler {
    [self removeFilesMatching:nil];
    if (completionHandler) completionHandler(nil);
}

@end
