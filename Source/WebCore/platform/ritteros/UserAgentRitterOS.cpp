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
#include "UserAgent.h"

#include <wtf/NeverDestroyed.h>
#include <wtf/text/MakeString.h>

// The user agent is fragile: sites choose what to send by it. Compass says
// what WebKitGTK's browsers say (X11 on Linux, Safari's version tokens), the
// form Gmail, Amazon and others recognize as a supported WebKit browser.

namespace WebCore {

static ASCIILiteral platformForUAString()
{
#if CPU(ARM64)
    return "X11; Linux aarch64"_s;
#else
    return "X11; Linux x86_64"_s;
#endif
}

static constexpr ASCIILiteral versionForUAString()
{
    // https://bugs.webkit.org/show_bug.cgi?id=180365
    return "605.1.15"_s;
}

static String standardUserAgentStatic()
{
    static NeverDestroyed<String> uaStatic(makeString("Mozilla/5.0 ("_s, platformForUAString(), ") AppleWebKit/"_s,
        versionForUAString(), " (KHTML, like Gecko) Version/18.0 Safari/"_s, versionForUAString()));
    return uaStatic;
}

String standardUserAgent(const String& applicationName, const String& applicationVersion)
{
    if (applicationName.isEmpty())
        return standardUserAgentStatic();
    if (applicationVersion.isEmpty())
        return makeString(standardUserAgentStatic(), ' ', applicationName);
    return makeString(standardUserAgentStatic(), ' ', applicationName, '/', applicationVersion);
}

String standardUserAgentForURL(const URL&)
{
    return { };
}

} // namespace WebCore
