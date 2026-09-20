#include "graphx/vita/processing.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > 8836) return 0;
  try {
    auto wire = std::span{reinterpret_cast<const std::byte*>(data), size};
    auto spectrum = graphx::vita::decode_spectrum(wire);
    (void)graphx::vita::detect(spectrum);
    auto encoded = graphx::vita::encode_spectrum(spectrum);
    if (!std::equal(encoded.begin(), encoded.end(), wire.begin(), wire.end())) std::abort();
  } catch (const std::invalid_argument&) {
  }
  return 0;
}
