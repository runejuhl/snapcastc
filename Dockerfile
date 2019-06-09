FROM debian AS build

RUN apt-get update && \
  apt-get install -y  \
  alsa-utils          \
  build-essential     \
  cmake               \
  devscripts          \
  gcc-6               \
  git                 \
  libasound2-dev      \
  libjson-c-dev       \
  libopus-dev         \
  librubberband-dev   \
  libsoxr-dev

RUN git clone https://github.com/christf/snapcastc.git /snapcastc
WORKDIR /snapcastc

RUN cd /snapcastc && \
  mkdir build && \
  cd build && \
  cmake .. && \
  make -j5 && \
  make install
