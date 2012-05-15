#pragma once

namespace audio {

class Source {
public:
    Source(bool owned)
        : owned_(owned)
    {
    }

    inline bool owned() const { return owned_; }

    virtual bool pollable() = 0;
    virtual int mix(int16_t * out, int limit) = 0;

    virtual bool poll() { return false; }

    virtual ~Source() {}
private:
    bool owned_;
};

}
