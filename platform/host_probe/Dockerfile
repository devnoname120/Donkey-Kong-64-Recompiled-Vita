FROM dk64-vita-build
RUN sed -i 's|http://|https://|g' /etc/apt/sources.list.d/ubuntu.sources \
    && apt-get -o Acquire::ForceIPv4=true update \
    && apt-get -o Acquire::ForceIPv4=true install -y --no-install-recommends \
    libegl1-mesa-dev libgles2-mesa-dev libgl1-mesa-dri
ENV EGL_PLATFORM=surfaceless
ENV LIBGL_ALWAYS_SOFTWARE=true
