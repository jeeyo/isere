FROM ubuntu

SHELL ["/bin/bash", "-c"]

RUN apt update && \
apt install -y git curl build-essential make cmake xxd protobuf-compiler python3 python3-venv python3-pip libusb-1.0-0

WORKDIR /root
RUN curl -L -o gcc-arm-none-eabi.tar.gz https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2 && \
tar xvf gcc-arm-none-eabi.tar.gz
ENV PATH=/root/gcc-arm-none-eabi-10.3-2021.10/bin:$PATH

RUN curl -L -o xtensa-esp-elf.tar.gz https://github.com/espressif/crosstool-NG/releases/download/esp-14.2.0_20241119/xtensa-esp-elf-14.2.0_20241119-x86_64-linux-gnu.tar.xz && \
tar xvf xtensa-esp-elf.tar.gz
ENV PATH=/root/xtensa-esp-elf/bin:$PATH

RUN python3 -m venv /root/venv && \
source /root/venv/bin/activate && \
pip3 install protobuf grpcio-tools

ENV IDF_PYTHON_ENV_PATH=/root/venv
ENV IDF_PYTHON_CHECK_CONSTRAINTS=no

RUN echo "source /root/venv/bin/activate" >> /root/.bashrc
ENV LD_LIBRARY_PATH=/workspaces/isere/build
