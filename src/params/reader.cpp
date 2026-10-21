#include "reader.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wg::params::detail {

std::string format_number(double value) {
  if (std::isfinite(value) && value == std::trunc(value) && std::fabs(value) < 1e15) {
    return std::to_string(static_cast<std::int64_t>(value));
  }
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out.precision(15);
  out << value;
  return std::move(out).str();
}

void add_error(std::vector<ValidationIssue>& issues, std::string path, std::string message) {
  issues.push_back({Severity::error, std::move(path), std::move(message)});
}

void add_warning(std::vector<ValidationIssue>& issues, std::string path, std::string message) {
  issues.push_back({Severity::warning, std::move(path), std::move(message)});
}

void add_type_error(std::vector<ValidationIssue>& issues, std::string path, std::string_view what,
                    const io::Json& value) {
  add_error(issues, std::move(path),
            "expected " + std::string(what) + ", got " + std::string(io::json_type_name(value)));
}

std::optional<double> read_number(const io::Json& value, const std::string& path, double lo,
                                  double hi, bool integer, std::vector<ValidationIssue>& issues) {
  if (integer ? !value.is_number_integer() : !value.is_number()) {
    add_type_error(issues, path, integer ? "integer" : "number", value);
    return std::nullopt;
  }
  double v = 0.0;
  if (value.is_number_unsigned()) {
    v = static_cast<double>(value.get<std::uint64_t>());
  } else if (value.is_number_integer()) {
    v = static_cast<double>(value.get<std::int64_t>());
  } else {
    v = value.get<double>();
  }
  if (v < lo) {
    add_error(issues, path, format_number(v) + " is below the minimum " + format_number(lo));
    return std::nullopt;
  }
  if (v > hi) {
    add_error(issues, path, format_number(v) + " is above the maximum " + format_number(hi));
    return std::nullopt;
  }
  return v;
}

ObjectReader::ObjectReader(const io::Json& object, std::string path,
                           std::vector<ValidationIssue>& issues)
    : object_(object), path_(std::move(path)), issues_(issues), valid_(object.is_object()) {
  if (!valid_) {
    add_type_error(issues_, path_, "object", object_);
  }
}

std::string ObjectReader::child(std::string_view key) const {
  return io::json_child_path(path_, key);
}

const io::Json* ObjectReader::find(std::string_view key, bool required) {
  if (!valid_) {
    return nullptr;
  }
  known_.emplace_back(key);
  const auto it = object_.find(std::string(key));
  if (it == object_.end()) {
    if (required) {
      add_error(issues_, child(key), "missing required key");
    }
    return nullptr;
  }
  return &*it;
}

std::optional<std::string> ObjectReader::string(std::string_view key, bool required) {
  const io::Json* v = find(key, required);
  if (v == nullptr) {
    return std::nullopt;
  }
  if (!v->is_string()) {
    add_type_error(issues_, child(key), "string", *v);
    return std::nullopt;
  }
  return v->get<std::string>();
}

std::optional<double> ObjectReader::number(std::string_view key, double lo, double hi,
                                           bool required) {
  const io::Json* v = find(key, required);
  if (v == nullptr) {
    return std::nullopt;
  }
  return read_number(*v, child(key), lo, hi, false, issues_);
}

std::optional<std::int64_t> ObjectReader::integer(std::string_view key, std::int64_t lo,
                                                  std::int64_t hi, bool required) {
  const io::Json* v = find(key, required);
  if (v == nullptr) {
    return std::nullopt;
  }
  const std::optional<double> d =
      read_number(*v, child(key), static_cast<double>(lo), static_cast<double>(hi), true, issues_);
  if (!d) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(*d);
}

std::optional<bool> ObjectReader::boolean(std::string_view key, bool required) {
  const io::Json* v = find(key, required);
  if (v == nullptr) {
    return std::nullopt;
  }
  if (!v->is_boolean()) {
    add_type_error(issues_, child(key), "boolean", *v);
    return std::nullopt;
  }
  return v->get<bool>();
}

std::optional<std::string> ObjectReader::label(std::string_view key,
                                               const std::vector<std::string_view>& labels,
                                               bool required) {
  std::optional<std::string> s = string(key, required);
  if (!s) {
    return std::nullopt;
  }
  std::string list;
  for (std::string_view l : labels) {
    if (*s == l) {
      return s;
    }
    list += list.empty() ? "" : ", ";
    list += l;
  }
  add_error(issues_, child(key), "\"" + *s + "\" is not one of " + list);
  return std::nullopt;
}

const io::Json* ObjectReader::array(std::string_view key, std::size_t size, bool required) {
  const io::Json* v = find(key, required);
  if (v == nullptr) {
    return nullptr;
  }
  if (!v->is_array()) {
    add_type_error(issues_, child(key), "array", *v);
    return nullptr;
  }
  if (size != 0 && v->size() != size) {
    add_error(issues_, child(key),
              "expected " + std::to_string(size) + " elements, got " + std::to_string(v->size()));
    return nullptr;
  }
  return v;
}

const io::Json* ObjectReader::object(std::string_view key, bool required) {
  const io::Json* v = find(key, required);
  if (v == nullptr) {
    return nullptr;
  }
  if (!v->is_object()) {
    add_type_error(issues_, child(key), "object", *v);
    return nullptr;
  }
  return v;
}

std::optional<std::pair<std::int64_t, std::int64_t>> ObjectReader::interval(std::string_view key,
                                                                            std::int64_t min,
                                                                            std::int64_t max) {
  const io::Json* a = array(key, 2);
  if (a == nullptr) {
    return std::nullopt;
  }
  const std::string path = child(key);
  const auto lo_d = static_cast<double>(min);
  const auto hi_d = static_cast<double>(max);
  const std::optional<double> lo =
      read_number((*a)[0], io::json_index_path(path, 0), lo_d, hi_d, true, issues_);
  const std::optional<double> hi =
      read_number((*a)[1], io::json_index_path(path, 1), lo_d, hi_d, true, issues_);
  if (!lo || !hi) {
    return std::nullopt;
  }
  if (*lo > *hi) {
    add_error(issues_, path,
              "lower end " + format_number(*lo) + " is above upper end " + format_number(*hi));
    return std::nullopt;
  }
  return std::pair{static_cast<std::int64_t>(*lo), static_cast<std::int64_t>(*hi)};
}

void ObjectReader::finish() {
  if (!valid_) {
    return;
  }
  for (const auto& item : object_.items()) {
    const std::string& key = item.key();
    bool known = key == "$schema";
    for (const std::string& k : known_) {
      known = known || k == key;
    }
    if (!known) {
      add_error(issues_, child(key), "unknown key");
    }
  }
}

}  // namespace wg::params::detail
