# Builds the Zephyr/Rust spike for native_sim (a Linux executable), for the
# platform evaluation in docs/PLATFORM_DECISION.md. native_sim uses the host's
# gcc, so no Zephyr SDK is needed.
#
#   docker build -t isere-zephyr -f spikes/zephyr-rust/Dockerfile spikes/zephyr-rust
#   docker run --rm -v ~/zephyrproject:/workspace -v "$PWD":/workspace/isere isere-zephyr \
#     west build -p always -b native_sim/native/64 -d /workspace/isere/build-zephyr /workspace/isere/spikes/zephyr-rust
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
  && apt-get install -y --no-install-recommends \
     build-essential cmake ninja-build device-tree-compiler git curl ca-certificates file \
     python3 python3-pip python3-venv python3-dev clang libclang-dev xz-utils \
  && rm -rf /var/lib/apt/lists/*

# west and what Zephyr's build scripts import
RUN python3 -m venv /opt/venv \
  && /opt/venv/bin/pip install --no-cache-dir west pyelftools pyyaml packaging jsonschema pykwalify canopen \
     intelhex natsort ply anytree pyserial psutil "jinja2>=3" junitparser
ENV PATH=/opt/venv/bin:/root/.cargo/bin:$PATH

# Rust, as zephyr-lang-rust expects it
RUN curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain stable \
  && /root/.cargo/bin/rustup target add aarch64-unknown-none x86_64-unknown-none \
  && /root/.cargo/bin/rustup component add clippy

ENV ZEPHYR_BASE=/workspace/zephyr
ENV ZEPHYR_TOOLCHAIN_VARIANT=host
RUN git config --global --add safe.directory '*'
WORKDIR /workspace
