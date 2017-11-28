// Copyright (c) 2017 Evan Klitzke <evan@eklitzke.org>
//
// This file is part of SPV.
//
// SPV is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// SPV is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// SPV. If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include <endian.h>
#include <limits>

#include "./buffer.h"
#include "./logging.h"
#include "./pow.h"
#include "./protocol.h"

#define CHECK(x)  \
  if (!(x)) {     \
    return false; \
  }

#define PULL(x) CHECK(pull(x))

namespace spv {
EXTERN_LOGGER(decoder)

class Decoder {
 public:
  Decoder() = delete;
  Decoder(const char *data, size_t sz) : data_(data), cap_(sz), off_(0) {}

  inline bool replace(size_t cap) {
    if (cap <= cap_ && cap <= off_) {
      cap_ = cap;
      return true;
    }
    return false;
  }

  inline bool pull(char *out, size_t sz) {
    if (sz + off_ > cap_) {
      decoder_log->debug("failed to pull {} bytes, offset = {}, capacity = {}",
                         sz, off_, cap_);
      return false;
    }
    memmove(out, data_ + off_, sz);
    off_ += sz;
    return true;
  }

  template <typename T>
  inline bool pull(T &out) {
    return pull(reinterpret_cast<char *>(&out), sizeof(T));
  }

  bool pull_varint(uint64_t &out) {
    uint8_t prefix;
    PULL(prefix);
    if (prefix < 0xfd) {
      out = prefix;
      return true;
    }
    switch (prefix) {
      case 0xfd: {
        uint16_t val;
        PULL(val);
        out = val;
        return true;
      }
      case 0xfe: {
        uint32_t val;
        PULL(val);
        out = val;
        return true;
      }
      case 0xff:
        PULL(out);
        return true;
    }
    assert(false);  // not reached
  }

  bool pull_string(std::string &out) {
    uint64_t sz;
    CHECK(pull_varint(sz));
    assert(sz <= std::numeric_limits<uint16_t>::max());
    out.append(data(), sz);
    off_ += sz;
    return true;
  }

  // Caller *must* check the buffer size
  void pull_headers(Headers &headers) {
    char cmd_buf[COMMAND_SIZE];
    std::memset(&cmd_buf, 0, sizeof cmd_buf);
    pull(headers.magic);
    if (headers.magic != TESTNET3_MAGIC) {
      decoder_log->warn("peer sent wrong magic bytes");
    }
    pull(cmd_buf);
    assert(cmd_buf[COMMAND_SIZE - 1] == '\0');
    headers.command = cmd_buf;
    pull(headers.payload_size);
    pull(headers.checksum);
  }

  bool pull_netaddr(VersionNetAddr &addr) {
    PULL(addr.services);
    PULL(addr.addr);
    PULL(addr.port);
    addr.port = be16toh(addr.port);  // TODO: convert ip as well
    return true;
  }

  bool pull_netaddr(NetAddr &addr) {
    PULL(addr.time);
    PULL(addr.services);
    PULL(addr.addr);
    PULL(addr.port);
    addr.port = be16toh(addr.port);  // TODO: convert ip as well
    return true;
  }

  bool pull_ping(Ping &msg) {
    pull(msg.nonce);
    return true;
  }

  bool pull_pong(Pong &msg) {
    pull(msg.nonce);
    return true;
  }

  bool pull_version(Version &msg) {
    PULL(msg.version);
    PULL(msg.services);
    PULL(msg.timestamp);
    CHECK(pull_netaddr(msg.addr_recv));
    if (msg.version >= 106) {
      CHECK(pull_netaddr(msg.addr_from));
      PULL(msg.nonce);
      CHECK(pull_string(msg.user_agent));
      PULL(msg.start_height);
      if (msg.version >= 70001) {
        CHECK(pull(msg.relay));
      }
    }
    return true;
  }

  bool pull_verack(Verack &msg) { return true; }

  bool validate_msg(const Message &msg) {
    if (off_ != cap_) {
      decoder_log->warn("failed to pull enough bytes");
      return false;
    }
    uint32_t cksum = checksum(data_, cap_);
    if (cksum != msg.headers.checksum) {
      decoder_log->warn("invalid checksum!");
      return false;
    }
    return true;
  }

  inline size_t bytes_read() const { return off_; }
  inline const char *data() const { return data_ + off_; }

 private:
  const char *data_;
  size_t cap_;
  size_t off_;
};
}  // namespace spv
