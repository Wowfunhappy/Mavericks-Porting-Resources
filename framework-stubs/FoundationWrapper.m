// Wrapper for Foundation.framework: supplies constants this OS's copy lacks.
// Built with a re-export of the real framework, so it still provides the rest.
//
// Hand-written (not generated): the value is a documented NSString constant, so
// it can be reproduced exactly rather than stubbed to a no-op.
#import <Foundation/Foundation.h>

NSString * const NSFileProtectionComplete = @"NSFileProtectionComplete";
