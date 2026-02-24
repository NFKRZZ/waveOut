#include "AudioFileLoader.h"

#include <algorithm>
#include <cstdint>
#include <sstream>

#if __has_include("third_party/miniaudio.h")
#include "third_party/miniaudio.h"
#define WAVEOUT_HAS_MINIAUDIO_DECODER 1
#elif __has_include("miniaudio.h")
#include "miniaudio.h"
#define WAVEOUT_HAS_MINIAUDIO_DECODER 1
#else
#define WAVEOUT_HAS_MINIAUDIO_DECODER 0
#endif

namespace audiofile
{
    namespace
    {
        static void SetError(std::string* outErr, const std::string& msg)
        {
            if (outErr) *outErr = msg;
        }
    }

    bool AudioFileLoader::LoadPcm16(const std::string& path, DecodedPcm16& out, std::string* errorMessage)
    {
        out = {};

#if !WAVEOUT_HAS_MINIAUDIO_DECODER
        SetError(errorMessage, "miniaudio.h not found; cannot decode MP3/FLAC/WAV.");
        return false;
#else
        ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, 0, 0);
        ma_decoder decoder{};
        const ma_result initRes = ma_decoder_init_file(path.c_str(), &cfg, &decoder);
        if (initRes != MA_SUCCESS)
        {
            std::ostringstream oss;
            oss << "ma_decoder_init_file failed (" << static_cast<int>(initRes) << ") for: " << path;
            SetError(errorMessage, oss.str());
            return false;
        }

        ma_format fmt = ma_format_unknown;
        ma_uint32 ch = 0;
        ma_uint32 sr = 0;
        if (ma_decoder_get_data_format(&decoder, &fmt, &ch, &sr, nullptr, 0) != MA_SUCCESS)
        {
            ma_decoder_uninit(&decoder);
            SetError(errorMessage, "ma_decoder_get_data_format failed.");
            return false;
        }

        if (fmt != ma_format_s16 || ch == 0 || sr == 0)
        {
            ma_decoder_uninit(&decoder);
            SetError(errorMessage, "Decoder did not produce valid PCM16 output.");
            return false;
        }

        out.sampleRate = static_cast<int>(sr);
        out.channels = static_cast<int>(ch);

        ma_uint64 totalFrames = 0;
        const ma_result lenRes = ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
        const std::size_t channels = static_cast<std::size_t>((std::max)(1u, ch));

        if (lenRes == MA_SUCCESS && totalFrames > 0)
        {
            out.samples.resize(static_cast<std::size_t>(totalFrames) * channels);

            ma_uint64 framesReadTotal = 0;
            while (framesReadTotal < totalFrames)
            {
                ma_uint64 framesRead = 0;
                const ma_result readRes = ma_decoder_read_pcm_frames(
                    &decoder,
                    out.samples.data() + static_cast<std::size_t>(framesReadTotal) * channels,
                    totalFrames - framesReadTotal,
                    &framesRead);
                if (readRes != MA_SUCCESS && readRes != MA_AT_END)
                {
                    out.samples.clear();
                    break;
                }
                if (framesRead == 0)
                    break;
                framesReadTotal += framesRead;
            }

            if (framesReadTotal == 0)
            {
                out.samples.clear();
            }
            else if (framesReadTotal < totalFrames)
            {
                out.samples.resize(static_cast<std::size_t>(framesReadTotal) * channels);
            }
        }
        else
        {
            constexpr ma_uint64 kChunkFrames = 4096;
            std::vector<short> temp(static_cast<std::size_t>(kChunkFrames) * channels);
            for (;;)
            {
                ma_uint64 framesRead = 0;
                const ma_result readRes = ma_decoder_read_pcm_frames(&decoder, temp.data(), kChunkFrames, &framesRead);
                if (readRes != MA_SUCCESS && readRes != MA_AT_END)
                {
                    out.samples.clear();
                    break;
                }
                if (framesRead == 0)
                    break;
                const std::size_t samplesRead = static_cast<std::size_t>(framesRead) * channels;
                out.samples.insert(out.samples.end(), temp.begin(), temp.begin() + static_cast<std::ptrdiff_t>(samplesRead));
            }
        }

        ma_decoder_uninit(&decoder);

        if (out.samples.empty())
        {
            SetError(errorMessage, "Decoded audio contained no samples.");
            return false;
        }
        return true;
#endif
    }
}
