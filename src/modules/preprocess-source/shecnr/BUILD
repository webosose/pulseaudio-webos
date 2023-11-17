# Description:
#   TensorFlow Lite minimal example.

load("//tensorflow/lite:build_def.bzl", "tflite_linkopts")

package(
    default_visibility = ["//visibility:public"],
    licenses = ["notice"],
)

cc_binary(
    name = "module_ec_nr_source",
    srcs = [
        "module_ecnr.h",
        "kiss_fft.c",
        "kiss_fft.h",
        "_kiss_fft_guts.h",
        "kiss_fft_log.h",
        "module_ecnr.cc",
        "module_ecnr_c.h",
        "module_ecnr_c.cc",
    ],
    linkopts = tflite_linkopts() + select({
        "//tensorflow:android": [
            "-pie",  # Android 5.0 and later supports only PIE
            "-lm",  # some builtin ops, e.g., tanh, need -lm
        ],
        "//conditions:default": [],
    }),
    deps = [
        "//tensorflow/lite:framework",
        "//tensorflow/lite/kernels:builtin_ops",
    ],
    linkshared=True,
)
