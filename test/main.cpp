#if 0
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <thinks/testFastMarchingMethod.hpp>
#include <thinks/ppm.hpp>


namespace {

template<typename R, typename T> inline
R Clamp(T const low, T const high, T const value)
{
  using namespace std;

  return static_cast<R>(min<T>(high, max<T>(low, value)));
}

template<typename R, typename U, typename InIter> inline
std::vector<R> TransformedAsVector(
  InIter const begin, InIter const end, U const unary_op)
{
  using namespace std;

  auto r = vector<R>{};
  transform(begin, end, back_inserter(r), unary_op);
  return r;
}

template<typename T, typename InIter> inline
std::vector<T> SignedNormalized(InIter const in_begin, InIter const in_end)
{
  using namespace std;

  auto max_pos_value = numeric_limits<T>::lowest();
  auto min_neg_value = numeric_limits<T>::max();
  for (auto iter = in_begin; iter != in_end; ++iter) {
    auto const value = *iter;
    if (value == numeric_limits<T>::max()) {
      continue;
    }

    if (value > T(0)) {
      max_pos_value = max(max_pos_value, value);
    }

    if (value < T(0)) {
      min_neg_value = min(min_neg_value, value);
    }
  }

  auto const pos_factor = max_pos_value > T(0) ? T(1) / max_pos_value : T(-1);
  auto const neg_factor = min_neg_value < T(0) ? T(-1) / min_neg_value : T(-1);

  return TransformedAsVector<T>(
    in_begin,
    in_end,
    [=](auto const value) {
      if (value == numeric_limits<T>::max()) {
        return value;
      }

      if (value > T(0) && pos_factor > T(0)) {
        return pos_factor * value;
      }

      if (value < T(0) && neg_factor > T(0)) {
        return neg_factor * value;
      }

      return T(0);
    });
}


template<typename InIter, typename C> inline
std::vector<std::uint8_t> PixelsFromValues(
  InIter const in_begin,
  InIter const in_end,
  C const pixel_from_value)
{
  using namespace std;

  auto pixels = vector<uint8_t>();
  for (auto iter = in_begin; iter != in_end; ++iter) {
    auto const value = *iter;
    auto const pixel = pixel_from_value(value);
    pixels.insert(end(pixels), begin(pixel), end(pixel));
  }
  return pixels;
}

template<typename T>
void WriteGradMagImages(
  thinks::fmm::test::GradientMagnitudeStats<T, 2> const& grad_mag_stats,
  std::string const& prefix)
{
  using namespace std;
  using namespace thinks;

  // Negative values in shades of blue, positive values in shades of red.
  // Very large values as grey.
  auto const pixel_from_value = [](T const x) {
    if (x == numeric_limits<T>::max()) {
      return array<uint8_t, 3>{{128, 128, 128}};
    }
    return x < T(0) ?
      array<uint8_t, 3>{{
        uint8_t(0),
        uint8_t(0),
        Clamp<uint8_t>(
          T(0),
          T(numeric_limits<uint8_t>::max()),
          numeric_limits<uint8_t>::max() * fabs(x))}} :
      array<uint8_t, 3>{{
        Clamp<uint8_t>(
          T(0),
          T(numeric_limits<uint8_t>::max()),
          numeric_limits<uint8_t>::max() * x),
        uint8_t(0),
        uint8_t(0)}};
  };

  auto const width = grad_mag_stats.grid_size[0];
  auto const height = grad_mag_stats.grid_size[1];

  stringstream ss_input;
  ss_input << prefix << "_input_" << typeid(T).name() << ".ppm";
  auto const normalized_input = SignedNormalized<T>(
    begin(grad_mag_stats.input_buffer),
    end(grad_mag_stats.input_buffer));
  ppm::writeRgbImage(
    ss_input.str(),
    width,
    height,
    PixelsFromValues(
      begin(normalized_input),
      end(normalized_input),
      pixel_from_value));

  stringstream ss_distance;
  ss_distance << prefix << "_distance_" << typeid(T).name() << ".ppm";
  auto const normalized_distance = SignedNormalized<T>(
    begin(grad_mag_stats.distance_buffer),
    end(grad_mag_stats.distance_buffer));
  ppm::writeRgbImage(
    ss_distance.str(),
    width,
    height,
    PixelsFromValues(
      begin(normalized_distance),
      end(normalized_distance),
      pixel_from_value));

#if 0
  stringstream ss_grad_mag;
  ss_grad_mag << prefix << "_" << typeid(T).name() << ".ppm";
  auto const grad_mag = transformedAsVector<T>(
    begin(grad_mag_stats.grad_buffer),
    end(grad_mag_stats.grad_buffer),
    [](auto const v) { return sqrt(v[0] * v[0] + v[1] * v[1]); } ),
  ppm::writeRgbImage(
    ss_grad_mag.str(),
    width,
    height,
    pixelsFromValues(
      begin(grad_mag),
      end(grad_mag),
      [](T const& v) {
        auto const
        return array<uint8_t, 3>{{}};
          clamp<Pixel8::ChannelType>(
            T(0),
            T(numeric_limits<Pixel8::ChannelType>::max()),
            numeric_limits<Pixel8::ChannelType>::max() * v));
      }));
#endif

