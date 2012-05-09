#include <IwDebug.h>

#include <vorbis/codec.h>
#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/vorbisfile.h>

#include "audio/Buffer.h"

#include "audio/OggFile.h"

namespace audio {

namespace {

struct OVMemoryBuffer {
    const char * data;
    size_t size;
    const char * pos;
};

size_t ovMemoryRead(void *ptr, size_t size, size_t nmemb, void *datasource)
{
    OVMemoryBuffer * buffer = static_cast<OVMemoryBuffer*>(datasource);
    size_t result = std::min<size_t>(size * nmemb, buffer->data + buffer->size - buffer->pos) / size;
    size = result * size;
    memcpy(ptr, buffer->pos, size);
    buffer->pos += size;
    return result ? result : EOF;
}

int ovMemorySeek(void *datasource, ogg_int64_t offset, int whence)
{
    OVMemoryBuffer * buffer = static_cast<OVMemoryBuffer*>(datasource);
    switch(whence) {
    case 0:
        buffer->pos = buffer->data + offset;
        break;
    case 1:
        buffer->pos += offset;
        break;
    case 2:
        buffer->pos = buffer->data + buffer->size + offset;
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
    return buffer->pos - buffer->data;
}

ov_callbacks OVMemoryCallbacks = {
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
        buffer_.data = data_.data();
        buffer_.size = data_.size();
        buffer_.pos = data_.data();
        int res = ov_open_callbacks(&buffer_, &vf_, 0, 0, OVMemoryCallbacks);
        IwAssertMsg(AUDIO_OGGFILE, res >= 0, ("Failed to open ogg stream: %d", res));
    }

    ~Impl()
    {
        ov_clear(&vf_);
    }

    long read(void * out, size_t len)
    {
        int bitstream = -1;
        long result = ov_read(&vf_, static_cast<char*>(out), len, 0, 2, 1, &bitstream);
        return result;
    }

    OggVorbis_File * handle()
    {
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
