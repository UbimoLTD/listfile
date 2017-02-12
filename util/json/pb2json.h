// Copyright 2014, Ubimo.com .  All rights reserved.
// Author: Roman Gershman (roman@ubimo.com)
//
#pragma once

#include <google/protobuf/message.h>

#include "base/status.h"
#include "util/json/json_parser.h"

namespace util {

namespace gpb = ::google::protobuf;

struct Pb2JsonOptions {
  bool stringify_keys = true;
  bool enum_as_ints = false;
  bool bool_as_int = false;
};

std::string Pb2Json(const gpb::Message& msg, const Pb2JsonOptions& opts = Pb2JsonOptions());

// Important: does not clear msg object beforhand. The caller must do it himself!
base::Status Json2Pb(StringPiece json, gpb::Message* msg, bool skip_unknown_fields = true,
                     bool unescape_unicode = false);

}  // namespace util
