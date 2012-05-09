#pragma once

namespace audio {

class Buffer;
class OggFile;

class Manager {
public:
    Manager();
    ~Manager();

    bool play(const Buffer & sample);
    void loopVolume(int volume);
    void loop(OggFile * file);
private:
    class Impl;
    std::auto_ptr<Impl> impl_;
};

}
