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
#include "config.h"
#include "RitterOSWebView.h"

#include "APIPageConfiguration.h"
#include "DrawingAreaProxyCoordinatedGraphics.h"
#include "WebProcessPool.h"
#if ENABLE(FULLSCREEN_API)
#include "WebFullScreenManagerProxy.h"
#endif
#include <WebCore/Region.h>
#include <skia/core/SkCanvas.h>
#include <skia/core/SkColorSpace.h>
#include <skia/core/SkSurface.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebKit {

WTF_MAKE_TZONE_ALLOCATED_IMPL(RitterOSWebView);

RefPtr<RitterOSWebView> RitterOSWebView::create(const API::PageConfiguration& configuration)
{
    return adoptRef(*new RitterOSWebView(configuration));
}

RitterOSWebView::RitterOSWebView(const API::PageConfiguration& conf)
    : m_pageClient(makeUniqueWithoutRefCountedCheck<PageClientImpl>(*this))
    , m_viewStateFlags { WebCore::ActivityState::WindowIsActive, WebCore::ActivityState::IsFocused, WebCore::ActivityState::IsVisible, WebCore::ActivityState::IsInWindow }
{
    auto configuration = conf.copy();
    auto& pool = configuration->processPool();
    lazyInitialize(m_page, pool.createWebPage(*m_pageClient, WTF::move(configuration)));

    auto& pageConfiguration = m_page->configuration();
    m_page->initializeWebPage(pageConfiguration.openedSite(), pageConfiguration.initialSandboxFlags(), pageConfiguration.initialReferrerPolicy());

    // The embedder paints from the backing store whenever its window needs it:
    // keep it instead of dropping it after a while (and waiting for a new one).
    if (auto* drawingArea = m_page->drawingArea())
        drawingArea->setBackingStoreIsDiscardable(false);
}

RitterOSWebView::~RitterOSWebView() = default;

void RitterOSWebView::setClient(std::unique_ptr<API::ViewClient>&& client)
{
    if (!client)
        m_client = makeUnique<API::ViewClient>();
    else
        m_client = WTF::move(client);
}

void RitterOSWebView::setViewSize(WebCore::IntSize viewSize)
{
    if (m_viewSize == viewSize)
        return;
    m_viewSize = viewSize;
    // (PlayStation's view only stores the size: the drawing area must be told.)
    if (auto* drawingArea = m_page->drawingArea())
        drawingArea->setSize(viewSize);
}

void RitterOSWebView::setViewState(OptionSet<WebCore::ActivityState> flags)
{
    auto changedFlags = m_viewStateFlags ^ flags;
    m_viewStateFlags = flags;
    if (changedFlags)
        m_page->activityStateDidChange(changedFlags);
}

void RitterOSWebView::paint(uint32_t* pixels, WebCore::IntSize surfaceSize, unsigned stride, const WebCore::IntRect& rect)
{
    if (!pixels || surfaceSize.isEmpty())
        return;
    auto info = SkImageInfo::MakeN32Premul(surfaceSize.width(), surfaceSize.height(), SkColorSpace::MakeSRGB());
    auto surface = SkSurfaces::WrapPixels(info, pixels, stride * 4, nullptr);
    if (!surface)
        return;
    auto* canvas = surface->getCanvas();
    auto paintRect = intersection(rect, WebCore::IntRect({ }, surfaceSize));

    WebCore::Region unpainted;
    if (auto* drawingArea = m_page->drawingArea())
        downcast<DrawingAreaProxyCoordinatedGraphics>(*drawingArea).paint(canvas, paintRect, unpainted);
    else
        unpainted = WebCore::Region(paintRect);

    // What the backing store does not have yet: the page's background.
    auto background = m_page->backgroundColor().value_or(WebCore::Color::white);
    auto [r, g, b, a] = background.toColorTypeLossy<WebCore::SRGBA<uint8_t>>().resolved();
    SkPaint paint;
    paint.setColor(SkColorSetARGB(255, r, g, b));
    for (const auto& unpaintedRect : unpainted.rects())
        canvas->drawRect(SkRect::MakeXYWH(unpaintedRect.x(), unpaintedRect.y(), unpaintedRect.width(), unpaintedRect.height()), paint);
}

void RitterOSWebView::setViewNeedsDisplay(const WebCore::Region& region)
{
    if (m_client)
        m_client->setViewNeedsDisplay(*this, region);
}

#if ENABLE(FULLSCREEN_API)
// (PlayStationWebView's, the client told at each step)
void RitterOSWebView::requestExitFullScreen()
{
    if (isFullScreen())
        m_page->fullScreenManager()->requestExitFullScreen();
}

void RitterOSWebView::closeFullScreenManager()
{
    if (m_client && isFullScreen())
        m_client->closeFullScreen(*this);
    m_isFullScreen = false;
}

void RitterOSWebView::enterFullScreen(CompletionHandler<void(bool)>&& completionHandler)
{
    if (!m_client || isFullScreen())
        return completionHandler(false);
    m_client->enterFullScreen(*this, [this, protectedThis = Ref { *this }, completionHandler = WTF::move(completionHandler)](bool entered) mutable {
        m_isFullScreen = entered;
        completionHandler(entered);
    });
}

void RitterOSWebView::exitFullScreen(CompletionHandler<void()>&& completionHandler)
{
    if (m_client && isFullScreen())
        m_client->exitFullScreen(*this);
    completionHandler();
}

void RitterOSWebView::beganEnterFullScreen(const WebCore::IntRect& initialFrame, const WebCore::IntRect& finalFrame, CompletionHandler<void(bool)>&& completionHandler)
{
    if (!m_client)
        return completionHandler(false);
    m_client->beganEnterFullScreen(*this, initialFrame, finalFrame, WTF::move(completionHandler));
}

void RitterOSWebView::beganExitFullScreen(const WebCore::IntRect& initialFrame, const WebCore::IntRect& finalFrame, CompletionHandler<void()>&& completionHandler)
{
    if (!m_client) {
        m_isFullScreen = false;
        return completionHandler();
    }
    m_client->beganExitFullScreen(*this, initialFrame, finalFrame, [this, protectedThis = Ref { *this }, completionHandler = WTF::move(completionHandler)] mutable {
        m_isFullScreen = false;
        completionHandler();
    });
}
#endif

void RitterOSWebView::setCursor(const WebCore::Cursor& cursor)
{
    if (m_client)
        m_client->setCursor(*this, cursor);
}

} // namespace WebKit
