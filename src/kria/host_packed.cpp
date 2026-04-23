// Packed host variant for Experiment 1 (Milestone 5).
//
// Same CLI and behavior as host.cpp, but packs 4 uint8 pixels per uint32 word
// on both the send and receive paths. This matches the protocol implemented
// in src/dut_packed.cpp. Compared to the current batched baseline:
//   - TX payload size: ceil(W*H/4) words instead of W*H words
//   - RX payload size: ceil(W*H/4) words instead of W*H words
//   - Header / response header are unchanged
//
// Timing output is kept identical to host.cpp so results can be compared
// directly with the Milestone 3 table.

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include "dut.hpp"
#include "hysteresis.hpp"
#include "image_io.hpp"

namespace {

struct Options {
    std::string input_path;
    std::string output_path;
    std::uint32_t low_threshold = 20;
    std::uint32_t high_threshold = 60;
};

void usage() {
    std::cout << "Usage: edge_detector_fpga_packed <input.pgm> <output.pgm> [--low value] [--high value]\n";
}

Options parse_args(int argc, char** argv) {
    if (argc < 3) {
        usage();
        throw std::runtime_error("Missing required arguments.");
    }

    Options options;
    options.input_path = argv[1];
    options.output_path = argv[2];
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--low" && i + 1 < argc) {
            options.low_threshold = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--high" && i + 1 < argc) {
            options.high_threshold = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else {
            usage();
            throw std::runtime_error("Unknown or incomplete argument: " + arg);
        }
    }
    return options;
}

void write_all(int fdw, const void* data, std::size_t bytes) {
    const char* p = static_cast<const char*>(data);
    std::size_t remaining = bytes;
    while (remaining > 0) {
        const ssize_t n = write(fdw, p, remaining);
        if (n <= 0) {
            throw std::runtime_error("Failed to write to CiFra OpenBus: " + std::string(std::strerror(errno)));
        }
        p += n;
        remaining -= static_cast<std::size_t>(n);
    }
}

