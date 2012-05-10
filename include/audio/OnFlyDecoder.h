#pragma once

#include <memory>

#include "audio/Manager.h"

namespace audio {

class OggFile;

class OnFlyDecoder : public Source {
public:
    OnFlyDecoder(bool owned, OggFile & file);
    ~OnFlyDecoder();

    OggFile & source();
    void volume(int value);

    bool poll();
    bool pollable() { return true; }
    int mix(int16_t * out, int limit);
private:
    class Impl;
    std::auto_ptr<Impl> impl_;
};

}
