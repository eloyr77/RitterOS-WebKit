/*
 * DRAFT: Source/WebCore/platform/graphics/ritteros/GraphicsLayerRitterOS.cpp
 *
 * GraphicsLayer::create() is defined by the compositing backend
 * (ca/GraphicsLayerCA.cpp:325, coordinated/GraphicsLayerCoordinated.cpp:73,
 * texmap/GraphicsLayerTextureMapper.cpp:35). RitterOS compiles none of them: they need
 * TextureMapper (GL). WebCore still references create() (RenderLayerBacking,
 * PageOverlayController.cpp:70-71, ...), and PageOverlayController creates its root layers
 * even with accelerated compositing off, so return a layer that holds state and paints nothing.
 * (In non-composited mode page overlays, e.g. find-in-page highlights, are not shown: M2.)
 */

#include "config.h"
#include "GraphicsLayer.h"

#if PLATFORM(RITTEROS)

#include "GraphicsLayerFactory.h"

namespace WebCore {

class GraphicsLayerRitterOS final : public GraphicsLayer {
public:
    GraphicsLayerRitterOS(Type layerType, GraphicsLayerClient& client)
        : GraphicsLayer(layerType, client)
    {
    }

private:
    void setNeedsDisplay() final { }
    void setNeedsDisplayInRect(const FloatRect&, ShouldClipToLayer) final { }
};

Ref<GraphicsLayer> GraphicsLayer::create(GraphicsLayerFactory* factory, GraphicsLayerClient& client, Type layerType)
{
    if (factory)
        return factory->createGraphicsLayer(layerType, client);
    return adoptRef(*new GraphicsLayerRitterOS(layerType, client));
}

} // namespace WebCore

#endif // PLATFORM(RITTEROS)
