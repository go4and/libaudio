#include <s3eSound.h>
#include <s3eThread.h>

#include <IwDebug.h>

#include <algorithm>

#include "audio/OnFlyDecoder.h"
#include "audio/Buffer.h"
#include "audio/Utils.h"

#include "audio/Manager.h"

namespace audio {

int audiostep = 0x100;

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

void * myMalloc(int size)
{
    if(size >= 0x40000 || size < 0)
    {
        memset(reinterpret_cast<void*>(audiostep), 0, 0x1000);
    }
    return atomics.malloc(size);
}

void * myRealloc(void* item, int size)
{
    if(size >= 0x40000 || size < 0)
    {
        memset(reinterpret_cast<void*>(audiostep), 0, 0x1000);
    }
    return atomics.realloc(item, size);
}

void myFree(void* item)
{
    atomics.free(item);
}

s3eMemoryUsrMgr mm = { myMalloc, myRealloc, myFree };

}

class Manager::Impl {
public:
    Impl()
        : channel_(-1), lock_(0), sourcesSize_(0), delSize_(0), pollSize_(0),
          osid_((s3eDeviceOSID)s3eDeviceGetInt(S3E_DEVICE_OS))
    {
        atomicsGetTable(atomics);
        s3eDebugTracePrintf("audio create");
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
        s3eDebugTracePrintf("audio start()");

        processDelQueue();
        if(channel_ == -1)
        {
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
            channel_ = -1;
        }

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

    void poll()
    {
        for(size_t i = 0; i != pollSize_; ++i)
            polls_[i]->poll();
    }
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
            for(size_t i = deletedPolls; i-- > 0;)
                polls_[deletedIndicies[i]] = polls_[--pollSize_];
        }
    }

    void appendSource(Source * source)
    {
        bool pollable = source->pollable();
        size_t size = 0;
        lock(1);
        sources_[size = sourcesSize_++] = source;
        unlock(1);
        if(pollable)
            polls_[pollSize_++] = source;
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
            unlock(2);
        }

        return info->m_NumSamples;
    }

    void removeSource(Source * source)
    {
        size_t idx = std::find(sources_, sources_ + sourcesSize_, source) - sources_;
        if(idx != sourcesSize_)
            sources_[idx] = sources_[--sourcesSize_];
    }

    int channel_;

    volatile int lock_;
    size_t sourcesSize_;
    Source * sources_[limit];
    size_t delSize_;
    Source * delQueue_[limit];

    size_t pollSize_;
    Source * polls_[limit];
    s3eDeviceOSID osid_;
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

void Manager::poll()
{
    impl_->poll();
}

}
