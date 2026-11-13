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
#include "WKView.h"

#include "APIClient.h"
#include "APIPageConfiguration.h"
#include "APIViewClient.h"
#include "NativeWebKeyboardEvent.h"
#include "NativeWebMouseEvent.h"
#include "NativeWebWheelEvent.h"
#include "RitterOSWebView.h"
#include "WKAPICast.h"
#include "WKSharedAPICast.h"
#include "WebPageProxy.h"
#include <WebCore/Cursor.h>
#include <WebCore/Region.h>
#include <WebCore/Scrollbar.h>
#include <wtf/text/MakeString.h>

namespace API {
template<> struct ClientTraits<WKViewClientBase> {
    typedef std::tuple<WKViewClientV0> Versions;
};
}

using namespace WebKit;

static WKCursorType toWKCursorType(const WebCore::Cursor& cursor)
{
    switch (cursor.type()) {
    case WebCore::Cursor::Type::Hand:
        return kWKCursorTypeHand;
    case WebCore::Cursor::Type::IBeam:
        return kWKCursorTypeIBeam;
    case WebCore::Cursor::Type::None:
        return kWKCursorTypeNone;
    default:
        return kWKCursorTypePointer;
    }
}

WKViewRef WKViewCreate(WKPageConfigurationRef configuration)
{
    return toAPI(RitterOSWebView::create(*toImpl(configuration)).leakRef());
}

void WKViewSetViewClient(WKViewRef view, const WKViewClientBase* client)
{
    class ViewClient final : public API::Client<WKViewClientBase>, public API::ViewClient {
    public:
        explicit ViewClient(const WKViewClientBase* client)
        {
            initialize(client);
        }

    private:
        void setViewNeedsDisplay(RitterOSWebView& view, const WebCore::Region& region) final
        {
            if (!m_client.setViewNeedsDisplay)
                return;
            m_client.setViewNeedsDisplay(toAPI(&view), toAPI(region.bounds()), m_client.base.clientInfo);
        }

        void setCursor(RitterOSWebView& view, const WebCore::Cursor& cursor) final
        {
            if (m_client.setCursor)
                m_client.setCursor(toAPI(&view), toWKCursorType(cursor), m_client.base.clientInfo);
        }
    };

    toImpl(view)->setClient(makeUnique<ViewClient>(client));
}

WKPageRef WKViewGetPage(WKViewRef view)
{
    return toAPI(toImpl(view)->page());
}

void WKViewSetSize(WKViewRef view, WKSize viewSize)
{
    toImpl(view)->setViewSize(toIntSize(viewSize));
}

static void setViewActivityStateFlag(WKViewRef view, WebCore::ActivityState flag, bool set)
{
    auto viewState = toImpl(view)->viewState();
    if (set)
        viewState.add(flag);
    else
        viewState.remove(flag);
    toImpl(view)->setViewState(viewState);
}

void WKViewSetFocus(WKViewRef view, bool focused)
{
    setViewActivityStateFlag(view, WebCore::ActivityState::IsFocused, focused);
}

void WKViewSetActive(WKViewRef view, bool active)
{
    setViewActivityStateFlag(view, WebCore::ActivityState::WindowIsActive, active);
}

void WKViewSetVisible(WKViewRef view, bool visible)
{
    setViewActivityStateFlag(view, WebCore::ActivityState::IsVisible, visible);
}

void WKViewPaint(WKViewRef view, uint32_t* pixels, WKSize surfaceSize, unsigned stride, WKRect rect)
{
    toImpl(view)->paint(pixels, toIntSize(surfaceSize), stride, toIntRect(rect));
}

static OptionSet<WebEventModifier> toModifiers(unsigned modifiers)
{
    OptionSet<WebEventModifier> result;
    if (modifiers & kWKViewModifierShift)
        result.add(WebEventModifier::ShiftKey);
    if (modifiers & kWKViewModifierControl)
        result.add(WebEventModifier::ControlKey);
    if (modifiers & kWKViewModifierAlt)
        result.add(WebEventModifier::AltKey);
    if (modifiers & kWKViewModifierMeta)
        result.add(WebEventModifier::MetaKey);
    return result;
}

