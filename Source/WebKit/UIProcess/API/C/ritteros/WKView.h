/*
 * DRAFT: Source/WebKit/UIProcess/API/C/ritteros/WKView.h
 *
 * The RitterOS embedding API: the cross-platform WebKit C API (WKContext, WKPage,
 * WKPageConfiguration, WKPreferences, WKURL, WKString, navigation clients) plus this view.
 * Modeled on UIProcess/API/C/playstation/WKView.h (view lifecycle, view client),
 * UIProcess/API/C/playstation/WKPagePrivatePlayStation.h (input + WKPagePaint) and
 * UIProcess/API/C/win/WKView.h. WKViewRef is declared in Shared/API/c/ritteros/WKBaseRitterOS.h,
 * included from WKBase.h under defined(__ritteros__) (patch WK7).
 */

#pragma once

#include <WebKit/WKBase.h>
#include <WebKit/WKEventRitterOS.h>   /* WKKeyboardEvent, WKMouseEvent, WKWheelEvent (copy of WKEventPlayStation.h) */
#include <WebKit/WKGeometry.h>
#include <WebKit/WKViewClient.h>      /* setViewNeedsDisplay, setCursor, ... (copy of playstation/WKViewClient.h) */

#ifdef __cplusplus
extern "C" {
#endif

/* Creates the view, its WebPageProxy and its PageClientImpl, and starts the web process. */
WK_EXPORT WKViewRef WKViewCreate(WKPageConfigurationRef configuration);
WK_EXPORT WKPageRef WKViewGetPage(WKViewRef view);
WK_EXPORT void WKViewSetViewClient(WKViewRef view, const WKViewClientBase* client);

/* Size in CSS pixels; scale is the framebuffer's device scale factor (1 for now).
   Calls DrawingAreaProxy::setSize() (PlayStation's WKViewSetSize forgets to). */
WK_EXPORT void WKViewSetSize(WKViewRef view, WKSize viewSize);
WK_EXPORT void WKViewSetDeviceScaleFactor(WKViewRef view, float scale);

WK_EXPORT void WKViewSetFocus(WKViewRef view, bool focused);
WK_EXPORT void WKViewSetActive(WKViewRef view, bool active);
WK_EXPORT void WKViewSetVisible(WKViewRef view, bool visible);

/* Input, in view coordinates. Call on the main thread (WKRunLoopCallOnMainThread). */
WK_EXPORT void WKViewHandleKeyboardEvent(WKViewRef view, WKKeyboardEvent event);
WK_EXPORT void WKViewHandleMouseEvent(WKViewRef view, WKMouseEvent event);
WK_EXPORT void WKViewHandleWheelEvent(WKViewRef view, WKWheelEvent event);

/* Copies the web page's backing store into a 32-bit premultiplied BGRA framebuffer
   (little-endian 0xAARRGGBB words, Skia's N32 with SK_R32_SHIFT=16), clipped to dirtyRect.
   Parts the backing store does not cover are filled with the page background.
   Typically called from the client's setViewNeedsDisplay callback. */
WK_EXPORT void WKViewPaint(WKViewRef view, uint32_t* pixels, WKSize framebufferSize, size_t bytesPerRow, WKRect dirtyRect);

/* Keep the UI-process backing store instead of dropping it 10 s after the last paint
   (DrawingAreaProxyCoordinatedGraphics.cpp:313-324); default true for a framebuffer embedder. */
WK_EXPORT void WKViewSetKeepsBackingStore(WKViewRef view, bool keep);

#ifdef __cplusplus
}
#endif
