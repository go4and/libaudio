#pragma once

#include <string.h>

#include <atomics.h>

namespace audio {

extern AtomicFunctions atomics;

class Buffer {
public:
    Buffer()
        : data_(0)
    {
    }

    explicit Buffer(size_t size)
        : data_(new char[size + 8])
    {
        *sizeAddress() = size;
        *counterAddress() = 1;
    }

    Buffer(char * data, size_t size)
        : data_(new char[size + 8])
    {
        memcpy(this->data(), data, size);
        *sizeAddress() = size;
        *counterAddress() = 1;
    }

    ~Buffer()
    {
        reset();
    }

    Buffer(const Buffer & rhs)
        : data_(rhs.data_)
    {
        if(data_)
            atomics.add(counterAddress(), 1);
    }

    void operator=(const Buffer & rhs)
    {
        reset();
        data_ = rhs.data_;
        if(data_)
            atomics.add(counterAddress(), 1);
    }

    size_t size() const
    {
        return *sizeAddress();
    }

    char * data() const
    {
        return data_ + 8;
    }

    void reset()
    {
        if(data_)
        {
            if(atomics.add(counterAddress(), -1) == 1)
                delete [] data_;
            data_ = 0;
        }
    }
private:
    size_t * sizeAddress() const
    {
        return static_cast<size_t*>(static_cast<void*>(data_ + 4));
    }

    volatile int * counterAddress() const
    {
        return static_cast<volatile int*>(static_cast<void*>(data_));
    }

    char * data_;
};

}
