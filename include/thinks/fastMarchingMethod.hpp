#ifndef THINKS_FMM_FASTMARCHINGMETHOD_HPP_INCLUDED
#define THINKS_FMM_FASTMARCHINGMETHOD_HPP_INCLUDED

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <numeric>
#include <queue>
#include <sstream>
#include <stack>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>


namespace thinks {
namespace fmm {
namespace detail {

//! Returns the product of the elements in array @a a.
//! Note: Not checking for integer overflow here!
template<std::size_t N> inline
std::size_t LinearSize(std::array<std::size_t, N> const& a)
{
  using namespace std;

  return accumulate(begin(a), end(a), size_t{1}, multiplies<size_t>());
}


//! Returns x * x.
template<typename T> inline constexpr
T Squared(T const x)
{
  return x * x;
}


//! Returns 1 / (x * x).
template<typename T> inline constexpr
T InverseSquared(T const x)
{
  using namespace std;

  static_assert(is_floating_point<T>::value,
                "scalar type must be floating point");

  return T{1} / Squared(x);
}

//! Returns element-wise 1 / (a * a).
template<typename T, std::size_t N> inline
std::array<T, N> InverseSquared(std::array<T, N> const& a)
{
  using namespace std;

  static_assert(is_floating_point<T>::value,
                "scalar type must be floating point");

  auto r = array<T, N>();
  transform(begin(a), end(a), begin(r),
            [](T const x) { return InverseSquared(x); });
  return r;
}


//! Returns true if @a index is inside @a size, otherwise false.
template<std::size_t N> inline
bool Inside(
  std::array<std::int32_t, N> const& index,
  std::array<std::size_t, N> const& size)
{
  using namespace std;

  for (auto i = size_t{0}; i < N; ++i) {
    // Cast is safe since we check that index[i] is greater than or
    // equal to zero first.
    if (!(0 <= index[i] && static_cast<size_t>(index[i]) < size[i])) {
      return false;
    }
  }

  return true;
}


//! Returns a string representation of the array @a a.
template<typename T, std::size_t N>
std::string ToString(std::array<T, N> const& a)
{
  using namespace std;

  auto ss = stringstream();
  ss << "[";
  for (auto i = size_t{0}; i < N; ++i) {
    ss << a[i];
    if (i != (N - 1)) {
      ss << ", ";
    }
  }
  ss << "]";

  return ss.str();
}


//! Throws a std::runtime_error if one or more of the elements in
//! @a grid_size is zero.
template<std::size_t N> inline
void ThrowIfZeroElementInGridSize(std::array<std::size_t, N> const& grid_size)
{
  using namespace std;

  if (find_if(begin(grid_size), end(grid_size),
              [](auto const x) { return x == size_t{0}; }) != end(grid_size)) {
    auto ss = stringstream();
    ss << "invalid grid size: " << ToString(grid_size);
    throw runtime_error(ss.str());
  }
}


//! Throws a std::runtime_error if the linear size of @a grid_size is not equal
//! to @a cell_buffer_size.
template<std::size_t N> inline
void ThrowIfInvalidCellBufferSize(
  std::array<std::size_t, N> const& grid_size,
  std::size_t const cell_buffer_size)
{
  using namespace std;

  if (LinearSize(grid_size) != cell_buffer_size) {
    auto ss = stringstream();
    ss << "grid size " << ToString(grid_size)
       << " does not match cell buffer size " << cell_buffer_size;
    throw runtime_error(ss.str());
  }
}


//! Throws a std::runtime_error if one or more of the elements in
//! @a grid_spacing is less than or equal to zero (or NaN).
template<typename T, std::size_t N> inline
void ThrowIfInvalidGridSpacing(std::array<T, N> const& grid_spacing)
{
  using namespace std;

  if (find_if(begin(grid_spacing), end(grid_spacing),
              [](auto const x) { return isnan(x) || x <= T(0); }) != end(grid_spacing)) {
    auto ss = stringstream();
    ss << "invalid grid spacing: " << ToString(grid_spacing);
    throw runtime_error(ss.str());
  }
}


//! Throws a std::runtime_error if @a speed is less than or equal
//! to zero or NaN.
template<typename T> inline
void ThrowIfZeroOrNegativeOrNanSpeed(T const speed)
{
  using namespace std;

  if (isnan(speed) || speed <= T{0}) {
    auto ss = stringstream();
    ss << "invalid speed: " << speed;
    throw runtime_error(ss.str());
  }
}


//! Returns an array that can be used to transform an N-dimensional index
//! into a linear index.
template<std::size_t N>
std::array<std::size_t, N - 1> GridStrides(
  std::array<std::size_t, N> const& grid_size)
{
  using namespace std;

  array<size_t, N - 1> strides;
  auto stride = size_t{1};
  for (auto i = size_t{1}; i < N; ++i) {
    stride *= grid_size[i - 1];
    strides[i - 1] = stride;
  }
  return strides;
}


//! Returns a linear (scalar) index into an array representing an
//! N-dimensional grid for integer coordinate @a index.
//! Note that this function does not check for integer overflow!
template<std::size_t N> inline
std::size_t GridLinearIndex(
  std::array<std::int32_t, N> const& index,
  std::array<std::size_t, N - 1> const& grid_strides)
{
  using namespace std;

  auto k = static_cast<size_t>(index[0]);
  for (auto i = size_t{1}; i < N; ++i) {
    k += index[i] * grid_strides[i - 1];
  }
  return k;
}


//! Allows accessing a linear array as if it where a higher dimensional
//! object. Allows mutating operations on the underlying array.
template<typename T, std::size_t N>
class Grid
{
public:
  typedef T CellType;
  typedef std::array<std::size_t, N> SizeType;
  typedef std::array<std::int32_t, N> IndexType;

  //! Construct a grid from a given @a size and @a cell_buffer. Does not
  //! take ownership of the cell buffer, it is assumed that this buffer exists
  //! during the life-time of the grid object.
  //!
  //! Throws a std::runtime_error if:
  //! - size evaluates to a zero linear size, i.e. if any of the elements are zero.
  //! - the linear size of @a size is not equal to the @a cell_buffer size.
  Grid(SizeType const& size, std::vector<T>& cell_buffer)
    : size_(size)
    , strides_(GridStrides(size))
    , cells_(&cell_buffer.front())
  {
    ThrowIfZeroElementInGridSize(size);
    ThrowIfInvalidCellBufferSize(size, cell_buffer.size());
  }

