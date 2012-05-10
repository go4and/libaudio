#include <limits>

#include <s3eFile.h>

#include <IwDebug.h>

#include "audio/Buffer.h"
#include "audio/Speex.h"

#include "audio/Utils.h"

namespace audio {

AtomicFunctions atomics;

void resample(SpeexResamplerState * resampler, int16_t * buffer, size_t & filled, std::vector<int16_t> & out)
{
    spx_uint32_t inputRate, outputRate;
    speex_resampler_get_rate(resampler, &inputRate, &outputRate);

    spx_uint32_t inlen = filled;
    size_t oldSize = out.size();
    out.resize(oldSize + (static_cast<int64_t>(inlen) * outputRate / inputRate) + 1); 
    out.resize(out.capacity());
    spx_uint32_t outlen = out.size() - oldSize;
    speex_resampler_process_int(resampler, 0, buffer, &inlen, &out[oldSize], &outlen);
    out.resize(oldSize + outlen);
    memmove(buffer, buffer + inlen, (filled - inlen) * 2);
    filled -= inlen;
}

inline int combine(int a, int b)
{
    return (a + b);//  - a * b / 0x8000;
}

inline int clamp(int v, int min, int max)
{
    if(v < min)
        return min;
    if(v > max)
        return max;
    return v;
}

void mix(bool mix, int16_t * out, int16_t * inp, int volume, size_t samples)
{
    if(!mix)
    {
        if(volume == 0x100)
            memcpy(out, inp, samples * 2);
        else {
            typedef std::numeric_limits<int16_t> limits;
            int min = limits::min();
            int max = limits::max();
            for(size_t i = 0; i != samples; ++i)
                out[i] = clamp(inp[i] * volume / 0x100, min, max);
        }
    } else {
        typedef std::numeric_limits<int16_t> limits;
        int min = limits::min();
        int max = limits::max();
        if(volume == 0x100)
        {
            for(size_t i = 0; i != samples; ++i)
                out[i] = clamp(combine(out[i], inp[i]), min, max);
        } else {
            for(size_t i = 0; i != samples; ++i)
                out[i] = clamp(combine(out[i], inp[i] * volume / 0x100), min, max);
        }
    }
}

Buffer loadFile(const char * fname)
{
    s3eFile * file = s3eFileOpen(fname, "rb");
    if(file)
    {
        size_t size = s3eFileGetSize(file);
        Buffer result(size);
        s3eFileRead(result.data(), 1, size, file);
        s3eFileClose(file);
        return result;
    } else {
        IwAssertMsg(AUDIO_UTILS, false, ("failed to open: %s", fname));
        return Buffer();
    }
}

Buffer loadFile(const std::string & fname)
{
    return loadFile(fname.c_str());
}

}
