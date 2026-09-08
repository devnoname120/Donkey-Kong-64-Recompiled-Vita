#pragma once
#include "fast/rt64_fast.h"
#include <cstdint>
#include <memory>

namespace ArchitectureProbe {
std::unique_ptr<RT64::FastDrawSink> createSink();
void beginTask(const uint8_t *rdram);
void endTask();
void observeVertex(uint32_t address,const uint8_t *bytes,uint32_t size,uint32_t flags);
void observeTexture(uint32_t address,const uint8_t *bytes,uint32_t size);
void observeOperation(unsigned index);
void observeCommand(uint32_t address,const uint8_t *rdram,size_t size);
unsigned residentGeometry(const std::vector<RT64::FastVertex> &vertices);
void configureViewport();
bool simpleFragment();
unsigned specializationId(const RT64::FastDraw &draw);
const std::string &specializedSource(const RT64::FastDraw &draw);
}