  SizeType size() const
  {
    return size_;
  }

  //! Returns a reference to the cell at @a index. No range checking!
  CellType& Cell(IndexType const& index)
  {
    assert(GridLinearIndex(index, strides_) < LinearSize(size()));
    return cells_[GridLinearIndex(index, strides_)];
  }

  //! Returns a const reference to the cell at @a index. No range checking!
  CellType const& Cell(IndexType const& index) const
  {
    assert(GridLinearIndex(index, strides_) < LinearSize(size()));
    return cells_[GridLinearIndex(index, strides_)];
  }

private:
  std::array<std::size_t, N> const size_;
  std::array<std::size_t, N - 1> const strides_;
  CellType* const cells_;
};


//! Allows const accessing a linear array as if it where a higher
//! dimensional object. The underlying array cannot be changed through this
//! interface.
template<typename T, std::size_t N>
class ConstGrid
{
public:
  typedef T CellType;
  typedef std::array<std::size_t, N> SizeType;
  typedef std::array<std::int32_t, N> IndexType;

  //! Construct a grid from a given @a size and @a cell_buffer. Does not
  //! take ownership of the cell buffer, it is assumed that this buffer exists
  //! during the life-time of the grid object.
  //!
  //! Throws a std::runtime_error if:
  //! - size evaluates to a zero linear size, i.e. if any of the elements are zero.
  //! - the linear size of @a size is not equal to the @a cell_buffer size.
  ConstGrid(SizeType const& size, std::vector<T> const& cell_buffer)
    : size_(size)
    , strides_(GridStrides(size))
    , cells_(&cell_buffer.front())
  {
    ThrowIfZeroElementInGridSize(size);
    ThrowIfInvalidCellBufferSize(size, cell_buffer.size());
  }

  SizeType size() const
  {
    return size_;
  }

  //! Returns a const reference to the cell at @a index. No range checking!
  CellType const& Cell(IndexType const& index) const
  {
    assert(GridLinearIndex(index, strides_) < LinearSize(size()));
    return cells_[GridLinearIndex(index, strides_)];
  }

private:
  std::array<std::size_t, N> const size_;
  std::array<std::size_t, N - 1> const strides_;
  CellType const* const cells_;
};


template<typename T, std::size_t N>
class NarrowBandStore
{
public:
  typedef T DistanceType;
  typedef std::array<std::int32_t, N> IndexType;
  typedef std::pair<DistanceType, IndexType> ValueType;

  NarrowBandStore()
  {}

  bool empty() const
  {
    return min_heap_.empty();
  }

  ValueType Pop()
  {
    assert(!min_heap_.empty());
    auto const v = min_heap_.top(); // O(1)
    min_heap_.pop(); // O(log N)
    return v;
  }

  void Push(ValueType const& value)
  {
    min_heap_.push(value); // O(log N)
  }

private:
  struct HeapComparison_
  {
    bool operator()(ValueType const& lhs, ValueType const& rhs)
    {
      return lhs.first > rhs.first;
    }
  };

  typedef std::priority_queue<
    ValueType,
    std::vector<ValueType>,
    HeapComparison_> MinHeap_;

  MinHeap_ min_heap_;
};


//! Returns base^exponent as a compile-time constant.
constexpr std::size_t inline
static_pow(std::size_t const base, std::size_t const exponent)
{
  using namespace std;

  // NOTE: Cannot use loops in constexpr functions in C++11, have to use
  // recursion here.
  return exponent == size_t{0} ?
    size_t{1} :
    base * static_pow(base, exponent - 1);
}


template<std::size_t N>
class IndexIterator
{
public:
  explicit IndexIterator(std::array<std::size_t, N> const& size)
    : size_(size)
  {
    using namespace std;

    if (LinearSize(size) == size_t{0}) {
      throw runtime_error("zero size element");
    }

    fill(begin(index_), end(index_), int32_t{0});
  }

  std::array<std::int32_t, N> index() const
  {
    return index_;
  }

