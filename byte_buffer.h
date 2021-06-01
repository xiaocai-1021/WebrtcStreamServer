#pragma once

#include <cstdint>
#include <string>

class ByteWriter {
 public:
  ByteWriter(uint8_t* bytes, size_t len);
  bool WriteUInt8(uint8_t val);
  bool WriteUInt16(uint16_t val);
  bool WriteUInt24(uint32_t val);
  bool WriteUInt32(uint32_t val);
  bool WriteUInt64(uint64_t val);
  bool WriteBytes(const char* val, size_t len);
  bool WriteString(const std::string& val);
  bool Consume(int size) {
    if (Left() < size)
      return false;
    len_ += size;
    return true;
  }
  size_t Left() {
    return size_ - len_;
  }
  size_t Used() {
    return len_;
  }
  uint8_t* Data() {
    return bytes_;
  };

  size_t Size() const {
    return size_;
  }

 private:
  uint8_t* bytes_;
  size_t len_;
  size_t size_;
};

class ByteReader {
 public:
  ByteReader(const uint8_t* bytes, size_t len);

  bool ReadUInt8(uint8_t* val);

  bool ReadUInt16(uint16_t* val);

  bool ReadUInt24(uint32_t* val);

  bool ReadUInt32(uint32_t* val);

  bool ReadUInt64(uint64_t* val);

  bool ReadBytes(char* val, size_t len);

  bool ReadString(std::string* val, size_t len);

  bool Consume(size_t size);
  // Back size bytes.
  bool Back(size_t size);

  // Returns start of unprocessed data.
  const uint8_t* CurrentData() const {
    return bytes_ + start_;
  }
  // Returns number of unprocessed bytes.
  size_t Left() const {
    return end_ - start_;
  }

  const uint8_t* Data() const {
    return bytes_;
  }

  size_t Size() const {
    return end_;
  }

  size_t Used() {
    return start_;
  }

 private:
  const uint8_t* bytes_;
  size_t start_;
  size_t end_;
};

uint8_t LoadUInt8BE(const uint8_t* data);

uint16_t LoadUInt16BE(const uint8_t* data);

uint32_t LoadUInt24BE(const uint8_t* data);

uint32_t LoadUInt32BE(const uint8_t* data);

uint64_t LoadUInt64BE(const uint8_t* data);

void StoreUInt8BE(uint8_t* data, uint8_t value);

void StoreUInt16BE(uint8_t* data, uint16_t value);

void StoreUInt24BE(uint8_t* data, uint32_t value);

void StoreUInt32BE(uint8_t* data, uint32_t value);

void StoreUInt64BE(uint8_t* data, uint64_t value);