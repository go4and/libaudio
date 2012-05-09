#include <s3eSound.h>
#include <s3eThread.h>

#include <IwDebug.h>

#include "audio/OnFlyDecoder.h"
#include "audio/Buffer.h"
#include "audio/Utils.h"

#include "audio/Manager.h"

namespace audio {

class Manager::Impl {
public:
    Impl()
        : numChannels_(s3eSoundGetInt(S3E_SOUND_NUM_CHANNELS)),
          loop_(0), buffer1_(0), buffer2_(1),
          channels_(numChannels_ - 2)
    {
        atomicsGetTable(atomics);

        for(std::vector<ChannelState>::iterator i = channels_.begin(), end = channels_.end(); i != end; ++i)
        {
            i->channel = numChannels_ - channels_.size() + (i - channels_.begin());
            i->sample = 0;
            s3eSoundChannelRegister(i->channel, S3E_CHANNEL_GEN_AUDIO, &Impl::genAudio, this);
        }

        thread_ = s3eThreadCreate(&Impl::executeStatic, this);
    }

    ~Impl()
    {
        atomicsWrite(&loop_, -2);
        s3eThreadJoin(thread_);
    }

    bool play(const Buffer & sample)
    {
        for(std::vector<ChannelState>::iterator i = channels_.begin(), end = channels_.end(); i != end; ++i)
            if(!s3eSoundChannelGetInt(i->channel, S3E_CHANNEL_STATUS))
            {
                i->sample = reinterpret_cast<int>(&sample);
                i->pos = 0;

                int16 dummy[8];
                memset(dummy, 0, sizeof(dummy));
                s3eResult res = s3eSoundChannelPlay(i->channel, dummy, 8, 1, 0);
                IwAssertMsg(AUDIO_MANAGER, res == S3E_RESULT_SUCCESS, ("Failed to start play: %d", res));
                return true;
            }
        return false;
    }

    void loopVolume(int value)
    {
        buffer1_.volume(value);
        buffer2_.volume(value);
    }

    void loop(OggFile * file)
    {
        atomicsWrite(&loop_, file ? reinterpret_cast<int>(file) : -1);
    }
private:
    static int32 genAudio(void* systemData, void* userData)
    {
        return static_cast<Impl*>(userData)->doGenAudio(static_cast<s3eSoundGenAudioInfo*>(systemData));
    }

    int32 doGenAudio(s3eSoundGenAudioInfo * info)
    {
        ChannelState & state = channels_[info->m_Channel - (numChannels_ - channels_.size())];
            
        const Buffer * sample = reinterpret_cast<const Buffer *>(state.sample);
        size_t len = sample->size() / 2;
        int32 result = std::min<size_t>(len - state.pos, info->m_NumSamples);
        mix(info->m_Mix, info->m_Target, reinterpret_cast<int16_t*>(sample->data()) + state.pos, 0x100, result);
        state.pos += result;
        info->m_EndSample = result == 0;
        return result;
    }

    static void * executeStatic(void * self)
    {
        static_cast<Impl*>(self)->execute();
        return 0;
    }

    void execute()
    {
        for(;;)
        {
            {
                int newLoop, expected = 0;
                for(;;)
                {
                    newLoop = atomics.cas(&loop_, expected, 0);
                    if(newLoop == expected)
                        break;
                    else
                        expected = newLoop;
                }
                if(newLoop)
                {
                    if(newLoop == -2)
                        break;
                    OggFile * file = newLoop != -1 ? reinterpret_cast<OggFile*>(newLoop) : 0;
                    if(buffer1_.source())
                    {
                        if(buffer1_.source() != file)
                        {
                            buffer1_.reset(0);
                            buffer2_.reset(file);
                        }
                    } else {
                        if(buffer2_.source() != file)
                        {
                            buffer1_.reset(file);
                            buffer2_.reset(0);
                        }
                    }
                }
            }

            if(!buffer1_.poll() && !buffer2_.poll())
            {
                timespec ts;
                ts.tv_sec = 0;
                ts.tv_nsec = 10000000;
                atomics.nanosleep(&ts, 0);
            } else
                atomics.sched_yield();
        }
    }

    s3eThread * thread_;
    int numChannels_;

    volatile int loop_;
    
    OnFlyDecoder buffer1_;
    OnFlyDecoder buffer2_;
    
    struct ChannelState {
        int channel;
        volatile int sample;
        size_t pos;
    };

    std::vector<ChannelState> channels_;
};

Manager::Manager()
    : impl_(new Impl)
{
}

Manager::~Manager()
{
}

bool Manager::play(const Buffer & sample)
{
    return impl_->play(sample);
}

void Manager::loop(OggFile * file)
{
    impl_->loop(file);
}

void Manager::loopVolume(int volume)
{
    impl_->loopVolume(volume);
}

}
