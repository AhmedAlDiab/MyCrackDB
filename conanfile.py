from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout


class MyCrackDBRecipe(ConanFile):
    package_type = "application"
    settings = "os", "compiler", "build_type", "arch"

    default_options = {
        "rocksdb/*:with_zstd": True,
    }

    def layout(self):
        cmake_layout(self)

    def requirements(self):
        self.requires("rocksdb/10.5.1")
        self.requires("openssl/4.0.1")

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()