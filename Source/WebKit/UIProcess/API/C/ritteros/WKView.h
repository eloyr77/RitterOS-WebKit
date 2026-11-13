/*
 * Copyright (C) 2026 Compass, the web browser for RitterOS.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS BE LIABLE
 * FOR ANY DAMAGES ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE.
 */

// RitterOS's web view (the C API): a page drawn into the embedder's pixels,
// fed with its input. With WKContext, WKPage and WKPreferences (WebKit's C
// API) it is all an embedder (Compass) uses. Call it on the main thread
// (WKRunLoop.h).

#pragma once

#include <WebKit/WKBase.h>
#include <WebKit/WKGeometry.h>
#include <WebKit/WKViewClient.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

WK_EXPORT WKViewRef WKViewCreate(WKPageConfigurationRef);
WK_EXPORT void WKViewSetViewClient(WKViewRef, const WKViewClientBase*);
WK_EXPORT WKPageRef WKViewGetPage(WKViewRef);

WK_EXPORT void WKViewSetSize(WKViewRef, WKSize);
WK_EXPORT void WKViewSetFocus(WKViewRef, bool);
WK_EXPORT void WKViewSetActive(WKViewRef, bool);
WK_EXPORT void WKViewSetVisible(WKViewRef, bool);

// Draw the view's rect into pixels (32 bits each, 0xAARRGGBB, `stride`
// pixels a row) that are the view's from (0, 0), surfaceSize big.
WK_EXPORT void WKViewPaint(WKViewRef, uint32_t* pixels, WKSize surfaceSize, unsigned stride, WKRect);

// Modifiers (as the embedder tracks them).
enum {
    kWKViewModifierShift = 1 << 0,
    kWKViewModifierControl = 1 << 1,
    kWKViewModifierAlt = 1 << 2,
    kWKViewModifierMeta = 1 << 3,
};

enum {
    kWKViewMouseMove = 0,
    kWKViewMouseDown = 1,
    kWKViewMouseUp = 2,
};
enum {
    kWKViewMouseButtonNone = 0,
    kWKViewMouseButtonLeft = 1,
    kWKViewMouseButtonMiddle = 2,
    kWKViewMouseButtonRight = 3,
};
// `buttons`: those held, 1 left, 2 right, 4 middle (the DOM's bits).
WK_EXPORT void WKViewSendMouseEvent(WKViewRef, int type, int button, unsigned buttons, int x, int y, int clickCount, unsigned modifiers);

// The wheel: steps (positive down and right), at (x, y).
WK_EXPORT void WKViewSendWheelEvent(WKViewRef, int x, int y, double stepsX, double stepsY, unsigned modifiers);

enum {
    kWKViewKeyDown = 0,
    kWKViewKeyUp = 1,
};
// A physical key: `usage` is its USB HID usage ID (RitterOS's WKEY_*); `text`
// (UTF-8, or null) what the key typed, for a key-down. A character typed with
// no key (a repeat) is a key-down with usage 0 and its text.
WK_EXPORT void WKViewSendKeyEvent(WKViewRef, int type, unsigned usage, const char* text, unsigned modifiers, bool isAutoRepeat);

#ifdef __cplusplus
}
#endif
