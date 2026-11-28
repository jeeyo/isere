# Linux validation environment for tools/linux.sh (build, node:test suite, valgrind, wrk).
#
#   docker build -t isere-linux -f tools/linux.Dockerfile tools
#   docker run --rm -v "$PWD":/app -w /app isere-linux tools/linux.sh
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
  && apt-get install -y --no-install-recommends \
     build-essential cmake git xxd curl ca-certificates valgrind wrk clang libclang-rt-18-dev python3 openssl \
  && curl -fsSL https://deb.nodesource.com/setup_22.x | bash - \
  && apt-get install -y --no-install-recommends nodejs \
  && rm -rf /var/lib/apt/lists/*

RUN git config --global --add safe.directory '*'
