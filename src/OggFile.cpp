#include <IwDebug.h>

#include <vorbis/vorbisfile.h>

#include "audio/Buffer.h"

#include "audio/OggFile.h"

namespace audio {

namespace {

struct OVMemoryBuffer {
    const char * begin;
    const char * end;
    const char * pos;
};

size_t ovMemoryRead(void *ptr, size_t size, size_t nmemb, void *datasource)
{
    OVMemoryBuffer * buffer = static_cast<OVMemoryBuffer*>(datasource);
    size_t result = std::min<size_t>(size * nmemb, buffer->end - buffer->pos) / size;
    size = result * size;
    memcpy(ptr, buffer->pos, size);
    buffer->pos += size;

    return result;
}

int ovMemorySeek(void *datasource, ogg_int64_t offset, int whence)
{
    OVMemoryBuffer * buffer = static_cast<OVMemoryBuffer*>(datasource);
    switch(whence) {
    case 0:
        buffer->pos = buffer->begin + offset;
        break;
    case 1:
        buffer->pos += offset;
        break;
    case 2:
        buffer->pos = buffer->end + offset;
        break;
    }
    return 0;
}

int ovMemoryClose(void *datasource)
{
    return 0;
}

long ovMemoryTell(void *datasource)
{
    OVMemoryBuffer * buffer = static_cast<OVMemoryBuffer*>(datasource);

    long result = buffer->pos - buffer->begin;
    return result;
}

ov_callbacks ovMemoryCallbacks = {
    ovMemoryRead,
    ovMemorySeek,
    ovMemoryClose,
    ovMemoryTell,
};

}

class OggFile::Impl {
public:
    Impl(const Buffer & buffer)
        : data_(buffer)
    {
        memset(&vf_, 0, sizeof(vf_));
        buffer_.begin = data_.data();
        buffer_.end = buffer_.begin + data_.size();
        buffer_.pos = data_.data();
    }

    ~Impl()
    {
        ov_clear(&vf_);
    }

    long read(void * out, size_t len)
    {
        int bitstream = -1;
        long result = ov_read(handle(), static_cast<char*>(out), len, 0, 2, 1, &bitstream);
        return result;
    }

    OggVorbis_File * handle()
    {
        if(vf_.datasource == 0)
        {
            int res = ov_open_callbacks(&buffer_, &vf_, 0, 0, ovMemoryCallbacks);
            IwAssertMsg(AUDIO_OGGFILE, res >= 0, ("Failed to open ogg stream: %d", res));
            
            vorbis_info * info = ov_info(handle(), -1);
            s3eDebugTracePrintf("rate: %d", static_cast<int>(info->rate));
        }
        return &vf_;
    }
private:
    Buffer data_;
    OVMemoryBuffer buffer_;
    OggVorbis_File vf_;
};

OggFile::OggFile(const Buffer & buffer)
    : impl_(new Impl(buffer))
{
}

long OggFile::read(void * out, size_t len)
{
    return impl_->read(out, len);
}

OggVorbis_File * OggFile::handle()
{
    return impl_->handle();
}

OggFile::~OggFile()
{
}

int OggFile::rate()
{
    return ov_info(handle(), -1)->rate;
}

void OggFile::rewind()
{
    ov_raw_seek(handle(), 0);
}

}
