#pragma once
#include <mutex>

#include "gpu/state.h"

namespace d3d {

gpu::State& State();
std::recursive_mutex& Lock();
void FinishReports();  // reads the pushed commands, then waits for the reports asked for

}  // namespace d3d
