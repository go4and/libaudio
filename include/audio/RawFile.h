#pragma once

#include "audio/Buffer.h"
#include "audio/File.h"

namespace audio {

class RawFile : public File {
public:
    RawFile(const Buffer & buffer, int rate = s3eSoundGetInt(S3E_SOUND_OUTPUT_FREQ))
        : buffer_(buffer), pos_(buffer.data()), rate_(rate)
    {
    }

    long read(void * out, size_t len)
    {
        const char * end = buffer_.data() + buffer_.size();
        len = std::min<size_t>(len, end - pos_) & ~1;
        memcpy(out, pos_, len);
        pos_ += len;
        return len;
    }

    int rate() { return rate_; }
    void rewind() { pos_ = buffer_.data(); }
private:
    Buffer buffer_;
    const char * pos_;
    int rate_;
};

}
