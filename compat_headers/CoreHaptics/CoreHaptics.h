/* CoreHaptics stub for macOS < 10.15 */
#ifndef _COMPAT_COREHAPTICS_H_
#define _COMPAT_COREHAPTICS_H_
#import <Foundation/Foundation.h>
@class CHHapticEngine;
@class CHHapticPattern;
@class CHHapticEvent;
@class CHHapticEventParameter;
@class CHHapticDynamicParameter;
extern NSString *CHHapticEventTypeHapticContinuous;
extern NSString *CHHapticEventParameterIDHapticIntensity;
extern NSString *CHHapticDynamicParameterIDHapticIntensityControl;
#endif
