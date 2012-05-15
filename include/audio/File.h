#pragma once

namespace audio {

class File {
public:
    virtual long read(void * out, size_t len) = 0;
    virtual int rate() = 0;
    virtual void rewind() = 0;

    virtual ~File() {}
};

}
