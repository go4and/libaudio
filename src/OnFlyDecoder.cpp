#include <s3eSound.h>

#include <speex/speex_resampler.h>

#include "audio/File.h"
#include "audio/Utils.h"

#include "audio/OnFlyDecoder.h"

namespace audio {

const size_t bufferSize = 0x8000;

extern int audiostep;

int speex_resampler_process_intx(SpeexResamplerState *st, spx_uint32_t channel_index, 
                          const spx_int16_t *in, 
                          spx_uint32_t *in_len, 
                          spx_int16_t *out, 
                          spx_uint32_t *out_len)
{
    spx_uint32_t len = std::min(*in_len, *out_len);
    memcpy(out, in, len * 2);
    *in_len = len;
    *out_len = len;
    return 0;
}

class OnFlyDecoder::Impl {
public:
    Impl(File & source)
        : source_(source), resampler_(0), resamplerRate_(0),
          decodeBuffer_(new int16_t[decodeBufferSize]), decodeUsed_(0),
          begin_(new int16_t[bufferSize]), volume_(0x100)
    {
        memset(decodeBuffer_, 0, decodeBufferSize * 2);
        end_ = begin_ + bufferSize;
        reader_ = writer_ = reinterpret_cast<int>(begin_);
    }

    ~Impl()
    {
        delete [] begin_;
        delete [] decodeBuffer_;
        if(resampler_)
            speex_resampler_destroy(resampler_);
    }

    File & source()
    {
        return source_;
    }

    void createResampler()
    {
        int rate = source_.rate();
        int outputRate = s3eSoundGetInt(S3E_SOUND_OUTPUT_FREQ);

        if(resamplerRate_ != rate)
        {
            if(resampler_)
                speex_resampler_destroy(resampler_);
            int err = 0;
            resampler_ = speex_resampler_init(1, rate, outputRate, 0, &err);
        }
    }

    bool poll()
    {
        audiostep = 0x101;
        
        int16_t * writer = reinterpret_cast<int16_t*>(writer_);
        int16_t * reader = reinterpret_cast<int16_t*>(atomics.cas(&reader_, 0, 0));
        if(reader == begin_)
            reader = end_ - 1;
        else
            --reader;

        audiostep = 0x102;
        decode();

        audiostep = 0x103;
        if(reader == writer)
            return false;
        int avail = reader > writer ? reader - writer : (end_ - writer) + (reader - begin_);
        if(resampler_ == 0)
            createResampler();

        audiostep = 0x104;
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
        
        audiostep = 0x105;
        if(start != decodeBuffer_)
        {
            decodeUsed_ = stop - start;
            memmove(decodeBuffer_, start, decodeUsed_ * 2);
        }

        audiostep = 0x106;
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
            long res = source_.read(decodeBuffer_ + decodeUsed_, (decodeBufferSize - decodeUsed_) * 2);
            if(res == 0)
                source_.rewind();
            else
                decodeUsed_ += res / 2;
        }
    }

    File & source_;
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

OnFlyDecoder::OnFlyDecoder(bool owned, File & file)
    : Source(owned), impl_(new Impl(file))
{
}

OnFlyDecoder::~OnFlyDecoder()
{
}

File & OnFlyDecoder::source()
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
