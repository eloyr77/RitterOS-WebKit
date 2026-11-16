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
#include "BundleRitterOS.h"

#include <array>
#include <dlfcn.h>
#include <sys/auxv.h>
#include <wtf/FileSystem.h>
#include <wtf/NeverDestroyed.h>

namespace WebCore {

static String bundleDirectory()
{
    // (dladdr names the library without its folder when it came through the
    // program's run path: the program's own path first)
    if (auto* program = reinterpret_cast<const char*>(getauxval(AT_EXECFN)); program && *program == '/')
        return FileSystem::parentPath(String::fromUTF8(program));
    Dl_info info { };
    if (dladdr(reinterpret_cast<void*>(&bundleFilePath), &info) && info.dli_fname && *info.dli_fname == '/')
        return FileSystem::parentPath(FileSystem::parentPath(String::fromUTF8(info.dli_fname)));
    return { };
}

String bundleFilePath(StringView folder, StringView name)
{
    static NeverDestroyed<String> bundle = bundleDirectory();
    std::array<StringView, 3> components { "share"_s, folder, name };
    return FileSystem::pathByAppendingComponents(bundle.get(), components);
}

} // namespace WebCore
