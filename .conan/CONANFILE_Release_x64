from conan import ConanFile
from conan.tools.microsoft import vs_layout, MSBuildDeps

class ConanApplication(ConanFile):
    package_type = "application"
    settings = "os", "compiler", "build_type", "arch"

    # ADD THIS: Force the ZSTD option on RocksDB
    def configure(self):
        self.options["rocksdb"].with_zstd = True

    def layout(self):
        vs_layout(self)

    def generate(self):
        deps = MSBuildDeps(self)
        deps.generate()

    def requirements(self):
        # Your existing logic
        requirements = self.conan_data.get('requirements', [])
        for requirement in requirements:
            self.requires(requirement)