void WKViewSendMouseEvent(WKViewRef view, int type, int button, unsigned buttons, int x, int y, int clickCount, unsigned modifiers)
{
    WebMouseEventInit init;
    switch (type) {
    case kWKViewMouseDown:
        init.event.type = WebEventType::MouseDown;
        break;
    case kWKViewMouseUp:
        init.event.type = WebEventType::MouseUp;
        break;
    default:
        init.event.type = WebEventType::MouseMove;
        break;
    }
    init.event.modifiers = toModifiers(modifiers);
    init.event.timestamp = MonotonicTime::now();
    switch (button) {
    case kWKViewMouseButtonLeft:
        init.mouse.button = WebMouseEventButton::Left;
        break;
    case kWKViewMouseButtonMiddle:
        init.mouse.button = WebMouseEventButton::Middle;
        break;
    case kWKViewMouseButtonRight:
        init.mouse.button = WebMouseEventButton::Right;
        break;
    default:
        init.mouse.button = WebMouseEventButton::None;
        break;
    }
    init.mouse.buttons = buttons;
    init.mouse.position = { static_cast<double>(x), static_cast<double>(y) };
    init.mouse.globalPosition = init.mouse.position;
    init.mouse.clickCount = type == kWKViewMouseMove ? 0 : clickCount;
    toImpl(view)->page()->handleMouseEvent(NativeWebMouseEvent::create(WTF::move(init)));
}

void WKViewSendWheelEvent(WKViewRef view, int x, int y, double stepsX, double stepsY, unsigned modifiers)
{
    // A wheel step scrolls as GTK's does: Scrollbar::pixelsPerLineStep() three times.
    // WebKit's deltas are positive up/left.
    const float pixelsPerStep = WebCore::Scrollbar::pixelsPerLineStep() * 3;
    WebWheelEventInit init;
    init.event.type = WebEventType::Wheel;
    init.event.modifiers = toModifiers(modifiers);
    init.event.timestamp = MonotonicTime::now();
    init.wheel.position = { x, y };
    init.wheel.globalPosition = init.wheel.position;
    init.wheel.wheelTicks = { static_cast<float>(-stepsX), static_cast<float>(-stepsY) };
    init.wheel.delta = { init.wheel.wheelTicks.width() * pixelsPerStep, init.wheel.wheelTicks.height() * pixelsPerStep };
    init.wheel.granularity = WebWheelEventGranularity::ScrollByPixelWheelEvent;
    toImpl(view)->page()->handleNativeWheelEvent(NativeWebWheelEvent::create(WTF::move(init)));
}

