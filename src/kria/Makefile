CXX ?= g++-13
CXXFLAGS ?= -std=c++17 -O3 -I. -I/usr/lib/vitis_hls -DHLS_NO_XIL_FPO_LIB

all: fpga fpga_packed sw

fpga: edge_detector_fpga

fpga_packed: edge_detector_fpga_packed

sw: edge_detector_arm

edge_detector_fpga: host.cpp image_io.cpp hysteresis.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

edge_detector_fpga_packed: host_packed.cpp image_io.cpp hysteresis.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

edge_detector_arm: main.cpp image_io.cpp gaussian.cpp sobel.cpp nms.cpp threshold.cpp hysteresis.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -f edge_detector_fpga edge_detector_fpga_packed edge_detector_arm
