#include <limits>

#include <s3eSound.h>

#include "speex/speex_resampler.h"

#include "audio/Buffer.h"
#include "audio/OggFile.h"
#include "audio/Utils.h"

#include "audio/Decoder.h"

namespace audio {

namespace {

const size_t decodeBufferSize = 0x2000;

}

Decoder::Decoder()
    : decodeBuffer_(new int16_t[decodeBufferSize])
{
}

Decoder::~Decoder()
{
    delete decodeBuffer_;
}

void decodeFile(SpeexResamplerState * resampler, OggFile & file, int16_t * decodeBuffer, std::vector<int16_t> & out)
{
    size_t decodeUsed = 0;
    for(;;)
    {
        long res = 1;
        while(decodeUsed * 2 < decodeBufferSize)
        {
            int request = (decodeBufferSize - decodeUsed) * 2;
            if(!request)
            {
                res = 1;
                break;
            }
            res = file.read(decodeBuffer + decodeUsed, request);
            if(res <= 0)
                break;
            decodeUsed += res / 2;
        }

        if(decodeUsed)
            resample(resampler, decodeBuffer, decodeUsed, out);

        if(res <= 0)
            break;
    }

    if(decodeUsed)
        resample(resampler, decodeBuffer, decodeUsed, out);
}

Buffer Decoder::decode(OggFile & file, int volume)
{
    int outputRate = s3eSoundGetInt(S3E_SOUND_OUTPUT_FREQ);

    int err = 0;
    int inputRate = file.rate();

    SpeexResamplerState * resampler = speex_resampler_init(1, inputRate, outputRate, 0, &err);

    buffer_.clear();
    decodeFile(resampler, file, decodeBuffer_, buffer_);

    speex_resampler_destroy(resampler);
    
    if(volume != 0x100)
    {
        typedef std::numeric_limits<int16_t> limits;
        int min = limits::min();
        int max = limits::max();
        for(std::vector<int16_t>::iterator i = buffer_.begin(), end = buffer_.end(); i != end; ++i)
            *i = std::max(min, std::min(max, *i * volume / 0x100));
    }

    const int16_t * data = &buffer_[0];
    size_t size = buffer_.size();
    return Buffer(reinterpret_cast<const char*>(data), size * 2);
}

}
