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

#include <wtf/text/WTFString.h>

namespace WebCore {

// RitterOS: a file WebCore brings in the app's bundle (as Windows' WebKit
// bundle, GTK's GResources): BUNDLE/share/FOLDER/NAME. The bundle is the
// program's folder (the app and WebKit's helper programs are at its top:
// the kernel gives the program's path, AT_EXECFN), else the folder above
// libWebKit's lib/.
WEBCORE_EXPORT String bundleFilePath(StringView folder, StringView name);

} // namespace WebCore
