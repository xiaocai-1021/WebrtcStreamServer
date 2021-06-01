#include "byte_buffer.h"

#include <cstring>

uint8_t LoadUInt8BE(const uint8_t* data) {
  return data[0];
}

uint16_t LoadUInt16BE(const uint8_t* data) {
  int16_t value;
  char* p = (char*)&value;
  p[1] = *data++;
  p[0] = *data;
  return value;
}

uint32_t LoadUInt24BE(const uint8_t* data) {
  int32_t value = 0x00;
  char* p = (char*)&value;
  p[2] = *data++;
  p[1] = *data++;
  p[0] = *data;

  return value;
}

uint32_t LoadUInt32BE(const uint8_t* data) {
  int32_t value;
  char* p = (char*)&value;
  p[3] = *data++;
  p[2] = *data++;
  p[1] = *data++;
  p[0] = *data;
  return value;
}

uint64_t LoadUInt64BE(const uint8_t* data) {
  int64_t value;
  char* p = (char*)&value;
  p[7] = *data++;
  p[6] = *data++;
  p[5] = *data++;
  p[4] = *data++;
  p[3] = *data++;
  p[2] = *data++;
  p[1] = *data++;
  p[0] = *data;

  return value;
}

void StoreUInt8BE(uint8_t* data, uint8_t value) {
  *data++ = value;
}

void StoreUInt16BE(uint8_t* data, uint16_t value) {
  char* p = (char*)&value;
  *data++ = p[1];
  *data = p[0];
}

void StoreUInt24BE(uint8_t* data, uint32_t value) {
  char* p = (char*)&value;
  *data++ = p[2];
  *data++ = p[1];
  *data = p[0];
}

void StoreUInt32BE(uint8_t* data, uint32_t value) {
  char* p = (char*)&value;
  *data++ = p[3];
  *data++ = p[2];
  *data++ = p[1];
  *data = p[0];
}

void StoreUInt64BE(uint8_t* data, uint64_t value) {
  char* p = (char*)&value;
  *data++ = p[7];
  *data++ = p[6];
  *data++ = p[5];
  *data++ = p[4];
  *data++ = p[3];
  *data++ = p[2];
  *data++ = p[1];
  *data = p[0];
}

ByteWriter::ByteWriter(uint8_t* bytes, size_t len)
    : bytes_{bytes}, size_{len}, len_{0} {}

bool ByteWriter::WriteUInt8(uint8_t val) {
  if (Left() < 1)
    return false;
  *(bytes_ + len_) = val;
  len_ += 1;
  return true;
}

bool ByteWriter::WriteUInt16(uint16_t val) {
  if (Left() < 2)
    return false;
  StoreUInt16BE(bytes_ + len_, val);
  len_ += 2;
  return true;
}

bool ByteWriter::WriteUInt24(uint32_t val) {
  if (Left() < 3)
    return false;
  StoreUInt24BE(bytes_ + len_, val);
  len_ += 3;
  return true;
}

bool ByteWriter::WriteUInt32(uint32_t val) {
  if (Left() < 4)
    return false;
  StoreUInt32BE(bytes_ + len_, val);
  len_ += 4;
  return true;
}

bool ByteWriter::WriteUInt64(uint64_t val) {
  if (Left() < 8)
    return false;
  StoreUInt64BE(bytes_ + len_, val);
  len_ += 8;
  return true;
}

bool ByteWriter::WriteBytes(const char* val, size_t len) {
  if (Left() < len)
    return false;
  std::memcpy(bytes_ + len_, val, len);
  len_ += len;
  return true;
}

bool ByteWriter::WriteString(const std::string& val) {
  return WriteBytes(val.data(), val.length());
}

ByteReader::ByteReader(const uint8_t* bytes, size_t len)
    : bytes_{bytes}, end_{len}, start_{0} {}

bool ByteReader::ReadUInt8(uint8_t* val) {
  if (!val)
    return false;
  if (Left() < 1)
    return false;
  *val = *(bytes_ + start_);
  start_ += 1;
  return true;
}

bool ByteReader::ReadUInt16(uint16_t* val) {
  if (!val)
    return false;
  if (Left() < 2)
    return false;
  *val = LoadUInt16BE(bytes_ + start_);
  start_ += 2;
  return true;
}

bool ByteReader::ReadUInt24(uint32_t* val) {
  if (!val)
    return false;
  if (Left() < 3)
    return false;
  *val = LoadUInt24BE(bytes_ + start_);
  start_ += 3;
  return true;
}

bool ByteReader::ReadUInt32(uint32_t* val) {
  if (!val)
    return false;
  if (Left() < 4)
    return false;
  *val = LoadUInt32BE(bytes_ + start_);
  start_ += 4;
  return true;
}

bool ByteReader::ReadUInt64(uint64_t* val) {
  if (!val)
    return false;
  if (Left() < 8)
    return false;
  *val = LoadUInt64BE(bytes_ + start_);
  start_ += 8;
  return true;
}

bool ByteReader::ReadBytes(char* val, size_t len) {
  if (!val)
    return false;
  if (Left() < len)
    return false;
  std::memcpy(val, bytes_ + start_, len);
  start_ += len;
  return true;
}

bool ByteReader::ReadString(std::string* val, size_t len) {
  if (!val)
    return false;
  if (Left() < len)
    return false;
  val->append((char*)(bytes_ + start_), len);
  start_ += len;
  return true;
}

bool ByteReader::Consume(size_t size) {
  if (Left() < size)
    return false;
  start_ += size;
  return true;
}

bool ByteReader::Back(size_t size) {
  if (size > start_)
    return false;
  start_ -= size;
  return true;
}