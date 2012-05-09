#pragma once

namespace audio {

class OggFile;
class Buffer;

class Decoder {
public:
    Decoder();
    ~Decoder();

    Buffer decode(OggFile & file, int volume = 0x100);
private:
    int16_t * decodeBuffer_;
    std::vector<int16_t> buffer_;
};

}
