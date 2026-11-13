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
#include "InjectedBundle.h"

namespace WebKit {

// No injected bundles on RitterOS yet (Platform/unix/ModuleUnix.cpp loads none).
bool InjectedBundle::initialize(const WebProcessCreationParameters&, RefPtr<API::Object>&&)
{
    return false;
}

void InjectedBundle::setBundleParameter(WTF::String const&, std::span<const uint8_t>)
{
}

void InjectedBundle::setBundleParameters(std::span<const uint8_t>)
{
}

} // namespace WebKit
