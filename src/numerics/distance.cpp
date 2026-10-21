#include "wg/numerics/distance.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include "common.hpp"
#include "wg/core/arena.hpp"
#include "wg/core/expected.hpp"

namespace wg::num {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

// Scratch of one worker for the row pass of the EDT.
struct EnvelopeScratch {
  std::span<double> f;        // squared column distances of the row
  std::span<std::int32_t> v;  // positions of the parabolas in the lower envelope
  std::span<double> z;        // boundaries between them, one more than v
};

// Lower envelope of the parabolas (q - p)^2 + f[p] over the finite f[p], evaluated at every q
// (Felzenszwalb and Huttenlocher 2012). All values are integers below 2^53, so the result is
// exact; a rounded boundary s can never cross an integer position.
void envelope_1d(EnvelopeScratch s, std::span<double> out) {
  const auto n = static_cast<std::int32_t>(s.f.size());
  const auto intersect = [&s](std::int32_t q, std::int32_t p) {
    const double qq = static_cast<double>(q) * q;
    const double pp = static_cast<double>(p) * p;
    return ((s.f[static_cast<std::size_t>(q)] + qq) - (s.f[static_cast<std::size_t>(p)] + pp)) /
           (2.0 * (static_cast<double>(q) - static_cast<double>(p)));
  };
  std::int32_t k = -1;
  for (std::int32_t q = 0; q < n; ++q) {
    if (s.f[static_cast<std::size_t>(q)] == kInf) {
      continue;
    }
    if (k < 0) {
      k = 0;
      s.v[0] = q;
      s.z[0] = -kInf;
      s.z[1] = kInf;
      continue;
    }
    double b = intersect(q, s.v[static_cast<std::size_t>(k)]);
    // z[0] = -inf, so k stays >= 0.
    while (b <= s.z[static_cast<std::size_t>(k)]) {
      --k;
      b = intersect(q, s.v[static_cast<std::size_t>(k)]);
    }
    ++k;
    s.v[static_cast<std::size_t>(k)] = q;
    s.z[static_cast<std::size_t>(k)] = b;
    s.z[static_cast<std::size_t>(k) + 1] = kInf;
  }
  if (k < 0) {
    std::fill(out.begin(), out.end(), kInf);
    return;
  }
  k = 0;
  for (std::int32_t q = 0; q < n; ++q) {
    while (s.z[static_cast<std::size_t>(k) + 1] < static_cast<double>(q)) {
      ++k;
    }
    const std::int32_t p = s.v[static_cast<std::size_t>(k)];
    const double d = static_cast<double>(q) - static_cast<double>(p);
    out[static_cast<std::size_t>(q)] = (d * d) + s.f[static_cast<std::size_t>(p)];
  }
}

}  // namespace

core::Status bfs_distance(std::span<const std::uint8_t> seeds,
                          std::span<const std::uint8_t> passable, const core::Grid& grid,
                          Connectivity connectivity, std::span<std::int32_t> out,
                          core::MemoryResource& resource) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(seeds, grid, "bfs_distance: seeds must have one value per cell"),
       detail::check_optional_field(passable, grid,
                                    "bfs_distance: passable must be empty or per cell"),
       detail::check_field(out, grid, "bfs_distance: out must have one value per cell")});
  if (!status.ok()) {
    return status;
  }
  const auto cells = static_cast<std::size_t>(grid.cells());
  core::Expected<core::Arena> arena =
      core::Arena::create(detail::arena_bytes<std::int64_t>(cells), resource);
  if (!arena) {
    return std::move(arena.error());
  }
  core::Expected<std::span<std::int64_t>> queue_storage = arena->allocate<std::int64_t>(cells);
  if (!queue_storage) {
    return std::move(queue_storage.error());
  }
  // Every cell enters the FIFO queue at most once.
  const std::span<std::int64_t> queue = *queue_storage;
  std::size_t head = 0;
  std::size_t tail = 0;
  for (std::size_t i = 0; i < cells; ++i) {
    if (seeds[i] != 0) {
      out[i] = 0;
      queue[tail++] = static_cast<std::int64_t>(i);
    } else {
      out[i] = kUnreached;
    }
  }
  while (head < tail) {
    const std::int64_t i = queue[head++];
    const std::int32_t next = out[static_cast<std::size_t>(i)] + 1;
    for_each_neighbor(grid, i, connectivity, [&](std::int64_t j, core::Dir8 /*d*/) {
      const auto k = static_cast<std::size_t>(j);
      if (out[k] == kUnreached && detail::in_mask(passable, j)) {
        out[k] = next;
        queue[tail++] = j;
      }
    });
  }
  return {};
}

