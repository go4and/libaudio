#pragma once

namespace audio {

class Buffer;
class OggFile;

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

class Manager {
public:
    Manager();
    ~Manager();

    void start();
    void stop();

    Source * play(const Buffer & sample);
    Source * play(Source * source);
    void stop(Source * source);
private:
    class Impl;
    std::auto_ptr<Impl> impl_;
};

}
