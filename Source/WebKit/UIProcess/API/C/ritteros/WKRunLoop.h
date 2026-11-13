/*
 * Copyright (C) 2021 Sony Interactive Entertainment Inc.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <WebKit/WKBase.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void(*WKRunLoopCallback)(void *userData);

WK_EXPORT void WKRunLoopRunMain();
WK_EXPORT void WKRunLoopStopMain();
WK_EXPORT void WKRunLoopCallOnMainThread(WKRunLoopCallback, void* userData);
WK_EXPORT void WKRunLoopInitializeMain();

// The main run loop inside the embedder's own wait (RitterOS): one pass of
// it (due timers, queued work), then the milliseconds until it has work
// again (0: at once, -1: only when woken). The wake-up callback is called,
// from any thread, when work is queued or a timer starts.
WK_EXPORT int WKRunLoopCycleMain();
WK_EXPORT void WKRunLoopSetWakeUpCallback(WKRunLoopCallback, void* userData);

#ifdef __cplusplus
}
#endif
