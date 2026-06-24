from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps

class MyCrackDBRecipe(ConanFile):
    package_type = "application"
    settings = "os", "compiler", "build_type", "arch"

    def configure(self):
        self.options["rocksdb"].with_zstd = True

    def requirements(self):
        self.requires("rocksdb/10.5.1")
        self.requires("openssl/4.0.1")
        self.requires("zstd/1.5.7")

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()