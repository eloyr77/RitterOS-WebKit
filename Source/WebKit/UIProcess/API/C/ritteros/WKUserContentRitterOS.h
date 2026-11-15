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

// RitterOS: the C API's user scripts, script message handlers and
// callAsyncJavaScript in a JavaScript world of the browser's own (a named,
// isolated world: the page shares the document with it, not its globals,
// so the page's scripts can neither call its message handler nor see what
// it keeps). The C API's own calls use the page's world only.

#pragma once

#include <WebKit/WKBase.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKUserContentControllerRef.h>
#include <WebKit/WKUserScriptInjectionTime.h>

#ifdef __cplusplus
extern "C" {
#endif

// A user script that runs in the world named `worldName` (made at its first use).
WK_EXPORT WKUserScriptRef WKUserScriptCreateWithSourceInWorld(WKStringRef source, _WKUserScriptInjectionTime, bool forMainFrameOnly, WKStringRef worldName);

// window.webkit.messageHandlers.NAME in that world only.
WK_EXPORT void WKUserContentControllerAddScriptMessageHandlerInWorld(WKUserContentControllerRef, WKStringRef name, WKScriptMessageHandlerCallback, const void* context, WKStringRef worldName);

// WKPageCallAsyncJavaScriptWithoutUserGesture in that world: the script is
// an async function's body, `arguments` its arguments by name; `frame` the
// frame (null: the main frame).
WK_EXPORT void WKPageCallAsyncJavaScriptInWorld(WKPageRef, WKStringRef script, WKDictionaryRef arguments, WKFrameInfoRef frame, WKStringRef worldName, void* context, WKPageEvaluateJavaScriptFunction callback);

#ifdef __cplusplus
}
#endif