void read_all(int fdr, void* data, std::size_t bytes) {
    char* p = static_cast<char*>(data);
    std::size_t remaining = bytes;
    while (remaining > 0) {
        const ssize_t n = read(fdr, p, remaining);
        if (n <= 0) {
            throw std::runtime_error("Failed to read from CiFra OpenBus: " + std::string(std::strerror(errno)));
        }
        p += n;
        remaining -= static_cast<std::size_t>(n);
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        using Clock = std::chrono::high_resolution_clock;
        const auto us = [](auto d) {
            return std::chrono::duration_cast<std::chrono::microseconds>(d).count();
        };

        const auto t_start = Clock::now();

        const Options options = parse_args(argc, argv);

        const auto t_args = Clock::now();

        const ImageU8 input = read_pgm(options.input_path);
        if (input.width > kMaxImageWidth || input.height > kMaxImageHeight) {
            throw std::runtime_error("Input image exceeds FPGA maximum supported size.");
        }

        const auto t_read_pgm = Clock::now();

        const std::size_t pixel_count = input.pixels.size();
        const std::size_t words_per_row = (static_cast<std::size_t>(input.width) + 3) / 4;
        const std::size_t pixel_words = words_per_row * static_cast<std::size_t>(input.height);

        std::vector<std::uint32_t> tx_buf;
        tx_buf.reserve(5 + pixel_words);
        tx_buf.push_back(kDutProtocolVersion);
        tx_buf.push_back(static_cast<std::uint32_t>(input.width));
        tx_buf.push_back(static_cast<std::uint32_t>(input.height));
        tx_buf.push_back(options.low_threshold);
        tx_buf.push_back(options.high_threshold);

        for (int y = 0; y < input.height; ++y) {
            const std::size_t row_base = static_cast<std::size_t>(y) * static_cast<std::size_t>(input.width);
            for (std::size_t w = 0; w < words_per_row; ++w) {
                const std::size_t base = w * 4;
                const std::uint32_t p0 = (base + 0 < static_cast<std::size_t>(input.width)) ? static_cast<std::uint32_t>(input.pixels[row_base + base + 0]) : 0u;
                const std::uint32_t p1 = (base + 1 < static_cast<std::size_t>(input.width)) ? static_cast<std::uint32_t>(input.pixels[row_base + base + 1]) : 0u;
                const std::uint32_t p2 = (base + 2 < static_cast<std::size_t>(input.width)) ? static_cast<std::uint32_t>(input.pixels[row_base + base + 2]) : 0u;
                const std::uint32_t p3 = (base + 3 < static_cast<std::size_t>(input.width)) ? static_cast<std::uint32_t>(input.pixels[row_base + base + 3]) : 0u;
                tx_buf.push_back(p0 | (p1 << 8) | (p2 << 16) | (p3 << 24));
            }
        }

        std::vector<std::uint32_t> rx_buf(3 + pixel_words);

        const auto t_pack = Clock::now();

        const int fdr = open("/dev/cifra_openbus_read_32", O_RDONLY);
        const int fdw = open("/dev/cifra_openbus_write_32", O_WRONLY);
        if (fdr < 0 || fdw < 0) {
            throw std::runtime_error("Failed to open /dev/cifra_openbus_* device files.");
        }

        const auto t_open_dev = Clock::now();

        write_all(fdw, tx_buf.data(), tx_buf.size() * sizeof(std::uint32_t));

        const auto t_fpga_write = Clock::now();

        read_all(fdr, rx_buf.data(), rx_buf.size() * sizeof(std::uint32_t));

        const auto t_fpga_read = Clock::now();

        const std::uint32_t version = rx_buf[0];
        const int width = static_cast<int>(rx_buf[1]);
        const int height = static_cast<int>(rx_buf[2]);
        if (version != kDutProtocolVersion || width != input.width || height != input.height) {
            throw std::runtime_error("FPGA returned an invalid response header.");
        }

        ImageU8 thresholded(width, height);
        for (int y = 0; y < height; ++y) {
            const std::size_t row_base = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
            for (std::size_t w = 0; w < words_per_row; ++w) {
                const std::uint32_t word = rx_buf[3 + static_cast<std::size_t>(y) * words_per_row + w];
                const std::size_t base = w * 4;
                if (base + 0 < static_cast<std::size_t>(width)) thresholded.pixels[row_base + base + 0] = static_cast<std::uint8_t>((word >>  0) & 0xffu);
                if (base + 1 < static_cast<std::size_t>(width)) thresholded.pixels[row_base + base + 1] = static_cast<std::uint8_t>((word >>  8) & 0xffu);
                if (base + 2 < static_cast<std::size_t>(width)) thresholded.pixels[row_base + base + 2] = static_cast<std::uint8_t>((word >> 16) & 0xffu);
                if (base + 3 < static_cast<std::size_t>(width)) thresholded.pixels[row_base + base + 3] = static_cast<std::uint8_t>((word >> 24) & 0xffu);
            }
        }
        (void)pixel_count;

        const ImageU8 edges = hysteresis(thresholded);

        const auto t_hyst = Clock::now();

        write_pgm(options.output_path, edges);

        close(fdr);
        close(fdw);

        const auto t_end = Clock::now();

        std::cout << "FPGA edge detection complete (packed). Output: " << options.output_path << "\n";
        std::cout << "Image: " << input.width << "x" << input.height
                  << " (" << input.pixels.size() << " pixels, "
                  << pixel_words << " pixel-words)\n";
        std::cout << "--- Pipeline timing (us) ---\n";
        std::cout << "  Arg parsing:      " << us(t_args      - t_start)     << "\n";
        std::cout << "  Read PGM input:   " << us(t_read_pgm  - t_args)      << "\n";
        std::cout << "  Pack TX buffer:   " << us(t_pack      - t_read_pgm)  << "\n";
        std::cout << "  Open /dev nodes:  " << us(t_open_dev  - t_pack)      << "\n";
        std::cout << "  FPGA write (in):  " << us(t_fpga_write - t_open_dev) << " (1 syscall, "
                  << (tx_buf.size() * sizeof(std::uint32_t)) << " bytes)\n";
        std::cout << "  FPGA read (out):  " << us(t_fpga_read - t_fpga_write)<< " (1 syscall, "
                  << (rx_buf.size() * sizeof(std::uint32_t)) << " bytes)\n";
        std::cout << "  FPGA round-trip:  " << us(t_fpga_read - t_open_dev)  << " (write + compute + read)\n";
        std::cout << "  Hysteresis (CPU): " << us(t_hyst      - t_fpga_read) << "\n";
        std::cout << "  Write PGM output: " << us(t_end       - t_hyst)      << "\n";
        std::cout << "  --------------------------\n";
        std::cout << "  Total elapsed:    " << us(t_end       - t_start)     << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
}
