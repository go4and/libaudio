#pragma once

#include <memory>

struct OggVorbis_File;

namespace audio {

class Buffer;

class OggFile {
public:
    OggFile(const Buffer & buffer);
    ~OggFile();

    long read(void * out, size_t len);
    int rate();
    void rewind();
    OggVorbis_File * handle();
private:
    class Impl;
    std::auto_ptr<Impl> impl_;
};

}
