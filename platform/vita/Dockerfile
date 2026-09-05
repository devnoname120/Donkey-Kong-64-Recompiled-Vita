FROM --platform=linux/amd64 vitasdk/vitasdk@sha256:07031c0abbad8b89503b7d69ddcafddf69cc3b0744c4576c1d5536efac26d574

# Verified upstream HEADs on 2026-09-05. Build vitaShaRK first because vitaGL
# uses shader-association APIs newer than the SDK's bundled vitaShaRK.
ARG VITASHARK_REV=df24065e65098b2d1ac533760109ad4367573f28
ARG VITAGL_REV=cd3791e29ff7f1c0ab349f12c7231f4871ce6a75
ARG VITAGL_SHARK_LOG=1
ARG VITAGL_LOG_ERRORS=2
RUN git clone https://github.com/Rinnegatamante/vitashark.git /tmp/vitashark \
    && git -C /tmp/vitashark checkout ${VITASHARK_REV} \
    && make -C /tmp/vitashark -j8 install
RUN git clone https://github.com/Rinnegatamante/vitaGL.git /tmp/vitagl \
    && git -C /tmp/vitagl checkout ${VITAGL_REV} \
    && make -C /tmp/vitagl -j8 NO_SPLASHSCREEN=1 HAVE_PTHREAD=1 HAVE_GLSL_TEXTURE_SIZE=1 HAVE_SHARK_LOG=${VITAGL_SHARK_LOG} LOG_ERRORS=${VITAGL_LOG_ERRORS} install
WORKDIR /project
