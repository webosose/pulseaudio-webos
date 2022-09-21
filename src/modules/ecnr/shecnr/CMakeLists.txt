# Builds the tensorflow lite ecnr library.

# for webos
# ARMCC_PREFIX=${HOME}/toolchains/gcc-arm-8.3-2019.03-x86_64-aarch64-linux-gnu/bin/aarch64-linux-gnu-
# ARMCC_FLAGS="-funsafe-math-optimizations"
# cmake -DCMAKE_C_COMPILER=${ARMCC_PREFIX}gcc   -DCMAKE_CXX_COMPILER=${ARMCC_PREFIX}g++   -DCMAKE_C_FLAGS="${ARMCC_FLAGS}"   -DCMAKE_CXX_FLAGS="${ARMCC_FLAGS}"   -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON   -DCMAKE_SYSTEM_NAME=Linux   -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DTFLITE_ENABLE_XNNPACK=ON ../tensorflow/lite
# cmake --build . -j -t module_ec_nr_source

# for android ndk
# cmake -DCMAKE_TOOLCHAIN_FILE=${HOME}/Android/Sdk/ndk/25.1.8937393/build/cmake/android.toolchain.cmake -DANDROID_ABI=armeabi-v7a -DTFLITE_ENABLE_XNNPACK=ON ../tensorflow/lite
# cmake --build . -j -t module_ec_nr_source

list(APPEND TFLITE_ECNR_SRCS
  module_ecnr.cc
  module_ecnr_c.cc
  kiss_fft.c
)


add_library(module_ec_nr_source1 SHARED
  ${TFLITE_ECNR_SRCS}
)

target_link_libraries(module_ec_nr_source1
  tensorflow-lite
)

#install(TARGETS module_ec_nr_source1 DESTINATION lib)
