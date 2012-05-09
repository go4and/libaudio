#pragma once

#include <string>
#include <vector>

#include <atomics.h>

struct SpeexResamplerState_;
typedef struct SpeexResamplerState_ SpeexResamplerState;

namespace audio {

const size_t decodeBufferSize = 0x2000;

class Buffer;
class OggFile;

void resample(SpeexResamplerState * resampler, int16_t * buffer, size_t & filled, std::vector<int16_t> & out);
void mix(bool mix, int16_t * out, int16_t * inp, int volume, size_t samples);

extern AtomicFunctions atomics;

inline void atomicsWrite(volatile int * x, int v)
{
    int expected = 0;
    for(;;)
    {
        int old = atomics.cas(x, expected, v);
        if(old == expected)
            break;
        else
            expected = old;
    }
}

Buffer loadFile(const char * fname);
Buffer loadFile(const std::string & fname);

}
