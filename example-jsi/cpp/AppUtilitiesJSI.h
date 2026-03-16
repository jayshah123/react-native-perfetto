#pragma once

#include <jsi/jsi.h>

#include <string>

namespace app_utilities {

void InstallAppUtilities(facebook::jsi::Runtime &runtime,
                         std::string cache_directory_path);

} // namespace app_utilities
