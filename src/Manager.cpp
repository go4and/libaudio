#include <s3eSound.h>
#include <s3eThread.h>

#include <IwDebug.h>

#include <algorithm>

#include "audio/OnFlyDecoder.h"
#include "audio/Buffer.h"
#include "audio/Utils.h"

#include "audio/Manager.h"

namespace audio {

namespace {

const size_t limit = 0x20;

class BufferSource : public Source {
public:
    explicit BufferSource(bool owned, const Buffer & buffer)
        : Source(owned), buffer_(buffer), pos_(0)
    {
    }

    bool pollable() { return false; }
    
    int mix(int16_t * out, int limit)
    {
        int left = buffer_.size() / 2 - pos_;
        if(!left)
            return -1;
        int result = std::min<int>(limit, left);
        audio::mix(true, out, reinterpret_cast<int16_t*>(buffer_.data()) + pos_, 0x100, result);
        pos_ += result;
        return result;
    }
private:
    Buffer buffer_;
    int pos_;
};

}

class Manager::Impl {
public:
    Impl()
        : thread_(0), channel_(-1), lock_(0), sourcesSize_(0), delSize_(0), pollVersion_(0), pollSize_(0), mainThread_(s3eThreadGetCurrent())
    {
        atomicsGetTable(atomics);
    }

    ~Impl()
    {
        stop(true);

        for(size_t i = 0; i != sourcesSize_; ++i)
            if(sources_[i]->owned())
                delete sources_[i];

        timespec ts;
        ts.tv_sec = 1;
        ts.tv_nsec = 0;
        atomics.nanosleep(&ts, 0);
    }

    void start()
    {
        processDelQueue();
        if(channel_ == -1)
        {
            pollVersion_ = 0;
            thread_ = s3eThreadCreate(&Impl::executeStatic, this);
            channel_ = s3eSoundGetFreeChannel();
            s3eSoundChannelRegister(channel_, S3E_CHANNEL_GEN_AUDIO, &Impl::genAudio, this);

            int16 dummy[8];
            memset(dummy, 0, sizeof(dummy));
            s3eSoundChannelPlay(channel_, dummy, sizeof(dummy) / sizeof(dummy[0]), 1, 0);
        } else
            IwAssertMsg(AUDIO_MANAGER, false, ("start on started audio manager"));
    }

    void stop(bool waitStop = false)
    {
        s3eDebugTracePrintf("audio::Manager::stop(%d)", static_cast<int>(waitStop));

        processDelQueue();
        
        if(channel_ != -1)
        {
            atomicsWrite(&pollVersion_, -1);

            s3eSoundChannelUnRegister(channel_, S3E_CHANNEL_GEN_AUDIO);
            s3eSoundChannelStop(channel_);
            if(waitStop)
                for(int j = 0; j != 1000 && s3eSoundChannelGetInt(channel_, S3E_CHANNEL_STATUS); ++j)
                {
                    timespec ts;
                    ts.tv_sec = 0;
                    ts.tv_nsec = 10000000;
                    atomics.nanosleep(&ts, 0);
                }
            s3eThreadJoin(thread_);
            thread_ = 0;
            channel_ = -1;
        } else
            IwAssertMsg(AUDIO_MANAGER, false, ("stop on stopped audio manager"));

        s3eDebugTracePrintf("audio::Manager::stop, done");
    }

    void stop(Source * source)
    {
        processDelQueue(source);
    }

    Source * play(Source * source)
    {
        processDelQueue();
        if(sourcesSize_ < limit)
        {
            appendSource(source);
            return source;
        } else {
            if(source->owned())
                delete source;
            return 0;
        }
    }

    Source * play(const Buffer & sample)
    {
        processDelQueue();
        if(sourcesSize_ < limit)
        {
            Source * result = new BufferSource(true, sample);
            appendSource(result);
            return result;
        } else
            return 0;
    }

/*    void loopVolume(int value)
    {
        buffer1_.volume(value);
        buffer2_.volume(value);
    }

    void loop(OggFile * file)
    {
        atomicsWrite(&loop_, file ? reinterpret_cast<int>(file) : -1);
    }*/
private:
    void lock(int id)
    {
        while(atomics.cas(&lock_, 0, id))
            atomics.sched_yield();
    }

    void unlock(int id)
    {
        int res = atomics.cas(&lock_, id, 0);
        IwAssertMsg(AUDIO_MANAGER, res == id, ("Invalid lock state: %d", res));
    }
    
    static int32 processDelQueueCB(void * system, void * user)
    {
        static_cast<Impl*>(user)->processDelQueue();
        return 0;
    }

