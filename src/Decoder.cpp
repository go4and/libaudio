#include <limits>

#include <s3eSound.h>

#include "audio/Buffer.h"
#include "audio/OggFile.h"
#include "audio/Speex.h"
#include "audio/Utils.h"

#include "audio/Decoder.h"

namespace audio {

Decoder::Decoder()
    : decodeBuffer_(new int16_t[decodeBufferSize])
{
}

Decoder::~Decoder()
{
    delete decodeBuffer_;
}

Buffer Decoder::decode(OggFile & file, int volume)
{
    int outputRate = s3eSoundGetInt(S3E_SOUND_OUTPUT_FREQ);
    int err = 0;
    SpeexResamplerState * resampler = speex_resampler_init(1, file.rate(), outputRate, 0, &err);

    buffer_.clear();
    size_t decodeUsed = 0;
    for(;;)
    {
        long res = file.read(decodeBuffer_, (decodeBufferSize - decodeUsed) * 2);
        decodeUsed += res / 2;
        if(decodeUsed)
            resample(resampler, decodeBuffer_, decodeUsed, buffer_);
        if(!res)
            break;
    }

    if(decodeUsed)
        resample(resampler, decodeBuffer_, decodeUsed, buffer_);

    speex_resampler_destroy(resampler);
    
    if(volume != 0x100)
    {
        typedef std::numeric_limits<int16_t> limits;
        int min = limits::min();
        int max = limits::max();
        for(std::vector<int16_t>::iterator i = buffer_.begin(), end = buffer_.end(); i != end; ++i)
            *i = std::max(min, std::min(max, *i * volume / 0x100));
    }

    return Buffer(reinterpret_cast<char*>(&buffer_[0]), buffer_.size() * 2);
}

}