  bool Next()
  {
    using namespace std;

    auto i = int32_t{N - 1};
    while (i >= 0) {
      assert(size_[i] > size_t{0});
      if (static_cast<size_t>(index_[i]) < size_[i] - 1) {
        ++index_[i];
        return true;
      }
      else {
        index_[i--] = 0;
      }
    }
    return false;
  }

private:
  std::array<std::size_t, N> const size_;
  std::array<std::int32_t, N> index_;
};


template<std::size_t N> inline
std::array<std::array<std::int32_t, N>, static_pow(3, N) - 1> VertexNeighborOffsets()
{
  using namespace std;

  auto neighbor_offsets = array<array<int32_t, N>, static_pow(3, N) - 1>();
  auto offset_index = size_t{0};
  auto index_size = array<size_t, N>{};
  fill(begin(index_size), end(index_size), size_t{3});
  auto index_iter = IndexIterator<N>(index_size);
  auto valid_index = true;
  while (valid_index) {
    auto offset = index_iter.index();
    for_each(begin(offset), end(offset), [](auto& d) { d -= int32_t{1}; });
    if (!all_of(begin(offset), end(offset), [](auto const i){ return i == 0; })) {
      neighbor_offsets[offset_index++] = offset;
    }
    valid_index = index_iter.Next();
  }
  assert(offset_index == static_pow(3, N) - 1);

  return neighbor_offsets;
}


template<std::size_t N> inline
std::array<std::array<std::int32_t, N>, 2 * N> FaceNeighborOffsets()
{
  using namespace std;

  auto neighbor_offsets = array<array<int32_t, N>, size_t{2} * N>{};
  for (auto i = size_t{0}; i < N; ++i) {
    for (auto j = size_t{0}; j < N; ++j) {
      if (j == i) {
        neighbor_offsets[2 * i + 0][j] = int32_t{+1};
        neighbor_offsets[2 * i + 1][j] = int32_t{-1};
      }
      else {
        neighbor_offsets[2 * i + 0][j] = int32_t{0};
        neighbor_offsets[2 * i + 1][j] = int32_t{0};
      }
    }
  }
  return neighbor_offsets;
}


template<std::size_t N, typename NeighborOffsetIt> inline
void ConnectedComponents(
  std::vector<std::array<std::int32_t, N>> const& indices,
  std::array<std::size_t, N> const& grid_size,
  NeighborOffsetIt const neighbor_offset_begin,
  NeighborOffsetIt const neighbor_offset_end,
  std::vector<std::vector<std::array<std::int32_t, N>>>* connected_components)
{
  using namespace std;

  assert(LinearSize(grid_size) > size_t{0});
  assert(connected_components != nullptr);

  connected_components->clear();
  if (indices.empty()) {
    return;
  }

  enum class LabelCell : uint8_t {
    kBackground = uint32_t{0},
    kForeground,
    kLabelled
  };

  auto label_buffer =
    vector<LabelCell>(LinearSize(grid_size), LabelCell::kBackground);
  auto label_grid = Grid<LabelCell, N>(grid_size, label_buffer.front());

  for (auto const& index : indices) {
    assert(Inside(index, label_grid.size()));
    label_grid.Cell(index) = LabelCell::kForeground;
  }

  for (auto const& index : indices) {
    assert(Inside(index, label_grid.size()));
    assert(label_grid.Cell(index) == LabelCell::kForeground ||
           label_grid.Cell(index) == LabelCell::kLabelled);
    // Check if this index has been labelled already.
    if (label_grid.Cell(index) == LabelCell::kForeground) {
      // Start a new component.
      label_grid.Cell(index) = LabelCell::kLabelled;
      auto component = vector<array<int32_t, N>>();
      component.push_back(index);
      auto neighbor_indices = stack<array<int32_t, N>>();
      neighbor_indices.push(index);

      // Flood-fill current label.
      while (!neighbor_indices.empty()) {
        auto const top_neighbor_index = neighbor_indices.top();
        neighbor_indices.pop();
        for (auto neighbor_offset_iter = neighbor_offset_begin;
             neighbor_offset_iter != neighbor_offset_end; ++neighbor_offset_iter) {
          // Offset neighbor index.
          auto neighbor_index = top_neighbor_index;
          for (auto i = size_t{0}; i < N; ++i) {
            neighbor_index[i] += (*neighbor_offset_iter)[i];
          }

          if (Inside(neighbor_index, label_grid.size()) &&
              label_grid.Cell(neighbor_index) == LabelCell::kForeground) {
            // Mark neighbor as labelled, store in component and add
            // to list of indices whose neighbors we should check.
            label_grid.Cell(neighbor_index) = LabelCell::kLabelled;
            component.push_back(neighbor_index);
            neighbor_indices.push(neighbor_index);
          }
        }
      }
      connected_components->push_back(component);
    }
  }
}


template<std::size_t N> inline
void DilationBands(
  std::vector<std::array<std::int32_t, N>> const& indices,
  std::array<std::size_t, N> const& grid_size,
  std::vector<std::vector<std::array<std::int32_t, N>>>* dilation_bands)
{
  using namespace std;

  assert(LinearSize(grid_size) > size_t{0});
  assert(dilation_bands != nullptr);

  dilation_bands->clear();
  if (indices.empty()) {
    return;
  }

  enum class DilationCell : uint8_t {
    kBackground = uint32_t{0},
    kForeground,
    kDilated
  };

  // Dilation grid is padded one cell in each dimension (positive and negative).
  auto dilation_grid_size = grid_size;
  for_each(begin(dilation_grid_size), end(dilation_grid_size),
           [](auto& d) { d += 2; });

  auto dilation_buffer =
    vector<DilationCell>(LinearSize(dilation_grid_size), DilationCell::kBackground);
  auto dilation_grid =
    Grid<DilationCell, N>(dilation_grid_size, dilation_buffer.front());

  // Set foreground.
  for (auto const& index : indices) {
    assert(Inside(index, grid_size));
    auto dilation_index = index;
    for_each(begin(dilation_index), end(dilation_index),
             [](auto& d) { d += int32_t{1}; });
    dilation_grid.Cell(dilation_index) = DilationCell::kForeground;
  }

  // Add dilated cell indices.
  auto const dilation_neighbor_offsets = VertexNeighborOffsets<N>();
  auto dilation_indices = vector<array<int32_t, N>>();
  for (auto const& grid_index : indices) {
    assert(Inside(grid_index, grid_size));
    auto dilation_index = grid_index;
    for_each(begin(dilation_index), end(dilation_index),
             [](auto& d) { d += int32_t{1}; });
    assert(dilation_grid.Cell(dilation_index) == DilationCell::kForeground);

    for (auto const dilation_neighbor_offset : dilation_neighbor_offsets) {
      auto neighbor_index = dilation_index;
      for (auto i = size_t{0}; i < N; ++i) {
        neighbor_index[i] += dilation_neighbor_offset[i];
      }

      if (dilation_grid.Cell(neighbor_index) == DilationCell::kBackground) {
        dilation_grid.Cell(neighbor_index) = DilationCell::kDilated;
        dilation_indices.push_back(neighbor_index);
      }
    }
  }

  auto const dilation_bands_neighbor_offsets = FaceNeighborOffsets<N>();
  auto connected_dilation_components = vector<vector<array<int32_t, N>>>();
  ConnectedComponents(
    dilation_indices,
    dilation_grid_size,
    begin(dilation_bands_neighbor_offsets),
    end(dilation_bands_neighbor_offsets),
    &connected_dilation_components);

  for (auto const& dilation_component : connected_dilation_components) {
    auto dilation_band = vector<array<int32_t, N>>();
    for (auto const& dilation_index : dilation_component) {
      auto grid_index = dilation_index;
      for_each(begin(grid_index), end(grid_index),
               [](auto& d) { d -= int32_t{1}; });
      if (Inside(grid_index, grid_size)) {
        dilation_band.push_back(grid_index);
      }
    }

    if (!dilation_band.empty()) {
      dilation_bands->push_back(dilation_band);
    }
  }
}


template<std::size_t N> inline
std::array<std::pair<std::int32_t, std::int32_t>, N> BoundingBox(
  std::vector<std::array<std::int32_t, N>> const& indices)
{
  using namespace std;

  if (indices.empty()) {
    throw runtime_error("cannot compute bounding box from empty indices");
  }

  array<pair<int32_t, int32_t>, N> bbox;

  // Initialize bounding box in all dimensions.
  for (auto i = size_t{0}; i < N; ++i) {
    bbox[i].first = numeric_limits<int32_t>::max();
    bbox[i].second = numeric_limits<int32_t>::min();
  }

  // Check each index in all dimensions.
  for (auto const& index : indices) {
    for (auto i = size_t{0}; i < N; ++i) {
      bbox[i].first = min(bbox[i].first, index[i]);
      bbox[i].second = max(bbox[i].second, index[i]);
    }
  }

  return bbox;
}


template<std::size_t N> inline
std::size_t HyperVolume(
  std::array<std::pair<std::int32_t, std::int32_t>, N> const& bbox)
{
  auto hyper_volume = size_t{1};
  for (auto i = size_t{0}; i < N; ++i) {
    assert(bbox[i].first <= bbox[i].second);
    hyper_volume *= (bbox[i].second - bbox[i].first + 1);
  }
  return hyper_volume;
}


template<std::size_t N> inline
void InitialUnsignedNarrowBand(
  std::vector<std::array<std::int32_t, N>> const& frozen_indices,
  std::array<std::size_t, N> const& grid_size,
  std::vector<std::array<std::int32_t, N>>* narrow_band_indices)
{
  using namespace std;

  assert(!frozen_indices.empty());
  assert(narrow_band_indices != nullptr);
  narrow_band_indices->clear();

  enum class NarrowBandCell : uint8_t {
    kBackground = uint8_t{0},
    kFrozen,
    kNarrowBand
  };

  auto narrow_band_buffer =
    vector<NarrowBandCell>(LinearSize(grid_size), NarrowBandCell::kBackground);
  auto narrow_band_grid = Grid<NarrowBandCell, N>(grid_size, narrow_band_buffer);

  // Set frozen cells.
  for (auto const& frozen_index : frozen_indices) {
    assert(Inside(frozen_index, narrow_band_grid.size()));
    narrow_band_grid.Cell(frozen_index) = NarrowBandCell::kFrozen;
  }

  // Find face neighbors of frozen cells, which then become
  // the initial narrow band.
  auto const kNeighborOffsets = array<int32_t, 2>{{-1, 1}};
  for (auto const& frozen_index : frozen_indices) {
    for (auto i = size_t{0}; i < N; ++i) {
      for (auto const neighbor_offset : kNeighborOffsets) {
        auto neighbor_index = frozen_index;
        neighbor_index[i] += neighbor_offset;

        if (Inside(neighbor_index, narrow_band_grid.size())) {
          auto& neighbor_cell = narrow_band_grid.Cell(neighbor_index);
          if (neighbor_cell == NarrowBandCell::kBackground) {
            neighbor_cell = NarrowBandCell::kNarrowBand;
            narrow_band_indices->push_back(neighbor_index);
          }
        }
      }
    }
  }
}


template<std::size_t N> inline
void InitialSignedNarrowBands(
  std::vector<std::array<std::int32_t, N>> const& frozen_indices,
  std::array<std::size_t, N> const& grid_size,
  std::vector<std::array<std::int32_t, N>>* inside_narrow_band_indices,
  std::vector<std::array<std::int32_t, N>>* outside_narrow_band_indices)
{
  using namespace std;

  assert(inside_narrow_band_indices != nullptr);
  assert(outside_narrow_band_indices != nullptr);
  inside_narrow_band_indices->clear();
  outside_narrow_band_indices->clear();

  auto connected_components = vector<vector<array<int32_t, N>>>();
  auto const cc_neighbor_offsets = VertexNeighborOffsets<N>();
  ConnectedComponents(
    frozen_indices,
    grid_size,
    begin(cc_neighbor_offsets),
    end(cc_neighbor_offsets),
    &connected_components);
  assert(!connected_components.empty());

  /*
  // TODO: check for contained components!
  auto cc_bbox = vector<pair<size_t, array<pair<int32_t, int32_t>, N>>>();
  for (auto i = size_t{0}; i < connected_components.size(); ++i) {
    cc_bbox.push_back({i, BoundingBox(connected_components[i])});
  }
  sort(
    begin(cc_bbox),
    end(cc_bbox),
    [](auto const lhs, auto const rhs) {
      return HyperVolume(lhs.second) > HyperVolume(rhs.second);
    });
  for (auto i = size_t{1}; i < connected_components.size(); ++i) {
    if (Contains(cc_bbox[0].second, cc_bbox[i].second)) {
      throw runtime_error("contained connected component");
    }
  }
  */

  enum class NarrowBandCell : uint8_t {
    kBackground = uint8_t{0},
    kFrozen,
    kNarrowBand
  };

  auto narrow_band_buffer =
    vector<NarrowBandCell>(LinearSize(grid_size), NarrowBandCell::kBackground);
  auto narrow_band_grid =
    Grid<NarrowBandCell, N>(grid_size, narrow_band_buffer.front());

  // Set frozen cells.
  for (auto const& frozen_index : frozen_indices) {
    assert(Inside(frozen_index, narrow_band_grid.size()));
    narrow_band_grid.Cell(frozen_index) = NarrowBandCell::kFrozen;
  }

  for (auto const& connected_component : connected_components) {
    auto dilation_bands = vector<vector<array<int32_t, N>>>();
    DilationBands(connected_component, grid_size, &dilation_bands);
    assert(!dilation_bands.empty());
    if (dilation_bands.size() == 1) {
      throw runtime_error("open connected component");
#if 0
      if (connected_component.size() == 1) {

      }
      else {
      }
#endif
    }
    else {
      auto dilation_band_areas = vector<pair<size_t, size_t>>();
      for (auto i = size_t{0}; i < dilation_bands.size(); ++i) {
        dilation_band_areas.push_back(
          {i, HyperVolume(BoundingBox(dilation_bands[i]))});
      }
      sort(
        begin(dilation_band_areas),
        end(dilation_band_areas),
        [](auto const lhs, auto const rhs) {
          return lhs.second > rhs.second;
        });

      const auto kNeighborOffsets = array<int32_t, 2>{{-1, 1}};

      // Outer dilation bands of several connected components may overlap.
      auto const& outer_dilation_band =
        dilation_bands[dilation_band_areas[0].first];
      for (auto const& dilation_index : outer_dilation_band) {
        assert(Inside(dilation_index, narrow_band_grid.size()));
        assert(narrow_band_grid.Cell(dilation_index) != NarrowBandCell::kFrozen);
        if (narrow_band_grid.Cell(dilation_index) == NarrowBandCell::kBackground) {
          auto i = size_t{0};
          auto frozen_neighbor_found = false;
          while (i < N && !frozen_neighbor_found) {
            auto j = size_t{0};
            while (j < kNeighborOffsets.size() && !frozen_neighbor_found) {
              auto neighbor_index = dilation_index;
              neighbor_index[i] += kNeighborOffsets[j];
              if (narrow_band_grid.Cell(neighbor_index) == NarrowBandCell::kFrozen) {
                frozen_neighbor_found = true;
                narrow_band_grid.Cell(dilation_index) = NarrowBandCell::kNarrowBand;
                outside_narrow_band_indices->push_back(dilation_index);
              }
              ++j;
            }
            ++i;
          }
        }
      }

      // Inner dilation bands cannot overlap for difference connected components.
      for (auto k = size_t{1}; k < dilation_bands.size(); ++k) {
        auto const& inner_dilation_band =
          dilation_bands[dilation_band_areas[k].first];
        for (auto const& dilation_index : inner_dilation_band) {
          assert(Inside(dilation_index, narrow_band_grid.size()));
          assert(narrow_band_grid.Cell(dilation_index) == NarrowBandCell::kBackground);
          auto i = size_t{0};
          auto frozen_neighbor_found = false;
          while (i < N && !frozen_neighbor_found) {
            auto j = size_t{0};
            while (j < kNeighborOffsets.size() && !frozen_neighbor_found) {
              auto neighbor_index = dilation_index;
              neighbor_index[i] += kNeighborOffsets[j];
              if (narrow_band_grid.Cell(neighbor_index) == NarrowBandCell::kFrozen) {
                frozen_neighbor_found = true;
                narrow_band_grid.Cell(dilation_index) = NarrowBandCell::kNarrowBand;
                inside_narrow_band_indices->push_back(dilation_index);
              }
              ++j;
            }
            ++i;
          }
        }
      }
    }
  }
}


//! Returns true if the value @a d indicates that a distance cell should
//! be considered frozen, otherwise false.
template <typename T> inline
bool frozen(T const d)
{
  using namespace std;

  static_assert(is_floating_point<T>::value,
                "scalar type must be floating point");

  return d < numeric_limits<T>::max();
}


template<typename T, std::size_t N, typename E> inline
std::unique_ptr<NarrowBandStore<T, N>>
InitialUnsignedNarrowBand(
  std::vector<std::array<std::int32_t, N>> const& frozen_indices,
  Grid<T, N> const& distance_grid,
  E const& eikonal_solver)
{
  using namespace std;

  assert(!frozen_indices.empty());

  enum class NarrowBandCell : uint8_t {
    kBackground = uint8_t{0},
    kNarrowBand
  };

  // Use a temporary grid to avoid adding cells with multiple frozen
  // neighbors multiple times to the narrow band.
  auto narrow_band_buffer =
    vector<NarrowBandCell>(LinearSize(distance_grid.size()),
                           NarrowBandCell::kBackground);
  auto narrow_band_grid =
    Grid<NarrowBandCell, N>(distance_grid.size(), narrow_band_buffer);

  auto narrow_band =
    unique_ptr<NarrowBandStore<T, N>>(new NarrowBandStore<T, N>());

  // Find face neighbors of frozen cells, which then become
  // the initial narrow band.
  auto const kNeighborOffsets = array<int32_t, 2>{{-1, 1}};
  for (auto const& frozen_index : frozen_indices) {
    assert(Inside(frozen_index, distance_grid.size()));
    assert(frozen(distance_grid.Cell(frozen_index)));

    for (auto i = size_t{0}; i < N; ++i) {
      for (auto const neighbor_offset : kNeighborOffsets) {
        auto neighbor_index = frozen_index;
        neighbor_index[i] += neighbor_offset;

        if (Inside(neighbor_index, narrow_band_grid.size())) {
          assert(!frozen(distance_grid.Cell(neighbor_index)));

          auto& neighbor_cell = narrow_band_grid.Cell(neighbor_index);
          if (neighbor_cell == NarrowBandCell::kBackground) {
            narrow_band->Push({
              eikonal_solver.Solve(neighbor_index, distance_grid),
              neighbor_index});
            neighbor_cell = NarrowBandCell::kNarrowBand;
          }
        }
      }
    }
  }

  assert(!narrow_band->empty());

  return narrow_band;
}



//! Set frozen cell state and distance on respective grids.
//! Throws std::runtime_error if:
//! - Frozen indices are empty, or
//! - Not the same number of indices and distances, or
//! - Any index is outside the distance grid, or
//! - Any duplicate indices, or
//! - Any distance does not pass the predicate test, or
//! - The whole grid is frozen.
template <typename T, std::size_t N, typename D> inline
void SetBoundaryCondition(
  std::vector<std::array<std::int32_t, N>> const& indices,
  std::vector<T> const& distances,
  T const multiplier,
  D const distance_predicate,
  Grid<T, N>* const distance_grid)
{
  using namespace std;

  assert(distance_grid != nullptr);

  if (indices.empty()) {
    throw runtime_error("empty frozen indices");
  }

  if (indices.size() != distances.size()) {
    throw runtime_error("frozen indices/distances size mismatch");
  }

  for (auto i = size_t{0}; i < indices.size(); ++i) {
    auto const index = indices[i];
    auto const distance = distances[i];
    if (!Inside(index, distance_grid->size())) {
      auto ss = stringstream();
      ss << "frozen index outside grid: " << ToString(index);
      throw runtime_error(ss.str());
    }

    if (!distance_predicate(distance)) {
      auto ss = stringstream();
      ss << "invalid frozen distance: " << distance;
      throw runtime_error(ss.str());
    }

    auto& distance_cell = distance_grid->Cell(index);
    if (frozen(distance_cell)) {
      auto ss = stringstream();
      ss << "duplicate frozen index: " << ToString(index);
      throw runtime_error(ss.str());
    }
    distance_cell = multiplier * distance;
  }

  // Here we know that all frozen indices are unique and inside the grid.
  if (indices.size() == LinearSize(distance_grid->size())) {
    throw std::runtime_error("whole grid frozen");
  }
}


//! Compute distances using the @a eikonal_solver for the face neighbors of
//! @a index. These are not written to the @a distance_grid, but are instead
//! stored in the @a narrow_band.
template <typename T, std::size_t N, typename E> inline
void UpdateNeighbors(
  std::array<std::int32_t, N> const& index,
  E const& eikonal_solver,
  Grid<T, N>* const distance_grid,
  NarrowBandStore<T, N>* const narrow_band)
{
  using namespace std;

  assert(distance_grid != nullptr);
  assert(narrow_band != nullptr);
  assert(Inside(index, distance_grid->size()));

  // Update the narrow band.
  const auto kNeighborOffsets = array<int32_t, 2>{{-1, 1}};
  for (auto i = size_t{0}; i < N; ++i) {
    for (auto const neighbor_offset : kNeighborOffsets) {
      auto neighbor_index = index;
      neighbor_index[i] += neighbor_offset;

      if (Inside(neighbor_index, distance_grid->size())) {
        // If the neighbor is not frozen compute a distance for it.
        // Note that we don't check if there is an entry for this index
        // in the narrow band already. If we happen to insert multiple
        // distances for the same index the smallest one will be frozen first
        // when marching and the larger distances will be ignored.
        auto& distance_cell = distance_grid->Cell(neighbor_index);
        if (!frozen(distance_cell)) {
          auto const distance = eikonal_solver.Solve(
            neighbor_index,
            *distance_grid);
          narrow_band->Push({distance, neighbor_index});
        }
      }
    }
  }
}


//! Compute distances for all cells in @a distance_grid. Starting from the
//! initial indices in @a narrow_band, freeze the smallest distance and update
//! the (non-frozen) neighbor distances for that cell and add the neighbors
//! to the @a narrow_band. Repeat this process until there are no non-frozen
//! cells in @a distance_grid.
template <typename T, std::size_t N, typename E> inline
void MarchNarrowBand(
  E const& eikonal_solver,
  NarrowBandStore<T, N>* const narrow_band,
  Grid<T, N>* const distance_grid)
{
  using namespace std;

  assert(distance_grid != nullptr);
  assert(narrow_band != nullptr);

  while (!narrow_band->empty()) {
    // Take smallest distance from the narrow band and freeze it.
    auto const narrow_band_cell = narrow_band->Pop();
    auto const distance = narrow_band_cell.first;
    auto const index = narrow_band_cell.second;

    auto& distance_cell = distance_grid->Cell(index);

    // Since we allow multiple distances for the same index in the narrow band
    // it could be that the distance for this grid cell has already been
    // frozen. In that case just ignore subsequent values from the narrow band
    // for that grid cell and move on.
    if (!frozen(distance_cell)) {
      distance_cell = distance;
      assert(frozen(distance_cell));
      UpdateNeighbors(
        index,
        eikonal_solver,
        distance_grid,
        narrow_band);
    }
  }
}


//! Polynomial coefficients are equivalent to array index,
//! i.e. Sum(q[i] * x^i) = 0, for i in [0, 2], or simpler
//! q[0] + q[1] * x + q[2] * x^2 = 0.
//!
//! Returns the largest real root.
//!
//! Throws a runtime_error if no real roots exist.
template<typename T>
T SolveEikonalQuadratic(std::array<T, 3> const& q)
{
  using namespace std;

  static_assert(is_floating_point<T>::value,
                "quadratic coefficients must be floating point");

  assert(fabs(q[2]) > T(1e-9));

  auto const discriminant = q[1] * q[1] - T{4} * q[2] * q[0];
  if (discriminant < T{0}) {
    throw runtime_error("negative discriminant");
  }

  auto const root = (-q[1] + sqrt(discriminant)) / (T{2} * q[2]);
  assert(!isnan(root));

  if (root < T{0}) {
    throw runtime_error("negative distance");
  }

  return root;
}


template<typename T, std::size_t N>
T SolveEikonal(
  std::array<std::int32_t, N> const& index,
  Grid<T, N> const& distance_grid,
  T const speed,
  std::array<T, N> const& grid_spacing)
{
  using namespace std;

  static_assert(std::is_floating_point<T>::value,
                "scalar type must be floating point");

  assert(Inside(index, distance_grid.size()));

  auto const neighbor_offsets = array<int32_t, 2>{{-1, 1}};

  // Initialize quadratic coefficients.
  auto q = array<T, 3>{{T{-1} / Squared(speed), T{0}, T{0}}};

  // Find the smallest frozen neighbor (if any) in each dimension.
  for (auto i = size_t{0}; i < N; ++i) {
    auto neighbor_min_distance = numeric_limits<T>::max();

    // Check neighbors in both directions for this dimenion.
    for (auto const neighbor_offset : neighbor_offsets) {
      auto neighbor_index = index;
      neighbor_index[i] += neighbor_offset;
      if (Inside(neighbor_index, distance_grid.size())) {
        // Note that if the neighbor is not frozen it will have the default
        // distance numeric_limits<T>::max().
        auto const neighbor_distance = distance_grid.Cell(neighbor_index);
        if (neighbor_distance < neighbor_min_distance) {
          assert(frozen(distance_cell));
          neighbor_min_distance = neighbor_distance;
        }
      }
    }

    // Update quadratic coefficients for the current direction.
    // If no frozen neighbor was found that dimension does not contribute
    // to the coefficients.
    if (neighbor_min_distance < numeric_limits<T>::max()) {
      auto const inv_grid_spacing_squared = InverseSquared(grid_spacing[i]);
      q[0] += Squared(neighbor_min_distance) * inv_grid_spacing_squared;
      q[1] += T{-2} * neighbor_min_distance * inv_grid_spacing_squared;
      q[2] += inv_grid_spacing_squared;
    }
  }

  return SolveEikonalQuadratic(q);
}


template<typename T, std::size_t N>
T HighAccuracySolveEikonal(
  std::array<std::int32_t, N> const& index,
  Grid<T, N> const& distance_grid,
  T const speed,
  std::array<T, N> const& grid_spacing)
{
  using namespace std;

  static_assert(std::is_floating_point<T>::value,
                "scalar type must be floating point");

  assert(Inside(index, distance_grid.size()));

  static auto const neighbor_offsets = array<int32_t, 2>{{-1, 1}};

  // Initialize quadratic coefficients.
  auto q = array<T, 3>{{T{-1} / Squared(speed), T{0}, T{0}}};

  // Find the smallest frozen neighbor(s) (if any) in each dimension.
  for (auto i = size_t{0}; i < N; ++i) {
    auto neighbor_min_distance = numeric_limits<T>::max();
    auto neighbor_min_distance2 = numeric_limits<T>::max();

    // Check neighbors in both directions for this dimenion.
    for (auto const neighbor_offset : neighbor_offsets) {
      auto neighbor_index = index;
      neighbor_index[i] += neighbor_offset;
      if (Inside(neighbor_index, distance_grid.size())) {
        auto const neighbor_distance = distance_grid.Cell(neighbor_index);
        if (neighbor_distance < neighbor_min_distance) {
          // Neighbor one step away is frozen.
          neighbor_min_distance = neighbor_distance;

          // Check if neighbor two steps away is frozen and has smaller
          // (or equal) distance than neighbor one step away.
          auto neighbor_index2 = neighbor_index;
          neighbor_index2[i] += neighbor_offset;
          if (Inside(neighbor_index2, distance_grid.size())) {
            auto const neighbor_distance2 = distance_grid.Cell(neighbor_index2);
            if (neighbor_distance2 <= neighbor_distance) {
              // Neighbor index two steps away is frozen.
              neighbor_min_distance2 = neighbor_distance2;
            }
          }
        }
      }
    }

    // Update quadratic coefficients for the current direction.
    if (neighbor_min_distance < numeric_limits<T>::max()) {
      if (neighbor_min_distance2 < numeric_limits<T>::max()) {
        // Second order coefficients.
        auto const alpha = T{9} / (T{4} * Squared(grid_spacing[i]));
        auto const t = (T{1} / T{3}) * (T{4} * neighbor_min_distance - neighbor_min_distance2);
        q[0] += Squared(t) * alpha;
        q[1] += T{-2} * t * alpha;
        q[2] += alpha;
      }
      else {
        // First order coefficients.
        auto const inv_grid_spacing_squared = InverseSquared(grid_spacing[i]);
        q[0] += Squared(neighbor_min_distance) * inv_grid_spacing_squared;
        q[1] += T{-2} * neighbor_min_distance * inv_grid_spacing_squared;
        q[2] += inv_grid_spacing_squared;
      }
    }
  }

  return SolveEikonalQuadratic(q);
}


//! Base class for Eikonal solvers.
template <typename T, std::size_t N>
class EikonalSolverBase
{
public:
  typedef T ScalarType;
  static std::size_t const kDimension = N;

