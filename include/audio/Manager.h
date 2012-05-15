#pragma once

namespace audio {

class Buffer;
class Source;

class Manager {
public:
    Manager();
    ~Manager();

    void start();
    void stop();
    void poll();

    Source * play(const Buffer & sample);
    Source * play(Source * source);
    void stop(Source * source);
private:
    class Impl;
    std::auto_ptr<Impl> impl_;
};

}
