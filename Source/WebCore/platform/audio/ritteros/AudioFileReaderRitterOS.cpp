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

// RitterOS: decodeAudioData() and Web Audio's own sounds (AudioBus::
// loadPlatformResource, the HRTF impulse responses) through FFmpeg, as the
// media engine decodes: the whole file in memory, its first audio stream,
// as 32-bit float channels at the rate asked (the file's when 0).

#include "config.h"
#include "AudioFileReader.h"

#if ENABLE(WEB_AUDIO) && PLATFORM(RITTEROS)

#include "AudioBus.h"
#include "AudioChannel.h"
#include <wtf/FileSystem.h>
#include <wtf/StdLibExtras.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringView.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <dlfcn.h>
#include <sys/auxv.h>
#include <stdio.h>

namespace WebCore {

namespace {

struct MemoryReader {
    std::span<const uint8_t> bytes;
    size_t position { 0 };

    static int read(void* opaque, uint8_t* buffer, int size)
    {
        auto* reader = static_cast<MemoryReader*>(opaque);
        size_t left = reader->bytes.size() - reader->position;
        if (!left)
            return AVERROR_EOF;
        size_t count = std::min<size_t>(left, size);
        memcpy(buffer, reader->bytes.subspan(reader->position, count).data(), count);
        reader->position += count;
        return static_cast<int>(count);
    }
    static int64_t seek(void* opaque, int64_t offset, int whence)
    {
        auto* reader = static_cast<MemoryReader*>(opaque);
        switch (whence & ~AVSEEK_FORCE) {
        case AVSEEK_SIZE:
            return reader->bytes.size();
        case SEEK_SET:
            break;
        case SEEK_CUR:
            offset += reader->position;
            break;
        case SEEK_END:
            offset += reader->bytes.size();
            break;
        default:
            return -1;
        }
        if (offset < 0 || static_cast<uint64_t>(offset) > reader->bytes.size())
            return -1;
        reader->position = offset;
        return offset;
    }
};

// What one decode holds (freed whatever way it ends).
struct Decode {
    MemoryReader reader;
    AVIOContext* io { nullptr };
    AVFormatContext* format { nullptr };
    AVCodecContext* codec { nullptr };
    SwrContext* resampler { nullptr };
    AVPacket* packet { nullptr };
    AVFrame* frame { nullptr };

