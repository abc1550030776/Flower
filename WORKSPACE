local_repository(
  name = "com_google_tcmalloc",
  path = "/root/Source/tcmalloc",
)

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "com_google_absl",
    urls = ["https://github.com/abseil/abseil-cpp/archive/e4e2e57e1a4308cf4ba008d9c1f2d478b3349201.zip"],
    strip_prefix = "abseil-cpp-e4e2e57e1a4308cf4ba008d9c1f2d478b3349201",
    sha256 = "22f571ae4e4d61a5f19f0c8e87dec40cc666c2be0e6bd62944d572cbe8fb41aa",
)
