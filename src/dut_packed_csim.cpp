// Standalone C-simulation for dut_packed.cpp.
// Builds on the HLS host compiler (vitis_hls include path). Run on the lab
// host to verify:
//   (1) dut() consumes exactly the expected number of packed input words.
//   (2) dut() produces exactly the expected number of packed output words.
//   (3) The unpacked output matches the reference CPU pipeline byte-for-byte.
//
// Linked together with dut_packed.cpp + the CPU reference chain.
//
// Usage: ./dut_packed_csim <input.pgm> [low] [high]

#include "dut.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gaussian.hpp"
#include "image_io.hpp"
#include "nms.hpp"
#include "sobel.hpp"
#include "threshold.hpp"

namespace {

ImageU8 reference(const ImageU8& input, float low, float high) {
    const ImageF32 in_f32 = u8_to_f32(input);
    const ImageF32 blurred = gaussian_blur(in_f32, 5, 1.4f);
    const GradientData grad = compute_sobel_gradients(blurred);
    const ImageF32 suppressed = non_maximum_suppression(grad.magnitude, grad.direction);
    return double_threshold(suppressed, low, high);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            std::cerr << "Usage: " << argv[0] << " <input.pgm> [low] [high]\n";
            return 2;
        }
        const std::string path = argv[1];
        const float low = (argc > 2) ? std::stof(argv[2]) : 20.0f;
        const float high = (argc > 3) ? std::stof(argv[3]) : 60.0f;

        const ImageU8 input = read_pgm(path);
        const int W = input.width;
        const int H = input.height;
        const int words_per_row = (W + 3) / 4;
        const int pixel_words = words_per_row * H;

        std::cout << "Image: " << W << "x" << H
                  << " pixels=" << (W * H)
                  << " words_per_row=" << words_per_row
                  << " pixel_words=" << pixel_words << "\n";

        DutStream in_stream;
        DutStream out_stream;

        in_stream.write(static_cast<DutWord>(kDutProtocolVersion));
        in_stream.write(static_cast<DutWord>(W));
        in_stream.write(static_cast<DutWord>(H));
        in_stream.write(static_cast<DutWord>(static_cast<std::uint32_t>(std::lround(low))));
        in_stream.write(static_cast<DutWord>(static_cast<std::uint32_t>(std::lround(high))));

        for (int y = 0; y < H; ++y) {
            for (int w = 0; w < words_per_row; ++w) {
                const int base = w * 4;
                const std::uint32_t p0 = (base + 0 < W) ? input.pixels[y * W + base + 0] : 0u;
                const std::uint32_t p1 = (base + 1 < W) ? input.pixels[y * W + base + 1] : 0u;
                const std::uint32_t p2 = (base + 2 < W) ? input.pixels[y * W + base + 2] : 0u;
                const std::uint32_t p3 = (base + 3 < W) ? input.pixels[y * W + base + 3] : 0u;
                in_stream.write(static_cast<DutWord>(p0 | (p1 << 8) | (p2 << 16) | (p3 << 24)));
            }
        }

        const int expected_in_words = 5 + pixel_words;
        std::cout << "in_stream size before dut(): " << in_stream.size()
                  << " (expected " << expected_in_words << ")\n";

        dut(in_stream, out_stream);

        std::cout << "in_stream size after dut(): " << in_stream.size()
                  << " (want 0, i.e. DUT consumed all input)\n";
        std::cout << "out_stream size after dut(): " << out_stream.size()
                  << " (expected " << (3 + pixel_words) << ")\n";

        if (out_stream.size() != static_cast<std::size_t>(3 + pixel_words)) {
            std::cerr << "[FAIL] output word count mismatch\n";
            return 1;
        }

        const std::uint32_t version = static_cast<std::uint32_t>(out_stream.read());
        const int out_w = static_cast<int>(static_cast<std::uint32_t>(out_stream.read()));
        const int out_h = static_cast<int>(static_cast<std::uint32_t>(out_stream.read()));
        if (version != kDutProtocolVersion || out_w != W || out_h != H) {
            std::cerr << "[FAIL] bad header: version=" << version
                      << " w=" << out_w << " h=" << out_h << "\n";
            return 1;
        }

        ImageU8 actual(W, H);
        for (int y = 0; y < H; ++y) {
            for (int w = 0; w < words_per_row; ++w) {
                const std::uint32_t word = static_cast<std::uint32_t>(out_stream.read());
                const int base = w * 4;
                if (base + 0 < W) actual.pixels[y * W + base + 0] = static_cast<std::uint8_t>((word >>  0) & 0xffu);
                if (base + 1 < W) actual.pixels[y * W + base + 1] = static_cast<std::uint8_t>((word >>  8) & 0xffu);
                if (base + 2 < W) actual.pixels[y * W + base + 2] = static_cast<std::uint8_t>((word >> 16) & 0xffu);
                if (base + 3 < W) actual.pixels[y * W + base + 3] = static_cast<std::uint8_t>((word >> 24) & 0xffu);
            }
        }

        const ImageU8 expected = reference(input, low, high);
        if (expected.pixels == actual.pixels) {
            std::cout << "[PASS] packed DUT output matches reference byte-for-byte\n";
            return 0;
        }
        int mismatches = 0;
        int first = -1;
        for (int i = 0; i < W * H; ++i) {
            if (expected.pixels[i] != actual.pixels[i]) {
                if (first < 0) first = i;
                ++mismatches;
            }
        }
        std::cout << "[FAIL] " << mismatches << " mismatching pixels, first at idx " << first
                  << " (y=" << (first / W) << " x=" << (first % W)
                  << ") expected=" << static_cast<int>(expected.pixels[first])
                  << " actual=" << static_cast<int>(actual.pixels[first]) << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}
