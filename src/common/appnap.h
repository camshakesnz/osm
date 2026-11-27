/**
 *  OSM
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */
#ifndef COMMON_APPNAP_H
#define COMMON_APPNAP_H

/**
 * macOS: keep the app out of App Nap and timer throttling while it runs.
 * Without this, macOS slows the measurement timers down whenever the window
 * is in the background or covered, so meters lag and measurements stall.
 */
void disableAppNap();

#endif // COMMON_APPNAP_H