namespace {

// A key by its USB HID usage ID: the DOM's `code`, its `key` when it types
// nothing, the legacy keyIdentifier and the Windows virtual key code.
struct KeyInfo {
    ASCIILiteral code;
    ASCIILiteral key; // empty: the key is what it typed
    ASCIILiteral identifier; // empty: "U+XXXX" of what it typed
    int windowsKeyCode;
};

static std::optional<KeyInfo> keyInfoForUsage(unsigned usage)
{
    if (usage >= 0x04 && usage <= 0x1d) { // A-Z
        static const ASCIILiteral codes[] = { "KeyA"_s, "KeyB"_s, "KeyC"_s, "KeyD"_s, "KeyE"_s, "KeyF"_s, "KeyG"_s, "KeyH"_s, "KeyI"_s,
            "KeyJ"_s, "KeyK"_s, "KeyL"_s, "KeyM"_s, "KeyN"_s, "KeyO"_s, "KeyP"_s, "KeyQ"_s, "KeyR"_s, "KeyS"_s, "KeyT"_s, "KeyU"_s,
            "KeyV"_s, "KeyW"_s, "KeyX"_s, "KeyY"_s, "KeyZ"_s };
        return KeyInfo { codes[usage - 0x04], { }, { }, static_cast<int>('A' + usage - 0x04) };
    }
    if (usage >= 0x1e && usage <= 0x27) { // 1-9, 0
        static const ASCIILiteral codes[] = { "Digit1"_s, "Digit2"_s, "Digit3"_s, "Digit4"_s, "Digit5"_s, "Digit6"_s, "Digit7"_s,
            "Digit8"_s, "Digit9"_s, "Digit0"_s };
        return KeyInfo { codes[usage - 0x1e], { }, { }, usage == 0x27 ? '0' : static_cast<int>('1' + usage - 0x1e) };
    }
    if (usage >= 0x3a && usage <= 0x45) { // F1-F12
        static const ASCIILiteral names[] = { "F1"_s, "F2"_s, "F3"_s, "F4"_s, "F5"_s, "F6"_s, "F7"_s, "F8"_s, "F9"_s, "F10"_s, "F11"_s, "F12"_s };
        auto name = names[usage - 0x3a];
        return KeyInfo { name, name, name, static_cast<int>(0x70 + usage - 0x3a) };
    }
    switch (usage) {
    case 0x28: return KeyInfo { "Enter"_s, "Enter"_s, "Enter"_s, 0x0d };
    case 0x29: return KeyInfo { "Escape"_s, "Escape"_s, "U+001B"_s, 0x1b };
    case 0x2a: return KeyInfo { "Backspace"_s, "Backspace"_s, "U+0008"_s, 0x08 };
    case 0x2b: return KeyInfo { "Tab"_s, "Tab"_s, "U+0009"_s, 0x09 };
    case 0x2c: return KeyInfo { "Space"_s, { }, { }, 0x20 };
    case 0x2d: return KeyInfo { "Minus"_s, { }, { }, 0xbd };
    case 0x2e: return KeyInfo { "Equal"_s, { }, { }, 0xbb };
    case 0x2f: return KeyInfo { "BracketLeft"_s, { }, { }, 0xdb };
    case 0x30: return KeyInfo { "BracketRight"_s, { }, { }, 0xdd };
    case 0x31: return KeyInfo { "Backslash"_s, { }, { }, 0xdc };
    case 0x33: return KeyInfo { "Semicolon"_s, { }, { }, 0xba };
    case 0x34: return KeyInfo { "Quote"_s, { }, { }, 0xde };
    case 0x35: return KeyInfo { "Backquote"_s, { }, { }, 0xc0 };
    case 0x36: return KeyInfo { "Comma"_s, { }, { }, 0xbc };
    case 0x37: return KeyInfo { "Period"_s, { }, { }, 0xbe };
    case 0x38: return KeyInfo { "Slash"_s, { }, { }, 0xbf };
    case 0x39: return KeyInfo { "CapsLock"_s, "CapsLock"_s, "CapsLock"_s, 0x14 };
    case 0x46: return KeyInfo { "PrintScreen"_s, "PrintScreen"_s, "PrintScreen"_s, 0x2c };
    case 0x47: return KeyInfo { "ScrollLock"_s, "ScrollLock"_s, "Scroll"_s, 0x91 };
    case 0x48: return KeyInfo { "Pause"_s, "Pause"_s, "Pause"_s, 0x13 };
    case 0x49: return KeyInfo { "Insert"_s, "Insert"_s, "Insert"_s, 0x2d };
    case 0x4a: return KeyInfo { "Home"_s, "Home"_s, "Home"_s, 0x24 };
    case 0x4b: return KeyInfo { "PageUp"_s, "PageUp"_s, "PageUp"_s, 0x21 };
    case 0x4c: return KeyInfo { "Delete"_s, "Delete"_s, "U+007F"_s, 0x2e };
    case 0x4d: return KeyInfo { "End"_s, "End"_s, "End"_s, 0x23 };
    case 0x4e: return KeyInfo { "PageDown"_s, "PageDown"_s, "PageDown"_s, 0x22 };
    case 0x4f: return KeyInfo { "ArrowRight"_s, "ArrowRight"_s, "Right"_s, 0x27 };
    case 0x50: return KeyInfo { "ArrowLeft"_s, "ArrowLeft"_s, "Left"_s, 0x25 };
    case 0x51: return KeyInfo { "ArrowDown"_s, "ArrowDown"_s, "Down"_s, 0x28 };
    case 0x52: return KeyInfo { "ArrowUp"_s, "ArrowUp"_s, "Up"_s, 0x26 };
    case 0xe0: return KeyInfo { "ControlLeft"_s, "Control"_s, "Control"_s, 0x11 };
    case 0xe1: return KeyInfo { "ShiftLeft"_s, "Shift"_s, "Shift"_s, 0x10 };
    case 0xe2: return KeyInfo { "AltLeft"_s, "Alt"_s, "Alt"_s, 0x12 };
    case 0xe3: return KeyInfo { "MetaLeft"_s, "Meta"_s, "Meta"_s, 0x5b };
    case 0xe4: return KeyInfo { "ControlRight"_s, "Control"_s, "Control"_s, 0x11 };
    case 0xe5: return KeyInfo { "ShiftRight"_s, "Shift"_s, "Shift"_s, 0x10 };
    case 0xe6: return KeyInfo { "AltRight"_s, "Alt"_s, "Alt"_s, 0x12 };
    case 0xe7: return KeyInfo { "MetaRight"_s, "Meta"_s, "Meta"_s, 0x5c };
    }
    return std::nullopt;
}

} // namespace

