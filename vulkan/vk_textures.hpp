#pragma once

#include "vk_types.hpp"
#include "vk_engine.hpp"

namespace vk_util {
    bool load_image_from_file(VulkanEngine& engine, const char* file, AllocatedImage& out_image);
}