core::Status edt_squared(std::span<const std::uint8_t> seeds, const core::Grid& grid,
                         std::span<double> out, core::Executor& exec,
                         core::MemoryResource& resource) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(seeds, grid, "edt_squared: seeds must have one value per cell"),
       detail::check_field(out, grid, "edt_squared: out must have one value per cell")});
  if (!status.ok()) {
    return status;
  }
  const std::int32_t n = grid.n;
  const auto row = static_cast<std::size_t>(n);
  const auto workers = static_cast<std::size_t>(exec.concurrency());
  core::Expected<core::Arena> arena = core::Arena::create(
      workers * (detail::arena_bytes<double>(row) + detail::arena_bytes<std::int32_t>(row) +
                 detail::arena_bytes<double>(row + 1)),
      resource);
  if (!arena) {
    return std::move(arena.error());
  }
  core::Expected<std::span<double>> f_all = arena->allocate<double>(workers * row);
  core::Expected<std::span<std::int32_t>> v_all = arena->allocate<std::int32_t>(workers * row);
  core::Expected<std::span<double>> z_all = arena->allocate<double>(workers * (row + 1));
  if (!f_all || !v_all || !z_all) {
    return core::make_status(core::StatusCode::out_of_memory, "edt_squared: no scratch memory");
  }

  // Column pass: squared distance to the nearest seed in the same column, from a forward and a
  // backward sweep. inf + 1 stays inf for columns without a seed.
  const auto columns = [&](const core::ChunkRange& r) {
    for (std::int64_t xx = r.begin; xx < r.end; ++xx) {
      const auto x = static_cast<std::int32_t>(xx);
      double d = kInf;
      for (std::int32_t y = 0; y < n; ++y) {
        const auto i = static_cast<std::size_t>(grid.idx(x, y));
        d = seeds[i] != 0 ? 0.0 : d + 1.0;
        out[i] = d;
      }
      d = kInf;
      for (std::int32_t y = n - 1; y >= 0; --y) {
        const auto i = static_cast<std::size_t>(grid.idx(x, y));
        d = seeds[i] != 0 ? 0.0 : d + 1.0;
        const double best = std::min(out[i], d);
        out[i] = best * best;
      }
    }
  };
  status = core::parallel_for(exec, 0, n, detail::kRowGrain, columns);
  if (!status.ok()) {
    return status;
  }

  // Row pass: lower envelope of the column distances along each row.
  const auto rows = [&](const core::ChunkRange& r) {
    const auto w = static_cast<std::size_t>(r.worker);
    const EnvelopeScratch s{.f = f_all->subspan(w * row, row),
                            .v = v_all->subspan(w * row, row),
                            .z = z_all->subspan(w * (row + 1), row + 1)};
    for (std::int64_t y = r.begin; y < r.end; ++y) {
      const std::span<double> line = out.subspan(static_cast<std::size_t>(y) * row, row);
      std::copy(line.begin(), line.end(), s.f.begin());
      envelope_1d(s, line);
    }
  };
  return core::parallel_for(exec, 0, n, detail::kRowGrain, rows);
}

core::Status edt(std::span<const std::uint8_t> seeds, const core::Grid& grid,
                 std::span<float> out, core::Executor& exec,
                 core::MemoryResource& resource) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(out, grid, "edt: out must have one value per cell")});
  if (!status.ok()) {
    return status;
  }
  const auto cells = static_cast<std::size_t>(grid.cells());
  core::Expected<core::Arena> arena =
      core::Arena::create(detail::arena_bytes<double>(cells), resource);
  if (!arena) {
    return std::move(arena.error());
  }
  core::Expected<std::span<double>> squared = arena->allocate<double>(cells);
  if (!squared) {
    return std::move(squared.error());
  }
  status = edt_squared(seeds, grid, *squared, exec, resource);
  if (!status.ok()) {
    return status;
  }
  const std::span<const double> sq = *squared;
  const std::int64_t n = grid.n;
  const auto body = [&](const core::ChunkRange& r) {
    for (std::int64_t i = r.begin * n; i < r.end * n; ++i) {
      const auto k = static_cast<std::size_t>(i);
      out[k] = static_cast<float>(std::sqrt(sq[k]));
    }
  };
  return core::parallel_for(exec, 0, n, detail::kRowGrain, body);
}

}  // namespace wg::num
