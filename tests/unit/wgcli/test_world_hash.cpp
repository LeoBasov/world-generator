#include "world_hash.hpp"

#include <doctest/doctest.h>

#include "support/sample_world.hpp"
#include "wg/io/world_file.hpp"

using wg::core::FieldId;

TEST_CASE("wgcli.world_hash formats hashes") {
  CHECK(wg::cli::hex64(0) == "0x0000000000000000");
  CHECK(wg::cli::hex64(0xDEADBEEF01234567ULL) == "0xdeadbeef01234567");
}

TEST_CASE("wgcli.world_hash of a state equals the hash of its world file") {
  auto state = wg::test::sample_world();
  const auto params = wg::test::sample_params();
  auto hashes = wg::cli::hash_world(state);
  REQUIRE(hashes);
  CHECK(hashes->fields.size() > 1);

  auto bytes = wg::io::write_world(state, params);
  REQUIRE(bytes);
  auto reader = wg::io::WorldReader::open_buffer(*bytes);
  REQUIRE(reader);
  auto from_file = wg::cli::hash_world_file(*reader);
  REQUIRE(from_file);
  CHECK(from_file->world == hashes->world);
  CHECK(from_file->layers == hashes->layers);
  CHECK(from_file->fields == hashes->fields);

  // The provenance does not enter the hash; field values and layers do.
  state.provenance().attempt += 7;
  CHECK(wg::cli::hash_world(state)->world == hashes->world);
  auto elevation = state.write<float>(FieldId::elevation);
  REQUIRE(elevation);
  elevation->data[3] += 1.0F;
  CHECK(wg::cli::hash_world(state)->world != hashes->world);
  elevation->data[3] -= 1.0F;
  CHECK(wg::cli::hash_world(state)->world == hashes->world);
  state.layers().peaks.pop_back();
  CHECK(wg::cli::hash_world(state)->world != hashes->world);
}
