/**
 *  OSM
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */
#include "appnap.h"
#import <Foundation/Foundation.h>

void disableAppNap()
{
    static id activity = nil;
    if (activity) {
        return;
    }
    NSActivityOptions options = NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical;
    activity = [[NSProcessInfo processInfo] beginActivityWithOptions: options
                                                              reason: @"Real-time audio measurement"];
    [activity retain];
}
