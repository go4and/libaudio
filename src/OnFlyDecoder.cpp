#include <s3eSound.h>

#include "audio/OggFile.h"
#include "audio/Speex.h"
#include "audio/Utils.h"

#include "audio/OnFlyDecoder.h"

namespace audio {

const size_t bufferSize = 0x8000;

class OnFlyDecoder::Impl {
public:
    Impl(int channel)
        : channel_(channel),
          source_(0), resampler_(0), resamplerRate_(0),
          decodeBuffer_(new int16_t[decodeBufferSize]), decodeUsed_(0),
          begin_(new int16_t[bufferSize]), volume_(0x100)
    {
        end_ = begin_ + bufferSize;
        reader_ = writer_ = reinterpret_cast<int>(begin_);

        s3eSoundChannelRegister(channel_, S3E_CHANNEL_GEN_AUDIO, &Impl::genAudio, this);
    }

    ~Impl()
    {
        delete [] begin_;
        delete [] decodeBuffer_;
        if(resampler_)
            speex_resampler_destroy(resampler_);
    }

    OggFile * source()
    {
        return source_;
    }

    void reset(OggFile * source)
    {
        if(source == source_)
            return;
        
        source_ = source;

        if(source_)
        {
            int rate = source_->rate();
            int outputRate = s3eSoundGetInt(S3E_SOUND_OUTPUT_FREQ);

            if(resamplerRate_ != rate)
            {
                if(resampler_)
                    speex_resampler_destroy(resampler_);
                int err = 0;
                resampler_ = speex_resampler_init(1, rate, outputRate, 0, &err);
            }

            int16 dummy[8];
            memset(dummy, 0, sizeof(dummy));
            s3eSoundChannelPlay(channel_, dummy, sizeof(dummy) / sizeof(dummy[0]), 0, 0);
        } else {
            s3eSoundChannelStop(channel_);
/*            while(s3eSoundChannelGetInt(channel_, S3E_CHANNEL_STATUS))
                atomics.sched_yield();*/
        }
    }

    bool poll()
    {
        if(!source_)
            return false;
        spx_int16_t * writer = reinterpret_cast<spx_int16_t*>(writer_);
        spx_int16_t * reader = reinterpret_cast<spx_int16_t*>(atomics.cas(&reader_, 0, 0));
        if(reader == begin_)
            reader = end_ - 1;
        else
            --reader;
        while(decodeUsed_ <= decodeBufferSize / 2)
        {
            long res = source_->read(decodeBuffer_ + decodeUsed_, decodeBufferSize - decodeUsed_);
            if(res == 0)
                source_->rewind();
            else
                decodeUsed_ += res / 2;
        }
        if(reader == writer)
            return false;
        spx_int16_t * start = decodeBuffer_;
        spx_int16_t * stop = start + decodeUsed_;
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

    static int32 genAudio(void* systemData, void* userData)
    {
        return static_cast<Impl*>(userData)->doGenAudio(static_cast<s3eSoundGenAudioInfo*>(systemData));
    }

    void volume(int value)
    {
        volume_ = value;
    }
private:
    int32 doGenAudio(s3eSoundGenAudioInfo * info)
    {
        spx_int16_t * reader = reinterpret_cast<spx_int16_t*>(reader_);
        spx_int16_t * writer = reinterpret_cast<spx_int16_t*>(atomics.cas(&writer_, 0, 0));
        if(writer == reader)
            return 0;
        size_t ready = reader < writer ? writer - reader : (end_ - reader) + (writer - begin_);
        size_t result = std::min<size_t>(std::min<size_t>(ready, info->m_NumSamples), 1024);
        if(reader < writer)
            mix(info->m_Mix, info->m_Target, reader, volume_, result);
        else {
            size_t tailSize = std::min<size_t>(end_ - reader, result);
            mix(info->m_Mix, info->m_Target, reader, volume_, tailSize);
            if(result > tailSize)
                mix(info->m_Mix, info->m_Target, begin_, volume_, result - tailSize);
        }
        reader += result;
        if(reader >= end_)
            reader = begin_ + (reader - end_);
        atomics.add(&reader_, reinterpret_cast<int>(reader) - reader_);
        return result;
    }

    int channel_;
    OggFile * source_;
    SpeexResamplerState * resampler_;
    int resamplerRate_;

    spx_int16_t * decodeBuffer_;
    size_t decodeUsed_;
    spx_int16_t * begin_;
    spx_int16_t * end_;
    volatile int reader_;
    volatile int writer_;
    int volume_;
};

OnFlyDecoder::OnFlyDecoder(int channel)
    : impl_(new Impl(channel))
{
}

OnFlyDecoder::~OnFlyDecoder()
{
}

OggFile * OnFlyDecoder::source()
{
    return impl_->source();
}

void OnFlyDecoder::reset(OggFile * source)
{
    impl_->reset(source);
}

bool OnFlyDecoder::poll()
{
    return impl_->poll();
}

void OnFlyDecoder::volume(int value)
{
    impl_->volume(value);
}

}
