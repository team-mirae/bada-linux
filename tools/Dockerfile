# The build and test machine: Debian trixie with Fcitx5, IBus and their headers, Rust for the
# engine, and what the end-to-end tests drive them with (D-Bus, Python GI). tools/test.sh uses it.
FROM debian:trixie
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config ca-certificates git \
      cargo rustc \
      fcitx5 fcitx5-modules libfcitx5core-dev libfcitx5utils-dev libfcitx5config-dev fcitx5-modules-dev \
      ibus libibus-1.0-dev gir1.2-ibus-1.0 \
      libdbus-1-dev dbus dbus-x11 \
      python3 python3-gi python3-dbus \
      xvfb xauth \
    && rm -rf /var/lib/apt/lists/*
