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

// The page-world versions are WKUserScriptRef.cpp's WKUserScriptCreateWithSource,
// WKUserContentControllerRef.cpp's WKUserContentControllerAddScriptMessageHandler
// and WKPage.cpp's callAsyncJavaScript; these are the same with the world named.

#include "config.h"
#include "WKUserContentRitterOS.h"

#include "APICompletionListener.h"
#include "APIContentWorld.h"
#include "APIDictionary.h"
#include "APIFrameInfo.h"
#include "APIScriptMessage.h"
#include "APIUserScript.h"
#include "JavaScriptEvaluationResult.h"
#include "RunJavaScriptParameters.h"
#include "WKAPICast.h"
#include "WebPageProxy.h"
#include "WebScriptMessageHandler.h"
#include "WebUserContentControllerProxy.h"
#include <WebCore/RunJavaScriptParameters.h>
#include <wtf/TZoneMallocInlines.h>

using namespace WebKit;

static Ref<API::ContentWorld> worldNamed(WKStringRef name)
{
    return API::ContentWorld::sharedWorldWithName(toWTFString(name));
}

WKUserScriptRef WKUserScriptCreateWithSourceInWorld(WKStringRef source, _WKUserScriptInjectionTime injectionTime, bool forMainFrameOnly, WKStringRef worldName)
{
    return toAPILeakingRef(API::UserScript::create(WebCore::UserScript { toWTFString(source), { }, { }, { }, toUserScriptInjectionTime(injectionTime), forMainFrameOnly ? WebCore::UserContentInjectedFrames::InjectInTopFrameOnly : WebCore::UserContentInjectedFrames::InjectInAllFrames }, worldNamed(worldName)));
}

namespace {

class WorldScriptMessageClient final : public WebScriptMessageHandler::Client {
    WTF_MAKE_TZONE_ALLOCATED(WorldScriptMessageClient);
public:
    WorldScriptMessageClient(const String& name, WKScriptMessageHandlerCallback callback, const void* context, Ref<API::ContentWorld>&& world)
        : m_name(name)
        , m_callback(callback)
        , m_context(context)
        , m_world(WTF::move(world))
    {
    }

private:
    void didPostMessage(WebPageProxy& page, FrameInfoData&& frameInfo, API::ContentWorld&, JavaScriptEvaluationResult&& result, CompletionHandler<void(std::expected<JavaScriptEvaluationResult, String>&&)>&& completionHandler) override
    {
        Ref message = API::ScriptMessage::create(result.toAPI(), page, API::FrameInfo::create(WTF::move(frameInfo)), m_name, m_world.get());
        Ref listener = API::CompletionListener::create([completionHandler = WTF::move(completionHandler)](WKTypeRef reply) mutable {
            if (auto result = JavaScriptEvaluationResult::extract(protect(toImpl(reply)).get()))
                return completionHandler(WTF::move(*result));
            completionHandler(makeUnexpected(String()));
        });
        m_callback(toAPI(message.ptr()), toAPI(listener.ptr()), m_context);
    }

    String m_name;
    WKScriptMessageHandlerCallback m_callback;
    const void* m_context;
    Ref<API::ContentWorld> m_world;
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(WorldScriptMessageClient);

} // namespace

void WKUserContentControllerAddScriptMessageHandlerInWorld(WKUserContentControllerRef userContentController, WKStringRef wkName, WKScriptMessageHandlerCallback callback, const void* context, WKStringRef worldName)
{
    String name = toWTFString(wkName);
    Ref world = worldNamed(worldName);
    auto handler = WebScriptMessageHandler::create(makeUnique<WorldScriptMessageClient>(name, callback, context, world.copyRef()), name, world.get());
    protect(toImpl(userContentController))->addUserScriptMessageHandler(handler);
}

void WKPageCallAsyncJavaScriptInWorld(WKPageRef page, WKStringRef script, WKDictionaryRef arguments, WKFrameInfoRef frame, WKStringRef worldName, void* context, WKPageEvaluateJavaScriptFunction callback)
{
    auto extractArguments = [](API::Dictionary* dictionary) -> std::optional<Vector<std::pair<String, JavaScriptEvaluationResult>>> {
        if (!dictionary)
            return std::nullopt;
        Vector<std::pair<String, JavaScriptEvaluationResult>> result;
        for (auto& [key, value] : dictionary->map()) {
            if (auto js = JavaScriptEvaluationResult::extract(protect(value.get())))
                result.append({ key, WTF::move(*js) });
        }
        return { WTF::move(result) };
    };
    auto scriptString = IPC::TransferString::create(toImpl(script)->stringView());
    if (!scriptString) {
        if (callback)
            callback(nullptr, nullptr, context);
        return;
    }

    auto frameID = frame ? std::optional(toImpl(frame)->frameInfoData().frameID) : std::nullopt;
    protect(toImpl(page))->runJavaScriptInFrameInScriptWorld(WebKit::RunJavaScriptParameters {
        WTF::move(*scriptString),
        JSC::SourceTaintedOrigin::Untainted,
        URL { },
        WebCore::RunAsAsyncFunction::Yes,
        extractArguments(protect(toImpl(arguments)).get()),
        WebCore::ForceUserGesture::No,
        WebCore::RemoveTransientActivation::Yes
    }, frameID, worldNamed(worldName), !!callback, [context, callback](auto&& result) {
        if (!callback)
            return;
        if (result)
            callback(toAPI(result->toAPI().get()), nullptr, context);
        else
            callback(nullptr, nullptr, context);
    });
}
