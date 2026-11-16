# Ubuntu 26.04 LTS base pinned by digest; update deliberately.
FROM ubuntu:26.04@sha256:6a06111c3b0a8600feadb270aec771dc239bf407b25625f93f44ab00e11f4d3c AS rocm-runtime
# Pin the tested ROCm release; HIPRTC and its compiler libraries ship with the runtime.
# AMD publishes these user-space packages for Noble; no host driver is installed.
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl gnupg \
    && mkdir -p /etc/apt/keyrings \
    && curl -fsSL https://repo.radeon.com/rocm/rocm.gpg.key -o /tmp/rocm.key \
    && gpg --batch --dearmor -o /etc/apt/keyrings/rocm.gpg /tmp/rocm.key \
    && printf '%s\n' 'deb [arch=amd64 signed-by=/etc/apt/keyrings/rocm.gpg] https://repo.radeon.com/rocm/apt/7.2 noble main' > /etc/apt/sources.list.d/rocm.list \
    && printf 'Package: *\nPin: origin repo.radeon.com\nPin-Priority: 600\n' > /etc/apt/preferences.d/rocm \
    && apt-get update && apt-get install -y --no-install-recommends \
       hip-runtime-amd=7.2.26015.70200-43~24.04 \
       libssl3t64 libstdc++6 libvulkan1 mesa-vulkan-drivers ocl-icd-libopencl1 \
    && rm -f /tmp/rocm.key && rm -rf /var/lib/apt/lists/*

FROM rocm-runtime AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build libssl-dev python3 ca-certificates \
    libvulkan1 mesa-vulkan-drivers vulkan-tools hip-dev=7.2.26015.70200-43~24.04 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DWIFI_GPU_ENABLE_HIP=ON -DWIFI_GPU_TEST_SOFTWARE_VULKAN=ON && cmake --build /build --parallel 2

FROM build AS test
ENV XDG_RUNTIME_DIR=/tmp
ENV LD_LIBRARY_PATH=/opt/rocm/lib
RUN ctest --test-dir /build --output-on-failure -LE gpu
RUN cmake --install /build --prefix /out

FROM rocm-runtime AS runtime
ENV LD_LIBRARY_PATH=/opt/rocm/lib
RUN useradd --uid 10001 --create-home worker
COPY --from=test /out/ /usr/local/
USER 10001:10001
WORKDIR /home/worker
ENTRYPOINT ["/usr/local/bin/wifi-gpu"]
CMD ["--help"]
