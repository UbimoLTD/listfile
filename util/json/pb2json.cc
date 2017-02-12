// Copyright 2014, Ubimo.com .  All rights reserved.
// Author: Roman Gershman (roman@ubimo.com)
//
#include "util/json/pb2json.h"

#include <google/protobuf/repeated_field.h>

#include "strings/escaping.h"
#include "strings/strcat.h"

namespace util {

namespace {
typedef gpb::FieldDescriptor FD;
using base::Status;
using base::StatusCode;

inline Status ParseStatus(string msg) {
  return Status(StatusCode::IO_ERROR, std::move(msg));
}

#define OUTPUT_REP(suffix, type)  case FD::CPPTYPE_##suffix: { \
        const auto& arr = refl->GetRepeatedField<type>(msg, fd); \
        for (auto val : arr) { \
          StrAppend(res, val, ","); \
        } \
        res->pop_back(); \
      } \
      break \

void AppendRepeated(const gpb::Message& msg, const gpb::FieldDescriptor* fd,
                    const gpb::Reflection* refl, const Pb2JsonOptions& opts, string* res) {
  *res += "[";
  switch (fd->cpp_type()) {
    OUTPUT_REP(INT32, int32);
    OUTPUT_REP(UINT32, uint32);
    OUTPUT_REP(INT64, gpb::int64);
    OUTPUT_REP(UINT64, gpb::uint64);
    OUTPUT_REP(FLOAT,  float);
    OUTPUT_REP(DOUBLE, double);
    OUTPUT_REP(BOOL, bool);
    case FD::CPPTYPE_ENUM: {
      int sz = refl->FieldSize(msg, fd);
      for (int i = 0; i < sz; ++i) {
        const gpb::EnumValueDescriptor* edescr = refl->GetRepeatedEnum(msg, fd, i);
        StrAppend(res, "\"", edescr->name(), "\",");
      }
      res->pop_back();
    }
    break;
    case FD::CPPTYPE_STRING: {
      const auto& arr = refl->GetRepeatedPtrField<string>(msg, fd);
      for (const auto& val : arr) {
        StrAppend(res, "\"", strings::JsonEscape(val), "\",");
      }
      res->pop_back();
    }
    break;
    case FD::CPPTYPE_MESSAGE: {
      const auto& arr = refl->GetRepeatedPtrField<gpb::Message>(msg, fd);
      for (const auto& val : arr) {
        StrAppend(res, Pb2Json(val, opts), ",");
      }
      res->pop_back();
    }
    break;
    default:
      LOG(FATAL) << "Not supported repeated " << fd->cpp_type_name();
  }
  *res += "]";
}

#undef OUTPUT_REP

void PrintValue(const gpb::Message& msg, const gpb::FieldDescriptor* fd,
                const gpb::Reflection* refl, const Pb2JsonOptions& opts, string* res) {
  switch (fd->cpp_type()) {
#define OUTPUT_FIELD(suffix, METHOD)      \
    case FD::CPPTYPE_##suffix: \
        StrAppend(res, refl->Get##METHOD(msg, fd));    \
      break

    OUTPUT_FIELD(INT32,  Int32);
    OUTPUT_FIELD(INT64,  Int64);
    OUTPUT_FIELD(UINT32, UInt32);
    OUTPUT_FIELD(UINT64, UInt64);
    OUTPUT_FIELD(FLOAT,  Float);
    OUTPUT_FIELD(DOUBLE, Double);
#undef OUTPUT_FIELD
    case FD::CPPTYPE_STRING: {
      string scratch;
      const string& value = refl->GetStringReference(msg, fd, &scratch);
      StrAppend(res, "\"", strings::JsonEscape(value), "\"");
    }
    break;
    case FD::CPPTYPE_BOOL: {
      bool b = refl->GetBool(msg, fd);
      const gpb::FieldOptions& fo = fd->options();
      if (opts.bool_as_int) {
        res->append(b ? "1" : "0");
      } else {
        res->append(b ? "true" : "false");
      }
    }
    break;

    case FD::CPPTYPE_ENUM:
      if (opts.enum_as_ints)
        StrAppend(res, refl->GetEnum(msg, fd)->number());
      else
        StrAppend(res, "\"", refl->GetEnum(msg, fd)->name(), "\"");
    break;
    case FD::CPPTYPE_MESSAGE:
      res->append(Pb2Json(refl->GetMessage(msg, fd), opts));
    break;
    default:
      LOG(FATAL) << "Not supported field " << fd->cpp_type_name();
  }
}

Status Json2Pb(JsonObject obj, gpb::Message* msg, bool skip_unknown_fields,
               bool unescape_unicode);

#define INVALID_FIELD ParseStatus("Invalid field")


inline Status FieldMismatch(StringPiece fname, StringPiece expected_type, int real_type) {
  return ParseStatus(StrCat("type mismatch for ", fname, " expected ", expected_type, " vs ",
                             real_type));
}

base::StatusObject<int32> ExtractInt32(const JsonObject& obj) {
  switch (obj.type()) {
    case JsonObject::INTEGER:
      return obj.GetInt();
    case JsonObject::STRING: {
      int32 val;
      if (!safe_strto32(obj.GetStringPiece(), &val)) {
        return ParseStatus("Bad number");
      }
      return val;
    }
    default: {}
  }
  return ParseStatus("Unsupported type");
}

base::StatusObject<int64> ExtractInt64(const JsonObject& obj) {
  switch (obj.type()) {
    case JsonObject::INTEGER:
      return obj.GetInt();
    case JsonObject::STRING: {
      int64 val;
      if (!safe_strto64(obj.GetStringPiece(), &val)) {
        return ParseStatus("Bad number");
      }
      return val;
    }
    default: {}
  }
  return ParseStatus("Unsupported type");
}

base::StatusObject<bool> ExtractBool(const JsonObject& obj) {
  switch (obj.type()) {
    case JsonObject::PRIMITIVE:
      // Must be bool because IsNull checked before calling this function.
      return obj.GetBool();
    case JsonObject::INTEGER: {
      int64 val = obj.GetInt();
      if ((val & 1) != val) {
        return ParseStatus("Bad boolean");
      }
      return val == 1;
    }
    default: {}
  }
  return ParseStatus("Unsupported type");
}

base::StatusObject<const gpb::EnumValueDescriptor*> ExtractEnum(
    const gpb::EnumDescriptor* enum_type, const JsonObject& obj) {

  const gpb::EnumValueDescriptor* eval;
  if (obj.type() == JsonObject::STRING) {
    eval = enum_type->FindValueByName(obj.GetString());
    if (eval == nullptr)
      return ParseStatus(StrCat("Invalid enum string for ", enum_type->name()));
    return eval;
  }
  if (obj.type() != JsonObject::INTEGER) {
    return ParseStatus(StrCat("Unsupported enum type ", obj.type()));
  }
  eval = enum_type->FindValueByNumber(obj.GetInt());
  if (eval == nullptr)
    return ParseStatus(StrCat("Invalid enum value ", obj.GetInt(), " for ", enum_type->name()));
  return eval;
}

Status ParseField(JsonObject obj, const gpb::FieldDescriptor* fd,
                  const gpb::Reflection* refl, bool skip_unknown_fields, bool unescape_unicode,
                  gpb::Message* msg) {
  if (!obj.is_defined() || obj.IsNull())
    return INVALID_FIELD;

#define MISMATCH(x) FieldMismatch(fd->name(), x, obj.type())

#define SET_FIELD(suffix, METHOD, JSON_TYPE, GET_METHOD)  \
    case FD::CPPTYPE_##suffix:           \
      if (obj.type() != JsonObject::JSON_TYPE)  \
        return MISMATCH(#JSON_TYPE); \
      refl->Set##METHOD(msg, fd, obj.Get##GET_METHOD());    \
    break
#define SET_FIELD2(suffix, method_suffix) \
    case FD::CPPTYPE_ ## suffix:  { \
      auto res = Extract ## method_suffix(obj); \
      if (!res.ok()) return res.status; \
      refl->Set ## method_suffix(msg, fd, res.obj); } \
      break;

  switch (fd->cpp_type()) {
    SET_FIELD2(INT32, Int32);
    SET_FIELD2(INT64, Int64);
    SET_FIELD(UINT32, UInt32, INTEGER, Int);
    SET_FIELD2(BOOL, Bool);
#undef SET_FIELD
#undef SET_FIELD2

    case FD::CPPTYPE_STRING:
      if (obj.type() != JsonObject::STRING)
        return MISMATCH("STRING");
      {
        StringPiece str = obj.GetStringPiece();
        if (unescape_unicode) {
          string tmp, error;
          if (!strings::CUnescape(str, &tmp, &error)) {
            LOG(WARNING) << "Bad unicode string " << str << ", error:" << error;
            return ParseStatus("Bad Unicode string");
          }
          refl->SetString(msg, fd, std::move(tmp));
        } else {
          refl->SetString(msg, fd, str.as_string());
        }
      }
    break;
    case FD::CPPTYPE_FLOAT:
    case FD::CPPTYPE_DOUBLE: {
      if (!obj.IsNumber())
        return MISMATCH("NUMBER");
      double d = obj.GetDouble();
      if (fd->cpp_type() == FD::CPPTYPE_FLOAT)
        refl->SetFloat(msg, fd, d);
      else
        refl->SetDouble(msg, fd, d);
    }
    break;
    case FD::CPPTYPE_UINT64: {
      if (obj.type() == JsonObject::UINT) {
        refl->SetUInt64(msg, fd, obj.GetUInt());
      } else if (obj.type() == JsonObject::INTEGER) {
        int64 v = obj.GetInt();
        if (v < 0) return ParseStatus("Negative number for uint64");
        refl->SetUInt64(msg, fd, v);
      } else {
        return MISMATCH("UINT64");
      }
      break;
    }
    case FD::CPPTYPE_MESSAGE:
      return Json2Pb(obj, refl->MutableMessage(msg, fd), skip_unknown_fields, unescape_unicode);
    break;
    case FD::CPPTYPE_ENUM: {
      auto res = ExtractEnum(fd->enum_type(), obj);
      if (!res.ok())
        return res.status;
      refl->SetEnum(msg, fd, res.obj);
    }
    break;
    default:
      LOG(FATAL) << "Not supported field " << fd->cpp_type_name();
  }

  return Status::OK;
}

#undef MISMATCH

#define MISMATCH(x) FieldMismatch(fd->name(), x, array.type())

Status ParseArray(JsonObject array, const gpb::FieldDescriptor* fd,
                  const gpb::Reflection* refl, bool skip_unknown_fields, bool unescape_unicode,
                  gpb::Message* msg) {
  if (array.type() != JsonObject::ARRAY)
    return MISMATCH("array");

  FD::CppType t = fd->cpp_type();

#define ADD_FIELD(suffix, METHOD, JSON_TYPE, GET_METHOD)  \
    case FD::CPPTYPE_##suffix:           \
      if (i->type() != JsonObject::JSON_TYPE)  \
        return MISMATCH(#JSON_TYPE);  \
      refl->Add##METHOD(msg, fd, i->Get##GET_METHOD());    \
    break
#define ADD_FIELD2(suffix, method_suffix) \
    case FD::CPPTYPE_ ## suffix:  { \
      auto res = Extract ## method_suffix(*i); \
      if (!res.ok()) return res.status; \
      refl->Add ## method_suffix(msg, fd, res.obj); } \
      break;

  for (auto i = array.GetArrayIterator(); !i.Done(); ++i) {
    if (i->IsNull()) return INVALID_FIELD;
    switch (t) {
      ADD_FIELD2(INT32,  Int32);
      ADD_FIELD2(INT64,  Int64);
      ADD_FIELD(UINT32, UInt32, INTEGER, Int);
      ADD_FIELD(STRING, String, STRING, String);
      ADD_FIELD(BOOL, Bool, PRIMITIVE, Bool);

    case FD::CPPTYPE_FLOAT:
    case FD::CPPTYPE_DOUBLE: {
      if (!i->IsNumber())
        return MISMATCH("NUMBER");

      double d = i->GetDouble();
      if (t == FD::CPPTYPE_FLOAT)
        refl->AddFloat(msg, fd, d);
      else
        refl->AddDouble(msg, fd, d);
    }
    break;
    case FD::CPPTYPE_UINT64: {
      if (i->type() == JsonObject::UINT) {
        refl->AddUInt64(msg, fd, i->GetUInt());
      } else if (i->type() == JsonObject::INTEGER) {
        int64 v = i->GetInt();
        if (v < 0) return ParseStatus("Negative number for uint64");
        refl->AddUInt64(msg, fd, v);
      } else {
        return MISMATCH("UINT64");
      }
    }
    case FD::CPPTYPE_MESSAGE:
      RETURN_IF_ERROR(Json2Pb(*i, refl->AddMessage(msg, fd), skip_unknown_fields,
                      unescape_unicode));
    break;
    case FD::CPPTYPE_ENUM: {
      auto res = ExtractEnum(fd->enum_type(), *i);
      if (!res.ok()) return res.status;

      refl->AddEnum(msg, fd, res.obj);
    }
    break;
    default:
      LOG(FATAL) << "Not supported repeated " << fd->cpp_type_name();
    }
  }
#undef ADD_FIELD
  return Status::OK;
}

#undef FIELD_MISMATCH

Status Json2Pb(JsonObject root, gpb::Message* msg, bool skip_unknown_fields,
               bool unescape_unicode) {
  if (root.type() != JsonObject::OBJECT) {
    return ParseStatus("Invalid root object");
  }
  const gpb::Descriptor* descr = msg->GetDescriptor();
  const gpb::Reflection* refl = msg->GetReflection();

  Status st;
  for (auto i = root.GetArrayIterator(); !i.Done(); ++i) {
    string name = i->name().as_string();
    const gpb::FieldDescriptor* fd = descr->FindFieldByName(name);
    if (fd == nullptr) {
      if (skip_unknown_fields)
        continue;
      LOG(ERROR) << "Invalid field name " << name << " in " << descr->name();
      return ParseStatus("Invalid field name");
    }
    if (fd->is_repeated()) {
      st = ParseArray(*i, fd, refl, skip_unknown_fields, unescape_unicode, msg);
    } else {
      st = ParseField(*i, fd, refl, skip_unknown_fields, unescape_unicode, msg);
    }
    if (!st.ok()) {
      LOG(WARNING) << "json field " << name << " type " << i->type() << " fd type:"
                   << fd->cpp_type_name();
      return st;
    }
  }
  return Status::OK;
}

}  // namespace

std::string Pb2Json(const gpb::Message& msg, const Pb2JsonOptions& opts) {
  const gpb::Descriptor* descr = msg.GetDescriptor();
  const gpb::Reflection* refl = msg.GetReflection();

  std::string res("{");
  for (int i = 0; i < descr->field_count(); ++i) {
    const gpb::FieldDescriptor* fd = descr->field(i);
    bool is_set = (fd->is_repeated() && refl->FieldSize(msg, fd) > 0) ||
      fd->is_required() || (fd->is_optional() && refl->HasField(msg, fd));
    if (!is_set)
      continue;

    const gpb::FieldOptions& fo = fd->options();

    const string& fname = fd->name();
    if (fname.empty())
      continue;

    if (opts.stringify_keys) {
      StrAppend(&res, "\"", fname, "\": ");
    } else {
      StrAppend(&res, fname, ": ");
    }
    if (fd->is_repeated()) {
      AppendRepeated(msg, fd, refl, opts, &res);
    } else {
      PrintValue(msg, fd, refl, opts, &res);
    }
    res += ", ";
  }
  if (res.size() > 2) {
    res.resize(res.size() - 2);
  }
  res += "}";
  // VLOG(1) << "msg: " << msg.ShortDebugString() << ", json:\n" << res;
  return res;
}

Status Json2Pb(StringPiece json, gpb::Message* msg, bool skip_unknown_fields,
               bool unescape_unicode) {
  JsonParser parser;
  JsonParser::Status st = parser.Parse(json);
  if (st != JsonParser::SUCCESS) {
    return ParseStatus("Invalid json");
  }
  return Json2Pb(parser.root(), msg, skip_unknown_fields, unescape_unicode);
}

}  // namespace util