  explicit EikonalSolverBase(std::array<T, N> const& grid_spacing)
    : grid_spacing_(grid_spacing)
  {
    ThrowIfInvalidGridSpacing(grid_spacing_);
  }

  std::array<T, N> grid_spacing() const
  {
    return grid_spacing_;
  }

private:
  std::array<T, N> const grid_spacing_;
};


//! Base class for Eikonal solvers with uniform speed.
template <typename T, std::size_t N>
class UniformSpeedEikonalSolverBase : public EikonalSolverBase<T, N>
{
public:
  UniformSpeedEikonalSolverBase(
    std::array<T, N> const& grid_spacing,
    T const speed)
    : EikonalSolverBase<T, N>(grid_spacing)
    , speed_(speed)
  {
    ThrowIfZeroOrNegativeOrNanSpeed(speed_);
  }

  //! Returns the uniform speed, guaranteed to be:
  //! - Non-zero
  //! - Non-negative
  //! - Non-NaN
  T speed() const
  {
    return speed_;
  }

private:
  T const speed_;
};


//! Base class for Eikonal solvers with varying speed.
template <typename T, std::size_t N>
class VaryingSpeedEikonalSolverBase : public EikonalSolverBase<T, N>
{
public:
  VaryingSpeedEikonalSolverBase(
    std::array<T, N> const& grid_spacing,
    std::array<std::size_t, N> const& speed_grid_size,
    std::vector<T> const& speed_buffer)
    : EikonalSolverBase(grid_spacing)
    , speed_grid_(speed_grid_size, speed_buffer)
  {
    for (auto const speed : speed_buffer) {
      ThrowIfZeroOrNegativeOrNanSpeed(speed);
    }
  }

