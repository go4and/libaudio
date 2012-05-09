#pragma once

#include <memory>

namespace audio {

class OggFile;

class OnFlyDecoder {
public:
    OnFlyDecoder(int channel);
    ~OnFlyDecoder();

    OggFile * source();
    void reset(OggFile * source);
    bool poll();
    void volume(int value);
private:
    class Impl;
    std::auto_ptr<Impl> impl_;
};

}
