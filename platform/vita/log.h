#pragma once
#ifndef DK64_VITA_DIAGNOSTICS
#define DK64_VITA_DIAGNOSTICS 1
#endif
#if DK64_VITA_DIAGNOSTICS
void vita_log(const char *format, ...);
void vita_log_guest_profile();
#else
#define vita_log(...) ((void)0)
inline void vita_log_guest_profile() {}
#endif