void WKViewSendKeyEvent(WKViewRef view, int type, unsigned usage, const char* utf8Text, unsigned modifiers, bool isAutoRepeat)
{
    String text = utf8Text ? String::fromUTF8(utf8Text) : String();
    auto info = keyInfoForUsage(usage);

    // Keys whose action WebCore's editing takes from the text they type.
    if (text.isEmpty() && type == kWKViewKeyDown && info) {
        if (usage == 0x28)
            text = "\r"_s;
        else if (usage == 0x2b)
            text = "\t"_s;
    }

    WebKeyboardEventInit init;
    init.event.type = type == kWKViewKeyUp ? WebEventType::KeyUp : WebEventType::KeyDown;
    init.event.modifiers = toModifiers(modifiers);
    init.event.timestamp = MonotonicTime::now();
    if (type != kWKViewKeyUp)
        init.keyboard.text = text;
    init.keyboard.unmodifiedText = init.keyboard.text;
    init.keyboard.isAutoRepeat = isAutoRepeat;
    init.keyboard.nativeVirtualKeyCode = usage;

    String typed = text.isEmpty() || text[0] < 0x20 ? String() : text;
    if (info) {
        init.keyboard.code = info->code;
        init.keyboard.key = !info->key.isNull() ? String(info->key) : (typed.isEmpty() ? String("Unidentified"_s) : typed);
        init.keyboard.windowsVirtualKeyCode = info->windowsKeyCode;
        if (!info->identifier.isNull())
            init.keyboard.keyIdentifier = info->identifier;
    } else {
        init.keyboard.code = "Unidentified"_s;
        init.keyboard.key = typed.isEmpty() ? String("Unidentified"_s) : typed;
        // A typed character with no known key: its upper-case letter or digit as the key code.
        if (!typed.isEmpty() && isASCIIAlphanumeric(typed[0]))
            init.keyboard.windowsVirtualKeyCode = toASCIIUpper(typed[0]);
    }
    if (init.keyboard.keyIdentifier.isEmpty()) {
        char32_t c = typed.isEmpty() ? 0 : typed[0];
        if (info && info->windowsKeyCode >= 'A' && info->windowsKeyCode <= 'Z')
            c = info->windowsKeyCode;
        init.keyboard.keyIdentifier = c ? makeString("U+"_s, hex(static_cast<unsigned>(c), 4)) : String("Unidentified"_s);
    }

    toImpl(view)->page()->handleKeyboardEvent(NativeWebKeyboardEvent::create(WTF::move(init)));
}