  //! Returns the speed at @a index, guaranteed to be:
  //! - Non-zero
  //! - Non-negative
  //! - Non-NaN
  //!
  //! Throws a std::runtime_error if @a index is outside the underlying grid.
  T Speed(std::array<std::int32_t, N> const& index) const
  {
    using namespace std;

    if (!Inside(index, speed_grid_.size())) {
      throw runtime_error("index outside speed grid");
    }

    return speed_grid_.Cell(index);
  }

private:
  ConstGrid<T, N> const speed_grid_;
};

} // namespace detail


//! Holds parameters related to the grid and provides methods for solving
//! the eikonal equation for a single grid cell at a time using information
//! about both distance and state of neighboring grid cells.
template<typename T, std::size_t N>
class UniformSpeedEikonalSolver :
  public detail::UniformSpeedEikonalSolverBase<T, N>
{
public:
  explicit UniformSpeedEikonalSolver(
    std::array<T, N> const& grid_spacing,
    T const speed = T{1})
    : detail::UniformSpeedEikonalSolverBase<T, N>(grid_spacing, speed)
  {}

  //! Returns the distance for grid cell at @a index given the current
  //! distances (@a distance_grid) and states (@a state_grid) of other cells.
  T Solve(
    std::array<std::int32_t, N> const& index,
    detail::Grid<T, N> const& distance_grid) const
  {
    return detail::SolveEikonal(
      index,
      distance_grid,
      speed(),
      grid_spacing());
  }
};


template <typename T, std::size_t N>
class HighAccuracyUniformSpeedEikonalSolver :
  public detail::UniformSpeedEikonalSolverBase<T, N>
{
public:
  explicit HighAccuracyUniformSpeedEikonalSolver(
    std::array<T, N> const& grid_spacing,
    T const speed = T{1})
    : detail::UniformSpeedEikonalSolverBase<T, N>(grid_spacing, speed)
  {}

  //! Returns the distance for grid cell at @a index given the current
  //! distances (@a distance_grid) and states (@a state_grid) of other cells.
  T Solve(
    std::array<std::int32_t, N> const& index,
    detail::Grid<T, N> const& distance_grid) const
  {
    return detail::HighAccuracySolveEikonal(
      index,
      distance_grid,
      speed(),
      grid_spacing());
  }
};


template <typename T, std::size_t N>
class VaryingSpeedEikonalSolver :
  public detail::VaryingSpeedEikonalSolverBase<T, N>
{
public:
  VaryingSpeedEikonalSolver(
    std::array<T, N> const& grid_spacing,
    std::array<std::size_t, N> const& speed_grid_size,
    std::vector<T> const& speed_buffer)
    : detail::VaryingSpeedEikonalSolverBase<T, N>(
        grid_spacing, speed_grid_size, speed_buffer)
  {}

  //! Returns the distance for grid cell at @a index given the current
  //! distances (@a distance_grid) and states (@a state_grid) of other cells.
  T Solve(
    std::array<std::int32_t, N> const& index,
    detail::Grid<T, N> const& distance_grid) const
  {
    return detail::SolveEikonal(
      index,
      distance_grid,
      Speed(index),
      grid_spacing());
  }
};


template <typename T, std::size_t N>
class HighAccuracyVaryingSpeedEikonalSolver :
  public detail::VaryingSpeedEikonalSolverBase<T, N>
{
public:
  HighAccuracyVaryingSpeedEikonalSolver(
    std::array<T, N> const& grid_spacing,
    std::array<std::size_t, N> const& speed_grid_size,
    std::vector<T> const& speed_buffer)
    : detail::VaryingSpeedEikonalSolverBase<T, N>(
        grid_spacing, speed_grid_size, speed_buffer)
  {}

  //! Returns the distance for grid cell at @a index given the current
  //! distances (@a distance_grid) and states (@a state_grid) of other cells.
  T Solve(
    std::array<std::int32_t, N> const& index,
    detail::Grid<T, N> const& distance_grid) const
  {
    return detail::HighAccuracySolveEikonal(
      index,
      distance_grid,
      Speed(index),
      grid_spacing());
  }
};


//! TODO - example usage!
template<typename T, std::size_t N, typename EikonalSolverType> inline
std::vector<T> UnsignedDistance(
  std::array<std::size_t, N> const& grid_size,
  std::vector<std::array<std::int32_t, N>> const& frozen_indices,
  std::vector<T> const& frozen_distances,
  EikonalSolverType const& eikonal_solver)
{
  using namespace std;
  using namespace detail;

  typedef T DistanceType;

  static_assert(N > 0, "number of dimensions must be > 0");
  static_assert(N == EikonalSolverType::kDimension,
                "mismatching eikonal solver dimension");

  auto distance_buffer = vector<DistanceType>(
    LinearSize(grid_size), numeric_limits<DistanceType>::max());
  auto distance_grid = Grid<DistanceType, N>(grid_size, distance_buffer);
  auto distance_predicate = [](auto const d) {
    return !isnan(d) && frozen(d) && d >= T{0};
  };

  assert(none_of(begin(distance_buffer), end(distance_buffer),
                 [](DistanceType const d) { return frozen(d); }));

  SetBoundaryCondition(
    frozen_indices,
    frozen_distances,
    DistanceType{1}, // Distance multiplier.
    distance_predicate,
    &distance_grid);

  auto narrow_band =
    InitialUnsignedNarrowBand(frozen_indices, distance_grid, eikonal_solver);

  MarchNarrowBand(
    eikonal_solver,
    narrow_band.get(),
    &distance_grid);

  assert(all_of(begin(distance_buffer), end(distance_buffer),
                [](DistanceType const d) { return frozen(d); }));

  return distance_buffer;
}


//! Compute the signed distance on a grid.
//!
//! Input:
//!   grid_size        - Number of grid cells in each dimension.
//!   dx               - Grid cell physical size in each dimension.
//!   speed            - Interface speed, when set to one gives
//!                      Euclidean distance. Must be positive.
//!   frozen_indices   - Integer coordinates of cells with given distances.
//!   frozen_distances - Signed distances assigned to frozen cells.
//!
//! Preconditions:
//!   - grid_size may not have a zero element.
//!   - dx must have all positive elements.
//!   - speed?
//!   - frozen_indices, frozen_distances and normals must have the same size.
//!   - frozen_indices must all be within size.
//!
//! TODO - example usage!
template<typename T, std::size_t N>
std::vector<T> SignedDistance(
  std::array<std::size_t, N> const& grid_size,
  std::array<T, N> const& dx,
  T const speed,
  std::vector<std::array<std::int32_t, N>> const& frozen_indices,
  std::vector<T> const& frozen_distances)
{
  using namespace std;
  using namespace detail;

  typedef T DistanceType;
  typedef EikonalSolver<DistanceType, N> EikonalSolverType;

  static_assert(is_floating_point<DistanceType>::value,
                "distance type must be floating point");
  static_assert(N > 1, "number of dimensions must be > 1");

  ThrowIfZeroElementInGridSize(grid_size);
  ThrowIfInvalidGridSpacing(dx);
  ThrowIfZeroOrNegativeOrNanSpeed(speed);
  ThrowIfSizeNotEqual(frozen_indices, frozen_distances);
  ThrowIfEmptyIndices(frozen_indices);
  ThrowIfIndexOutsideGrid(frozen_indices, grid_size);
  ThrowIfDuplicateIndices(frozen_indices, grid_size);
  ThrowIfWholeGridFrozen(frozen_indices, grid_size);
  ThrowIfInvalidDistance(
    frozen_distances,
    [](auto const d) { return !isnan(d); });

  auto distance_buffer = vector<DistanceType>(
    LinearSize(grid_size), numeric_limits<DistanceType>::max());
  auto distance_grid = Grid<DistanceType, N>(grid_size, distance_buffer.front());
  SetBoundaryCondition(
    frozen_indices,
    frozen_distances,
    DistanceType{-1}, // Multiplier
    &distance_grid);

  auto inside_narrow_band_indices = vector<array<int32_t, N>>();
  auto outside_narrow_band_indices = vector<array<int32_t, N>>();
  InitialSignedNarrowBands(
    frozen_indices,
    grid_size,
    &inside_narrow_band_indices,
    &outside_narrow_band_indices);

  auto const eikonal_solver = EikonalSolverType(dx, speed);
  auto narrow_band = NarrowBandStore<DistanceType, N>();
  InitializeNarrowBand(
    inside_narrow_band_indices,
    eikonal_solver,
    &distance_grid
    &narrow_band);

  MarchNarrowBand(
    eikonal_solver,
    &narrow_band,
    &distance_grid);

  // Negate all the inside distance values and flip the frozen values
  // back to their original values.
  for_each(
    begin(distance_buffer),
    end(distance_buffer),
    [](auto& d) {
      d = d < numeric_limits<DistanceType>::max() ? d * DistanceType{-1} : d;
    });

  InitializeNarrowBand(
    outside_narrow_band_indices,
    eikonal_solver,
    &distance_grid
    &narrow_band);

  MarchNarrowBand(
    eikonal_solver,
    &narrow_band,
    &distance_grid);

  return distance_buffer;
}

} // namespace fmm
} // namespace thinks

#endif // THINKS_FMM_FASTMARCHINGMETHOD_HPP_INCLUDED