    ~Decode()
    {
        av_frame_free(&frame);
        av_packet_free(&packet);
        swr_free(&resampler);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
        if (io) {
            av_freep(&io->buffer);
            avio_context_free(&io);
        }
    }
};

} // namespace

RefPtr<AudioBus> createBusFromInMemoryAudioFile(std::span<const uint8_t> data, bool mixToMono, float sampleRate)
{
    if (data.empty())
        return nullptr;

    Decode decode;
    decode.reader.bytes = data;
    constexpr int bufferSize = 32 * 1024;
    decode.io = avio_alloc_context(static_cast<uint8_t*>(av_malloc(bufferSize)), bufferSize, 0, &decode.reader, MemoryReader::read, nullptr, MemoryReader::seek);
    decode.format = avformat_alloc_context();
    if (!decode.io || !decode.format)
        return nullptr;
    decode.format->pb = decode.io;
    decode.format->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&decode.format, nullptr, nullptr, nullptr) < 0) {
        decode.format = nullptr; // (freed by avformat_open_input)
        return nullptr;
    }
    if (avformat_find_stream_info(decode.format, nullptr) < 0)
        return nullptr;

    const AVCodec* decoder = nullptr;
    int streamIndex = av_find_best_stream(decode.format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (streamIndex < 0 || !decoder)
        return nullptr;
    AVStream* stream = decode.format->streams[streamIndex];
    decode.codec = avcodec_alloc_context3(decoder);
    if (!decode.codec || avcodec_parameters_to_context(decode.codec, stream->codecpar) < 0)
        return nullptr;
    if (avcodec_open2(decode.codec, decoder, nullptr) < 0)
        return nullptr;

    AVChannelLayout inputLayout;
    if (decode.codec->ch_layout.nb_channels > 0)
        av_channel_layout_copy(&inputLayout, &decode.codec->ch_layout);
    else
        av_channel_layout_default(&inputLayout, 2);
    unsigned channels = mixToMono ? 1 : static_cast<unsigned>(inputLayout.nb_channels);
    if (!channels || channels > 32) {
        av_channel_layout_uninit(&inputLayout);
        return nullptr;
    }
    int inputRate = decode.codec->sample_rate;
    int outputRate = sampleRate > 0 ? static_cast<int>(sampleRate) : inputRate;
    if (inputRate <= 0 || outputRate <= 0) {
        av_channel_layout_uninit(&inputLayout);
        return nullptr;
    }
    AVChannelLayout outputLayout;
    if (mixToMono)
        av_channel_layout_default(&outputLayout, 1);
    else
        av_channel_layout_copy(&outputLayout, &inputLayout);
    int failed = swr_alloc_set_opts2(&decode.resampler, &outputLayout, AV_SAMPLE_FMT_FLTP, outputRate,
        &inputLayout, decode.codec->sample_fmt, inputRate, 0, nullptr);
    av_channel_layout_uninit(&inputLayout);
    av_channel_layout_uninit(&outputLayout);
    if (failed < 0 || swr_init(decode.resampler) < 0)
        return nullptr;

    decode.packet = av_packet_alloc();
    decode.frame = av_frame_alloc();
    if (!decode.packet || !decode.frame)
        return nullptr;

    Vector<Vector<float>> samples(channels);
    Vector<uint8_t*> planes(channels);
    auto convert = [&](const uint8_t** input, int inputCount) {
        int room = swr_get_out_samples(decode.resampler, inputCount);
        if (room <= 0)
            return;
        for (unsigned c = 0; c < channels; c++) {
            size_t at = samples[c].size();
            samples[c].grow(at + room);
            planes[c] = reinterpret_cast<uint8_t*>(samples[c].mutableSpan().subspan(at).data());
        }
        int converted = swr_convert(decode.resampler, planes.mutableSpan().data(), room, input, inputCount);
        for (unsigned c = 0; c < channels; c++)
            samples[c].shrink(samples[c].size() - room + std::max(converted, 0));
    };
    auto receive = [&] {
        while (avcodec_receive_frame(decode.codec, decode.frame) >= 0) {
            convert(const_cast<const uint8_t**>(decode.frame->extended_data), decode.frame->nb_samples);
            av_frame_unref(decode.frame);
        }
    };

    while (av_read_frame(decode.format, decode.packet) >= 0) {
        if (decode.packet->stream_index == streamIndex && avcodec_send_packet(decode.codec, decode.packet) >= 0)
            receive();
        av_packet_unref(decode.packet);
    }
    avcodec_send_packet(decode.codec, nullptr); // (the decoder's last frames)
    receive();
    convert(nullptr, 0); // (the resampler's)

    size_t length = samples[0].size();
    if (!length)
        return nullptr;
    auto bus = AudioBus::create(channels, length);
    for (unsigned c = 0; c < channels; c++)
        memcpySpan(bus->channel(c)->mutableSpan(), samples[c].span().first(length));
    bus->setSampleRate(outputRate);
    return bus;
}

// Web Audio's sounds (the HRTF database's impulse responses, "Composite"):
// WebCore/platform/audio/resources/NAME.wav in the bundle's share/webaudio.
// The bundle is the program's folder (Compass and WebKit's helper programs
// are at its top: the kernel gives the program's path, AT_EXECFN), else
// the folder above libWebKit's lib/ (dladdr names the file without its
// folder when the library came through the program's run path).
static String bundleDirectory()
{
    if (auto* program = reinterpret_cast<const char*>(getauxval(AT_EXECFN)); program && *program == '/')
        return FileSystem::parentPath(String::fromUTF8(program));
    Dl_info info { };
    if (dladdr(reinterpret_cast<void*>(&createBusFromInMemoryAudioFile), &info) && info.dli_fname && *info.dli_fname == '/')
        return FileSystem::parentPath(FileSystem::parentPath(String::fromUTF8(info.dli_fname)));
    return { };
}

RefPtr<AudioBus> AudioBus::loadPlatformResource(StringView name, float sampleRate)
{
    String file = makeString(name, ".wav"_s);
    std::array<StringView, 3> components { "share"_s, "webaudio"_s, file };
    String path = FileSystem::pathByAppendingComponents(bundleDirectory(), components);
    auto contents = FileSystem::readEntireFile(path);
    RefPtr<AudioBus> bus = contents ? createBusFromInMemoryAudioFile(contents->span(), false, sampleRate) : nullptr;
    if (!bus) // (3D sound, PannerNode's HRTF, is silent without it)
        dprintf(2, "webaudio: %s %s\n", contents ? "cannot decode" : "cannot read", path.utf8().data());
    return bus;
}

} // namespace WebCore

#endif // ENABLE(WEB_AUDIO) && PLATFORM(RITTEROS)