  stringstream ss_error;
  ss_error << prefix << "_error_" << typeid(T).name() << ".ppm";
  auto const normalized_error = SignedNormalized<T>(
    begin(grad_mag_stats.error_buffer),
    end(grad_mag_stats.error_buffer));
  ppm::writeRgbImage(
    ss_error.str(),
    width,
    height,
    PixelsFromValues(
      begin(normalized_error),
      end(normalized_error),
      pixel_from_value));
}

template<typename T>
void WriteDistStatImages(
  thinks::fmm::test::DistanceValueStats<T, 2> const& dist_stats,
  std::string const& prefix)
{
  using namespace std;
  using namespace thinks;

  // Negative values in shades of blue, positive values in shades of red.
  // Very large values as grey.
  auto const pixel_from_value = [](T const x) {
    if (x == numeric_limits<T>::max()) {
      return array<uint8_t, 3>{{128, 128, 128}};
    }
    return x < T(0) ?
      array<uint8_t, 3>{{
        uint8_t(0),
        uint8_t(0),
        Clamp<uint8_t>(
          T(0),
          T(numeric_limits<uint8_t>::max()),
          numeric_limits<uint8_t>::max() * fabs(x))}} :
      array<uint8_t, 3>{{
        Clamp<uint8_t>(
          T(0),
          T(numeric_limits<uint8_t>::max()),
          numeric_limits<uint8_t>::max() * x),
        uint8_t(0),
        uint8_t(0)}};
  };

  auto const width = dist_stats.grid_size[0];
  auto const height = dist_stats.grid_size[1];

  stringstream ss_input;
  ss_input << prefix << "_input_" << typeid(T).name() << ".ppm";
  auto const normalized_input = SignedNormalized<T>(
    begin(dist_stats.input_buffer),
    end(dist_stats.input_buffer));
  ppm::writeRgbImage(
    ss_input.str(),
    width,
    height,
    PixelsFromValues(
     begin(normalized_input),
     end(normalized_input),
     pixel_from_value));

  stringstream ss_distance;
  ss_distance << prefix << "_distance_" << typeid(T).name() << ".ppm";
  auto const normalized_distance = SignedNormalized<T>(
    begin(dist_stats.distance_buffer),
    end(dist_stats.distance_buffer));
  ppm::writeRgbImage(
    ss_distance.str(),
    width,
    height,
    PixelsFromValues(
      begin(normalized_distance),
      end(normalized_distance),
      pixel_from_value));

  stringstream ss_gt;
  ss_gt << prefix << "_gt_" << typeid(T).name() << ".ppm";
  auto const normalized_gt = SignedNormalized<T>(
    begin(dist_stats.distance_ground_truth_buffer),
    end(dist_stats.distance_ground_truth_buffer));
  ppm::writeRgbImage(
    ss_gt.str(),
    width,
    height,
    PixelsFromValues(
      begin(normalized_gt),
      end(normalized_gt),
      pixel_from_value));

  stringstream ss_error;
  ss_error << prefix << "_error_" << typeid(T).name() << ".ppm";
  auto const normalized_error = SignedNormalized<T>(
    begin(dist_stats.error_buffer),
    end(dist_stats.error_buffer));
  ppm::writeRgbImage(
    ss_error.str(),
    width,
    height,
    PixelsFromValues(
      begin(normalized_error),
      end(normalized_error),
      pixel_from_value));
}


