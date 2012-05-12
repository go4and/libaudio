#include <s3eSound.h>

#include <speex/speex_resampler.h>

#include "audio/OggFile.h"
#include "audio/Utils.h"

#include "audio/OnFlyDecoder.h"

namespace audio {

const size_t bufferSize = 0x8000;

class OnFlyDecoder::Impl {
public:
    Impl(OggFile & source)
        : source_(source), resampler_(0), resamplerRate_(0),
          decodeBuffer_(new int16_t[decodeBufferSize]), decodeUsed_(0),
          begin_(new int16_t[bufferSize]), volume_(0x100)
    {
        end_ = begin_ + bufferSize;
        reader_ = writer_ = reinterpret_cast<int>(begin_);

        int rate = source.rate();
        int outputRate = s3eSoundGetInt(S3E_SOUND_OUTPUT_FREQ);

        if(resamplerRate_ != rate)
        {
            if(resampler_)
                speex_resampler_destroy(resampler_);
            int err = 0;
            resampler_ = speex_resampler_init(1, rate, outputRate, 0, &err);
        }
    }

    ~Impl()
    {
        delete [] begin_;
        delete [] decodeBuffer_;
        if(resampler_)
            speex_resampler_destroy(resampler_);
    }

    OggFile & source()
    {
        return source_;
    }

    bool poll()
    {
        int16_t * writer = reinterpret_cast<int16_t*>(writer_);
        int16_t * reader = reinterpret_cast<int16_t*>(atomics.cas(&reader_, 0, 0));
        if(reader == begin_)
            reader = end_ - 1;
        else
            --reader;
        decode();
        if(reader == writer)
            return false;
        int16_t * start = decodeBuffer_;
        int16_t * stop = start + decodeUsed_;
        if(reader > writer)
        {
            uint32_t inlen = stop - start;
            uint32_t outlen = reader - writer;
            speex_resampler_process_int(resampler_, 0, start, &inlen, writer, &outlen);
            start += inlen;
            writer += outlen;
        } else if(reader < writer)
        {
            uint32_t inlen = stop - start;
            uint32_t outlen = end_ - writer;
            speex_resampler_process_int(resampler_, 0, start, &inlen, writer, &outlen);
            start += inlen;
            writer += outlen;
            if(writer == end_)
                writer = begin_;
            if(start != stop && writer < reader)
            {
                inlen = stop - start;
                outlen = reader - writer;
                speex_resampler_process_int(resampler_, 0, start, &inlen, writer, &outlen);
                start += inlen;
                writer += outlen;
            }
        }

        if(start != decodeBuffer_)
        {
            decodeUsed_ = stop - start;
            memmove(decodeBuffer_, start, decodeUsed_ * 2);
        }

        atomics.add(&writer_, reinterpret_cast<int>(writer) - writer_);

        return true;
    }

    void volume(int value)
    {
        volume_ = value;
    }

    int mix(int16_t * out, int limit)
    {
        int16_t * reader = reinterpret_cast<int16_t*>(reader_);
        int16_t * writer = reinterpret_cast<int16_t*>(atomics.cas(&writer_, 0, 0));
        if(writer == reader)
            return 0;
        size_t ready = reader < writer ? writer - reader : (end_ - reader) + (writer - begin_);
        size_t result = std::min<size_t>(ready, limit);
        if(reader < writer)
            audio::mix(true, out, reader, volume_, result);
        else {
            size_t tailSize = std::min<size_t>(end_ - reader, result);
            audio::mix(true, out, reader, volume_, tailSize);
            if(result > tailSize)
                audio::mix(true, out + tailSize, begin_, volume_, result - tailSize);
        }
        reader += result;
        if(reader >= end_)
            reader = begin_ + (reader - end_);
        atomics.add(&reader_, reinterpret_cast<int>(reader) - reader_);
        return result;
    }
private:
    void decode()
    {
        while(decodeUsed_ <= decodeBufferSize / 2)
        {
            long res = source_.read(decodeBuffer_ + decodeUsed_, decodeBufferSize - decodeUsed_);
            if(res == 0)
            {
                source_.rewind();
            } else
                decodeUsed_ += res / 2;
        }
    }

    OggFile & source_;
    SpeexResamplerState * resampler_;
    int resamplerRate_;

    int16_t * decodeBuffer_;
    size_t decodeUsed_;
    int16_t * begin_;
    int16_t * end_;
    volatile int reader_;
    volatile int writer_;
    int volume_;
};

OnFlyDecoder::OnFlyDecoder(bool owned, OggFile & file)
    : Source(owned), impl_(new Impl(file))
{
}

OnFlyDecoder::~OnFlyDecoder()
{
}

OggFile & OnFlyDecoder::source()
{
    return impl_->source();
}

bool OnFlyDecoder::poll()
{
    return impl_->poll();
}

void OnFlyDecoder::volume(int value)
{
    impl_->volume(value);
}

int OnFlyDecoder::mix(int16_t * out, int limit)
{
    return impl_->mix(out, limit);
}

}
