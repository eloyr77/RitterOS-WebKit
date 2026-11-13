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
#pragma once

#include "APIObject.h"
#include "APIViewClient.h"
#include "PageClientImpl.h"
#include "WebPageProxy.h"
#include <wtf/TZoneMalloc.h>

namespace WebKit {

// RitterOS's web view: a WebPageProxy drawn by the non-composited drawing
// area into the embedder's pixels (paint), fed with the embedder's input.
class RitterOSWebView : public API::ObjectImpl<API::Object::Type::View> {
    WTF_MAKE_TZONE_ALLOCATED(RitterOSWebView);
public:
    static RefPtr<RitterOSWebView> create(const API::PageConfiguration&);
    virtual ~RitterOSWebView();

    void setClient(std::unique_ptr<API::ViewClient>&&);

    WebPageProxy* page() { return m_page.get(); }

    void setViewSize(WebCore::IntSize);
    WebCore::IntSize viewSize() const { return m_viewSize; }

    void setViewState(OptionSet<WebCore::ActivityState>);
    OptionSet<WebCore::ActivityState> viewState() const { return m_viewStateFlags; }

    // Draw the page's rect (view coordinates) into 32-bit pixels (0xAARRGGBB,
    // stride in pixels) whose (0, 0) is the view's.
    void paint(uint32_t* pixels, WebCore::IntSize surfaceSize, unsigned stride, const WebCore::IntRect&);

    // Functions called by PageClientImpl
    void setViewNeedsDisplay(const WebCore::Region&);
#if ENABLE(FULLSCREEN_API)
    bool isFullScreen() { return false; }
    void closeFullScreenManager() { }
    void enterFullScreen(CompletionHandler<void(bool)>&& completionHandler) { completionHandler(false); }
    void exitFullScreen(CompletionHandler<void()>&& completionHandler) { completionHandler(); }
    void beganEnterFullScreen(const WebCore::IntRect&, const WebCore::IntRect&, CompletionHandler<void(bool)>&& completionHandler) { completionHandler(false); }
    void beganExitFullScreen(const WebCore::IntRect&, const WebCore::IntRect&, CompletionHandler<void()>&& completionHandler) { completionHandler(); }
#endif
    void setCursor(const WebCore::Cursor&);

private:
    explicit RitterOSWebView(const API::PageConfiguration&);

    std::unique_ptr<API::ViewClient> m_client;
    const std::unique_ptr<WebKit::PageClientImpl> m_pageClient;
    const RefPtr<WebPageProxy> m_page;
    OptionSet<WebCore::ActivityState> m_viewStateFlags;
    WebCore::IntSize m_viewSize;
};

} // namespace WebKit
