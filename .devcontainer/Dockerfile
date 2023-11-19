FROM ubuntu:20.04 as builder

RUN groupadd -g 1000 docker && useradd docker -u 1000 -g 1000 -s /bin/bash -d /build
RUN mkdir /build && chown docker:docker /build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt update && apt-get install -y \
  make unrar-free autoconf automake libtool gcc g++ gperf \
  flex bison texinfo gawk ncurses-dev libexpat-dev python3-dev python3 python3-serial \
  sed git unzip bash help2man wget bzip2 libtool-bin

# Manually download some dependent packages as builtin URLs are not working
RUN su docker -c " \
    wget -P /build https://libisl.sourceforge.io/isl-0.14.tar.bz2 ; \
    wget -P /build https://github.com/libexpat/libexpat/releases/download/R_2_1_0/expat-2.1.0.tar.gz ; \
"
# Checkout main source code
RUN su docker -c " \
    git clone --recursive https://github.com/pfalcon/esp-open-sdk.git /build/esp-open-sdk ; \
"
COPY crosstool-NG-configure.patch /build
# Patch source code to make it work on newer Linux version
RUN su docker -c " \
    cd /build/esp-open-sdk ; \
    patch -p1 < /build/crosstool-NG-configure.patch ; \
    echo CT_LOCAL_TARBALLS_DIR=/build >> crosstool-config-overrides ; \
    echo CT_DEBUG_gdb=n >> crosstool-config-overrides ; \
    cd esptool ; \
    git checkout v1.3 ; \
"

# Build code
RUN su docker -c "cd /build/esp-open-sdk && make STANDALONE=n"


FROM ubuntu:20.04

RUN DEBIAN_FRONTEND=noninteractive apt update && \
    DEBIAN_FRONTEND=noninteractive apt install -y make python3 python3-serial

COPY --from=builder /build/esp-open-sdk/xtensa-lx106-elf /opt/xtensa-lx106-elf
ENV PATH /opt/xtensa-lx106-elf/bin:$PATH