template<std::size_t N>
void TestUnsignedDistanceN()
{
  using namespace std;
  using namespace thinks::fmm;

  auto const grad_mag_stats_f = test::UnsignedGradientMagnitudeStats<float, N>();
  cout << grad_mag_stats_f << endl;
  WriteGradMagImages<float>(grad_mag_stats_f, "unsigned_grad_mag");

  auto const grad_mag_stats_d = test::UnsignedGradientMagnitudeStats<double, N>();
  cout << grad_mag_stats_d << endl;
  WriteGradMagImages<double>(grad_mag_stats_d, "unsigned_grad_mag");

  auto const dist_stats_f = test::UnsignedDistanceValueStats<float, N>();
  cout << dist_stats_f << endl;
  WriteDistStatImages<float>(dist_stats_f, "unsigned_dist_stat");

  auto const dist_stats_d = test::UnsignedDistanceValueStats<double, N>();
  cout << dist_stats_d << endl;
  WriteDistStatImages<double>(dist_stats_d, "unsigned_dist_stat");
}


template<std::size_t N>
void TestSignedDistanceN()
{
  using namespace std;
  using namespace thinks::fmm;

  auto const grad_mag_stats_f = test::SignedGradientMagnitudeStats<float, N>();
  cout << grad_mag_stats_f << endl;
  WriteGradMagImages<float>(grad_mag_stats_f, "signed_grad_mag");

  auto const grad_mag_stats_d = test::SignedGradientMagnitudeStats<double, N>();
  cout << grad_mag_stats_d << endl;
  WriteGradMagImages<double>(grad_mag_stats_d, "signed_grad_mag");

  auto const dist_stats_f = test::SignedDistanceValueStats<float, N>();
  cout << dist_stats_f << endl;
  WriteDistStatImages<float>(dist_stats_f, "signed_dist_stat");

  auto const dist_stats_d = test::SignedDistanceValueStats<double, N>();
  cout << dist_stats_d << endl;
  WriteDistStatImages<double>(dist_stats_d, "signed_dist_stat");
}



} // namespace


namespace std {

template<typename T, size_t N>
ostream& operator<<(
  ostream& os,
  thinks::fmm::test::GradientMagnitudeStats<T, N> const& grad_mag_stats)
{
  os << "Gradient magnitude stats <" << typeid(T).name() << ", " << N << ">:" << endl
    << "min abs error: " << grad_mag_stats.min_abs_error << endl
    << "max abs error: " << grad_mag_stats.max_abs_error << endl
    << "avg abs error: " << grad_mag_stats.avg_abs_error << endl
    << "std_dev abs error: " << grad_mag_stats.std_dev_abs_error << endl
    << "duration: " << grad_mag_stats.duration_in_s << " [s]" << endl;
  return os;
}


template<typename T, size_t N>
ostream& operator<<(
  ostream& os,
  thinks::fmm::test::DistanceValueStats<T, N> const& dist_stats)
{
  os << "Distance value stats <" << typeid(T).name() << ", " << N << ">:" << endl
    << "min abs error: " << dist_stats.min_abs_error << endl
    << "max abs error: " << dist_stats.max_abs_error << endl
    << "avg abs error: " << dist_stats.avg_abs_error << endl
    << "std_dev abs error: " << dist_stats.std_dev_abs_error << endl
    << "duration: " << dist_stats.duration_in_s << " [s]" << endl;
  return os;
}

} // namespace std
#endif

#include <gtest/gtest.h>

int main(int argc, char* argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