    void processDelQueue(Source * source = 0)
    {
        size_t size;
        Source * queue[limit + 1];
        lock(1);
        size = delSize_;
        delSize_ = 0;
        memcpy(queue, delQueue_, size * sizeof(queue[0]));
        if(source)
            removeSource(source);
        bool empty = !sourcesSize_;
        unlock(1);
        
        if(source)
            queue[size++] = source;
        // TODO owned cannot be mixed with pollable

        size_t deletedPolls = 0;
        size_t deletedIndicies[limit];

        for(size_t i = 0; i != size; ++i)
        {
            size_t idx = std::find(polls_, polls_ + pollSize_, queue[i]) - polls_;
            if(idx != pollSize_)
                deletedIndicies[deletedPolls++] = idx;
            if(queue[i]->owned())
                delete queue[i];
        }

        if(deletedPolls)
        {
            std::sort(deletedIndicies, deletedIndicies + deletedPolls);
            lock(1);
            for(size_t i = deletedPolls; i-- > 0;)
                polls_[deletedIndicies[i]] = polls_[--pollSize_];
            unlock(1);
            atomics.add(&pollVersion_, 1);
        }
    }

    void appendSource(Source * source)
    {
        bool pollable = source->pollable();
        size_t size = 0;
        lock(1);
        sources_[size = sourcesSize_++] = source;
        if(pollable)
            polls_[pollSize_++] = source;
        unlock(1);
        if(pollable)
            atomics.add(&pollVersion_, 1);
    }

    static int32 genAudio(void* systemData, void* userData)
    {
        return static_cast<Impl*>(userData)->doGenAudio(static_cast<s3eSoundGenAudioInfo*>(systemData));
    }

    int32 doGenAudio(s3eSoundGenAudioInfo * info)
    {
        size_t size;
        Source * sources[limit];
        {
            lock(2);
            size = sourcesSize_;
            memcpy(sources, sources_, size * sizeof(*sources));
            unlock(2);
        }

        if(!info->m_Mix)
            memset(info->m_Target, 0, info->m_NumSamples * 2);

        size_t delSize = 0;
        Source * del[limit];

        int result = 0;
        for(size_t i = 0; i != size; ++i)
        {
            int current = sources[i]->mix(info->m_Target, info->m_NumSamples);
            if(current == -1)
            {
                del[delSize++] = sources[i];
            } else
                result = std::max(result, current);
        }

        if(delSize)
        {
            lock(2);
            delSize = std::min(delSize, limit - delSize_);
            for(size_t j = 0; j != delSize; ++j)
                removeSource(del[j]);
            memcpy(delQueue_ + delSize_, del, delSize * sizeof(del[0]));
            delSize_ += delSize;
            bool empty = !sourcesSize_;
            unlock(2);
        }

        if(result)
            s3eDebugTracePrintf("generated audio: %d of %d\n", result, info->m_NumSamples);

        return info->m_NumSamples;
    }

    void removeSource(Source * source)
    {
        size_t idx = std::find(sources_, sources_ + sourcesSize_, source) - sources_;
        if(idx != sourcesSize_)
            sources_[idx] = sources_[--sourcesSize_];
    }

    static void * executeStatic(void * self)
    {
        static_cast<Impl*>(self)->execute();
        return 0;
    }

    void execute()
    {
        int pollVersion = -1;
        size_t pollSize = 0;
        Source * polls[limit];
        for(;;)
        {
            {
                int newPollVersion = atomics.cas(&pollVersion_, 0, 0);
                if(newPollVersion != pollVersion)
                {
                    if(newPollVersion == -1)
                        break;
                    lock(3);
                    pollSize = pollSize_;
                    memcpy(polls, polls_, pollSize * sizeof(polls[0]));
                    unlock(3);
                    pollVersion = newPollVersion;
                }
            }

            bool worked = false;
            for(size_t i = 0; i != pollSize; ++i)
                worked = polls[i]->poll() || worked;
            if(!worked)
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
    int channel_;

    volatile int lock_;
    size_t sourcesSize_;
    Source * sources_[limit];
    size_t delSize_;
    Source * delQueue_[limit];

    volatile int pollVersion_;
    size_t pollSize_;
    Source * polls_[limit];
    s3eThread * mainThread_;
};

Manager::Manager()
    : impl_(new Impl)
{
}

Manager::~Manager()
{
}

Source * Manager::play(const Buffer & sample)
{
    return impl_->play(sample);
}

Source * Manager::play(Source * source)
{
    return impl_->play(source);
}

void Manager::stop(Source * source)
{
    impl_->stop(source);
}

void Manager::start()
{
    impl_->start();
}

void Manager::stop()
{
    impl_->stop();
}

}
