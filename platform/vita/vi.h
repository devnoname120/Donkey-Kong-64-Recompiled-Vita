#pragma once
#include "hle/rt64_vi.h"
#include "ultramodern/renderer_context.hpp"

inline RT64::VI vita_video_interface(const ultramodern::renderer::ViRegs &regs) {
    RT64::VI vi{};
    vi.status.word=regs.VI_STATUS_REG;
    vi.origin=regs.VI_ORIGIN_REG & 0xffffff;
    vi.width=regs.VI_WIDTH_REG;
    vi.intr=regs.VI_INTR_REG;
    vi.vCurrentLine=regs.VI_V_CURRENT_LINE_REG;
    vi.burst.word=regs.VI_TIMING_REG;
    vi.vSync=regs.VI_V_SYNC_REG;
    vi.hSync.word=regs.VI_H_SYNC_REG;
    vi.leap.word=regs.VI_LEAP_REG;
    vi.hRegion.word=regs.VI_H_START_REG;
    vi.vRegion.word=regs.VI_V_START_REG;
    vi.vBurst.word=regs.VI_V_BURST_REG;
    vi.xTransform.word=regs.VI_X_SCALE_REG;
    vi.yTransform.word=regs.VI_Y_SCALE_REG;
    return vi;
}
