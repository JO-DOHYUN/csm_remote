#pragma once

#include <stddef.h>

class Stream {
 public:
  virtual ~Stream() = default;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual void flush() = 0;
  virtual size_t write(unsigned char) = 0;
  virtual size_t write(const unsigned char*, size_t) = 0;
};
