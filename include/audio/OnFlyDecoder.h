#pragma once

#include <memory>

#include "audio/Source.h"

namespace audio {

class File;

class OnFlyDecoder : public Source {
public:
    OnFlyDecoder(bool owned, File & file);
    ~OnFlyDecoder();

    File & source();
    void volume(int value);

    bool poll();
    bool pollable() { return true; }
    int mix(int16_t * out, int limit);
private:
    class Impl;
    std::auto_ptr<Impl> impl_;
};

}
