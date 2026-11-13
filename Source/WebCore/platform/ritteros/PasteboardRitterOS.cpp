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
#include "Pasteboard.h"

#include "NotImplemented.h"
#include <wtf/HashMap.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/text/StringHash.h>

namespace WebCore {

// Milestone 1: a pasteboard of the web process's own (copy and paste work
// within the pages one process shows). The system clipboard (windowd's)
// comes through the UI process later.
static HashMap<String, String>& processPasteboard()
{
    static NeverDestroyed<HashMap<String, String>> data;
    return data;
}

static constexpr ASCIILiteral plainTextType = "text/plain;charset=utf-8"_s;

std::unique_ptr<Pasteboard> Pasteboard::createForCopyAndPaste(std::unique_ptr<PasteboardContext>&& context)
{
    return makeUnique<Pasteboard>(WTF::move(context));
}

Pasteboard::Pasteboard(std::unique_ptr<PasteboardContext>&& context)
    : m_context(WTF::move(context))
{
}

bool Pasteboard::hasData()
{
    return !processPasteboard().isEmpty();
}

Vector<String> Pasteboard::typesSafeForBindings(const String&)
{
    return typesForLegacyUnsafeBindings();
}

Vector<String> Pasteboard::typesForLegacyUnsafeBindings()
{
    return copyToVector(processPasteboard().keys());
}

String Pasteboard::readOrigin()
{
    return { };
}

String Pasteboard::readString(const String& type)
{
    return processPasteboard().get(type == "text/plain"_s ? String(plainTextType) : type);
}

String Pasteboard::readStringInCustomData(const String&)
{
    return { };
}

void Pasteboard::writeString(const String& type, const String& text)
{
    processPasteboard().set(type == "text/plain"_s ? String(plainTextType) : type, text);
}

void Pasteboard::clear()
{
    processPasteboard().clear();
}

void Pasteboard::clear(const String& type)
{
    processPasteboard().remove(type);
}

void Pasteboard::read(PasteboardPlainText& text, PlainTextURLReadingPolicy, std::optional<size_t>)
{
    text.text = processPasteboard().get(plainTextType);
}

void Pasteboard::read(PasteboardWebContentReader&, WebContentReadingPolicy, std::optional<size_t>)
{
    notImplemented();
}

void Pasteboard::read(PasteboardFileReader&, std::optional<size_t>)
{
}

void Pasteboard::write(const PasteboardURL& url)
{
    clear();
    writeString(plainTextType, url.url.string());
}

void Pasteboard::writeTrustworthyWebURLsPboardType(const PasteboardURL&)
{
}

void Pasteboard::write(PasteboardImage&&)
{
}

void Pasteboard::write(const PasteboardBuffer&)
{
}

void Pasteboard::write(const PasteboardWebContent& content)
{
    clear();
    writeString(plainTextType, content.text);
    if (!content.markup.isEmpty())
        writeString("text/html"_s, content.markup);
}

Pasteboard::FileContentState Pasteboard::fileContentState()
{
    return FileContentState::NoFileOrImageData;
}

bool Pasteboard::canSmartReplace()
{
    return false;
}

void Pasteboard::writeMarkup(const String& markup)
{
    writeString("text/html"_s, markup);
}

void Pasteboard::writePlainText(const String& text, SmartReplaceOption)
{
    clear();
    writeString(plainTextType, text);
}

void Pasteboard::writeCustomData(const Vector<PasteboardCustomData>&, PasteboardWriteType)
{
}

void Pasteboard::write(const Color&)
{
}

} // namespace WebCore
