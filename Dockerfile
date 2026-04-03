FROM ubuntu:24.04 AS base

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    python3 \
    python3-pip \
    git \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 24.04 pip is externally managed; --break-system-packages is fine inside a container
RUN pip3 install conan --break-system-packages

# Generate a default Conan profile based on the detected compiler
RUN conan profile detect --force

WORKDIR /workspace

# ---- dependency cache layer ----
# Copy only the manifest first so Docker can cache the Conan install step
# and skip it on rebuilds when only source files change.
FROM base AS deps

COPY conanfile.txt .
RUN conan install . \
    --output-folder=build \
    --build=missing \
    -s build_type=Debug

# ---- full build ----
FROM deps AS builder

COPY . .
RUN cmake -B build \
        -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
        -DCMAKE_BUILD_TYPE=Debug \
        -G Ninja && \
    cmake --build build --parallel

# ---- test runner ----
FROM builder AS tester
WORKDIR /workspace/build
CMD ["ctest", "--output-on-failure", "--test-dir", "."]
