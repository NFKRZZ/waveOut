#pragma once

#include <string>
#include <vector>

namespace audiofile
{
    struct DecodedPcm16
    {
        std::vector<short> samples; // interleaved PCM16
        int sampleRate = 0;
        int channels = 0;
    };

    class AudioFileLoader
    {
    public:
        // Decodes an audio file (WAV/MP3/FLAC and any miniaudio-supported format)
        // into interleaved PCM16 at the file's native sample rate/channel count.
        static bool LoadPcm16(const std::string& path, DecodedPcm16& out, std::string* errorMessage = nullptr);
    };
}

