/*
 * DRAFT: Source/WebKit/WebProcess/WebPage/CoordinatedGraphics/LayerTreeHostNull.h
 *
 * DrawingAreaCoordinatedGraphics (the non-GLib one) owns a std::unique_ptr<LayerTreeHost>
 * (DrawingAreaCoordinatedGraphics.h:136) and calls 17 methods on it. The class comes from
 * LayerTreeHostPlayStation.h (USE(COORDINATED_GRAPHICS), GL), LayerTreeHostTextureMapper.h
 * (USE(GRAPHICS_LAYER_TEXTURE_MAPPER), GLContext) or LayerTreeHost.h (USE(COORDINATED_GRAPHICS)).
 * With USE(TEXTURE_MAPPER) alone none of them is visible, although
 * DrawingAreaCoordinatedGraphics::enterAcceleratedCompositingMode() already has the
 * "no layer tree host" branch (DrawingAreaCoordinatedGraphics.cpp:451-453:
 * m_layerTreeHost = nullptr; return;).
 *
 * This header supplies a LayerTreeHost that is never instantiated, so that file compiles.
 * Include it from DrawingAreaCoordinatedGraphics.cpp (patch WK3):
 *
 *   #if !PLATFORM(PLAYSTATION) && !USE(GRAPHICS_LAYER_TEXTURE_MAPPER) && !USE(COORDINATED_GRAPHICS)
 *   #include "LayerTreeHostNull.h"
 *   #endif
 */

#pragma once

#if !PLATFORM(PLAYSTATION) && !USE(GRAPHICS_LAYER_TEXTURE_MAPPER) && !USE(COORDINATED_GRAPHICS)

#include "LayerTreeContext.h"
#include <WebCore/DisplayRefreshMonitor.h>
#include <WebCore/PlatformScreen.h>
#include <wtf/CompletionHandler.h>
#include <wtf/Function.h>
#include <wtf/Noncopyable.h>

namespace WebCore {
class GraphicsLayer;
class GraphicsLayerFactory;
class IntRect;
class Region;
}

namespace WebKit {

class WebPage;

// Never constructed: accelerated compositing is off and enterAcceleratedCompositingMode()
// returns before creating one. Every method is unreachable.
class LayerTreeHost {
    WTF_MAKE_NONCOPYABLE(LayerTreeHost);
public:
    LayerTreeHost() = delete;
    ~LayerTreeHost() = default;

    const LayerTreeContext& layerTreeContext() const { RELEASE_ASSERT_NOT_REACHED(); }
    WebCore::PlatformDisplayID displayID() const { RELEASE_ASSERT_NOT_REACHED(); }
    WebCore::GraphicsLayerFactory* graphicsLayerFactory() { RELEASE_ASSERT_NOT_REACHED(); }
    RefPtr<WebCore::DisplayRefreshMonitor> createDisplayRefreshMonitor(WebCore::PlatformDisplayID) { RELEASE_ASSERT_NOT_REACHED(); }

    void setLayerTreeStateIsFrozen(bool) { RELEASE_ASSERT_NOT_REACHED(); }
    void scheduleRenderingUpdate() { RELEASE_ASSERT_NOT_REACHED(); }
    void setRootCompositingLayer(WebCore::GraphicsLayer*) { RELEASE_ASSERT_NOT_REACHED(); }
    void setViewOverlayRootLayer(WebCore::GraphicsLayer*) { RELEASE_ASSERT_NOT_REACHED(); }
    void setNonCompositedContentsNeedDisplay(const WebCore::IntRect&) { RELEASE_ASSERT_NOT_REACHED(); }
    void updateRenderingWithForcedRepaint() { RELEASE_ASSERT_NOT_REACHED(); }
    void updateRenderingWithForcedRepaintAsync(CompletionHandler<void()>&&) { RELEASE_ASSERT_NOT_REACHED(); }
    void sizeDidChange() { RELEASE_ASSERT_NOT_REACHED(); }
    void pauseRendering() { RELEASE_ASSERT_NOT_REACHED(); }
    void resumeRendering() { RELEASE_ASSERT_NOT_REACHED(); }
    void backgroundColorDidChange() { RELEASE_ASSERT_NOT_REACHED(); }
#if ENABLE(DAMAGE_TRACKING)
    void resetDamageHistoryForTesting() { RELEASE_ASSERT_NOT_REACHED(); }
    void foreachRegionInDamageHistoryForTesting(NOESCAPE const Function<void(const WebCore::Region&)>&) const { RELEASE_ASSERT_NOT_REACHED(); }
#endif
};

} // namespace WebKit

#endif
