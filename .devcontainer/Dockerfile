FROM ubuntu

SHELL ["/bin/bash", "-c"]

RUN apt update && \
apt install -y git curl build-essential make cmake xxd protobuf-compiler python3 python3-venv python3-pip

WORKDIR /root
RUN curl -L -o gcc-arm-none-eabi.tar.gz https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2 && \
tar xvf gcc-arm-none-eabi.tar.gz
ENV PATH=/root/gcc-arm-none-eabi-10.3-2021.10/bin:$PATH

RUN python3 -m venv /root/venv && \
source /root/venv/bin/activate && \
pip3 install protobuf grpcio-tools

RUN echo "source /root/venv/bin/activate" >> /root/.bashrc
ENV LD_LIBRARY_PATH=/workspaces/isere/build
