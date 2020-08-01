#include "window.hpp"
#include "renderer.hpp"
#include "shared.hpp"
#include "vertex.hpp"
#include "BUILD_OPTIONS.hpp"
 
#include <assert.h>
#include <array>
#include <cstring>

const std::vector<Vertex> vertices = { 
    {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}}, 
    {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}}, 
    {{-0.5, 0.5f}, {0.0f, 0.0f, 1.0f}} 
};

Window::Window(Renderer* render, uint32_t size_x, uint32_t size_y, std::string name) {
    renderer = render;
    surface_size_x = size_x;
    surface_size_y = size_y;
    window_name = name;

    initOSWindow();
    initSurface();
    initSwapchain();
    initSwapchainImages();
    initRenderPass();
    initGraphicsPipeline();
    initFramebuffers();
    initCommandPool();
    initVertexBuffer();
    initCommandBuffers();
    initSynchronizations();
}

Window::~Window() {
    vkQueueWaitIdle(renderer->queue);
    destroySynchronizations();
    destroyCommandBuffers();
    destroyVertexBuffer();
    destroyCommandPool();
    destroyFramebuffers();
    destroyGraphicsPipeline();
    destroyRenderPass();
    destroySwapchainImages();
    destroySwapchain();
    destroySurface();
    destroyOSWindow();
}

void Window::recreateSwapchain() {
    vkDeviceWaitIdle(renderer->device);

    destroyCommandBuffers();
    destroyFramebuffers();
    destroyGraphicsPipeline();
    destroyRenderPass();
    destroySwapchainImages();
    destroySwapchain();

    initSwapchain();
    initSwapchainImages();
    initRenderPass();
    initGraphicsPipeline();
    initFramebuffers();
    initCommandBuffers();
}

void Window::close() {
    window_should_run = false;
}

bool Window::update() {
    updateOSWindow();
    return window_should_run;
}

void Window::beginRender() {
    VkResult result;
    errorCheck(result = vkAcquireNextImageKHR(renderer->device, swapchain, UINT64_MAX, VK_NULL_HANDLE, swapchain_image_available, &active_swapchain_image_id));
    if (result == VK_ERROR_OUT_OF_DATE_KHR) recreateSwapchain();
    errorCheck(vkWaitForFences(renderer->device, 1, &swapchain_image_available, VK_TRUE, UINT64_MAX));
    errorCheck(vkResetFences(renderer->device, 1, &swapchain_image_available));
    errorCheck(vkQueueWaitIdle(renderer->queue));

}

void Window::endRender(std::vector<VkSemaphore> wait_semaphores) {
    VkResult present_result = VkResult::VK_RESULT_MAX_ENUM;
    VkPresentInfoKHR present_info {};
    present_info.waitSemaphoreCount = wait_semaphores.size();
    present_info.pWaitSemaphores = wait_semaphores.data();
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain;
    present_info.pImageIndices = &active_swapchain_image_id;
    present_info.pResults = &present_result;

    errorCheck(vkQueuePresentKHR(renderer->queue, &present_info));
    errorCheck(present_result);
}

void Window::initSurface() {
    initOSSurface();
    auto gpu = renderer->gpu;
    VkBool32 WSI_supported = false;
    errorCheck(vkGetPhysicalDeviceSurfaceSupportKHR(gpu, renderer->graphics_family_index, surface, &WSI_supported));
    if (!WSI_supported) {
        assert(0 && "WSI not supported");
        std::exit(-1);
    }

    errorCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &surface_capabilities));
    if (surface_capabilities.currentExtent.width < UINT32_MAX) {
        surface_size_x = surface_capabilities.currentExtent.width;
        surface_size_y = surface_capabilities.currentExtent.height;
    }

    uint32_t format_count = 0;
    errorCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, nullptr));
    if (format_count == 0) {
        assert(0 && "Surface formats missing");
        std::exit(-1);
    }
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    errorCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, formats.data()));
    if (formats[0].format == VK_FORMAT_UNDEFINED) {
        surface_format.format = VK_FORMAT_B8G8R8A8_UNORM;
        surface_format.colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    } else {
        surface_format = formats[0];
    }
}

void Window::destroySurface() {
    vkDestroySurfaceKHR(renderer->instance, surface, nullptr);
}

void Window::initOSWindow() {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfw_window = glfwCreateWindow(surface_size_x, surface_size_y, window_name.c_str(), nullptr, nullptr);
    if (!glfw_window) {
        glfwTerminate();
        assert(0 && "GLFW could not create window.");
        return;
    }
    glfwGetFramebufferSize(glfw_window, (int*)&surface_size_x, (int*)&surface_size_y);
}

void Window::destroyOSWindow() {
    glfwDestroyWindow(glfw_window);
}

void Window::updateOSWindow() {
    glfwPollEvents();
    if (glfwWindowShouldClose(glfw_window)) close();
}

void Window::initOSSurface() {
    if (VK_SUCCESS != glfwCreateWindowSurface(renderer->instance, glfw_window, nullptr, &surface)) {
        glfwTerminate();
        assert(0 && "GLFW could not create window surface.");
        return;
    }
}

void Window::initSwapchain() {
    if (swapchain_image_count < surface_capabilities.minImageCount + 1) {
        swapchain_image_count = surface_capabilities.minImageCount + 1;
    }
    if (surface_capabilities.maxImageCount > 0) {
        if (swapchain_image_count > surface_capabilities.maxImageCount) {
            swapchain_image_count = surface_capabilities.maxImageCount;
        }
    }

    if (surface_capabilities.currentExtent.width != UINT32_MAX) {
        swapchain_extent = surface_capabilities.currentExtent;
    } else {
        int width, height;
        glfwGetFramebufferSize(glfw_window, &width, &height);
        swapchain_extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
        swapchain_extent.width = std::max(surface_capabilities.minImageExtent.width, std::min(surface_capabilities.maxImageExtent.width, swapchain_extent.width));
        swapchain_extent.height = std::max(surface_capabilities.minImageExtent.height, std::min(surface_capabilities.maxImageExtent.height, swapchain_extent.height));
    }

    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    uint32_t present_mode_count = 0;
    errorCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(renderer->gpu, surface, &present_mode_count, nullptr));
    std::vector<VkPresentModeKHR> present_mode_list(present_mode_count);
    errorCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(renderer->gpu, surface, &present_mode_count, present_mode_list.data()));
    for (auto m : present_mode_list) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) present_mode = m;
    }

    VkSwapchainCreateInfoKHR swapchain_create_info {};
	swapchain_create_info.sType	= VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchain_create_info.surface = surface;
	swapchain_create_info.minImageCount = swapchain_image_count;
	swapchain_create_info.imageFormat = surface_format.format;
	swapchain_create_info.imageColorSpace = surface_format.colorSpace;
	swapchain_create_info.imageExtent.width	= surface_size_x;
	swapchain_create_info.imageExtent.height = surface_size_y;
	swapchain_create_info.imageArrayLayers = 1;
	swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchain_create_info.queueFamilyIndexCount = 0;
	swapchain_create_info.pQueueFamilyIndices = nullptr;
	swapchain_create_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchain_create_info.presentMode = present_mode;
	swapchain_create_info.clipped = VK_TRUE;
	swapchain_create_info.oldSwapchain = VK_NULL_HANDLE;

	errorCheck(vkCreateSwapchainKHR(renderer->device, &swapchain_create_info, nullptr, &swapchain));
	errorCheck(vkGetSwapchainImagesKHR(renderer->device, swapchain, &swapchain_image_count, nullptr ));
}

void Window::destroySwapchain() {
    vkDestroySwapchainKHR(renderer->device, swapchain, nullptr);
}


void Window::initSwapchainImages() {
    swapchain_images.resize(swapchain_image_count);
    swapchain_image_views.resize(swapchain_image_count);

    errorCheck(vkGetSwapchainImagesKHR(renderer->device, swapchain, &swapchain_image_count, swapchain_images.data()));
    for (uint32_t x = 0; x < swapchain_image_count; x++) {
        VkImageViewCreateInfo image_view_create_info {};
        image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        image_view_create_info.image = swapchain_images[x];
        image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        image_view_create_info.format = surface_format.format;
        image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        image_view_create_info.subresourceRange.baseMipLevel = 0;
        image_view_create_info.subresourceRange.levelCount = 1;
        image_view_create_info.subresourceRange.baseArrayLayer = 0;
        image_view_create_info.subresourceRange.layerCount = 1;
        errorCheck(vkCreateImageView(renderer->device, &image_view_create_info, nullptr, &swapchain_image_views[x]));
    }
}

void Window::destroySwapchainImages() {
    for(auto view : swapchain_image_views) {
		vkDestroyImageView(renderer->device, view, nullptr);
	}
}

void Window::initDepthStencilImage() {
	std::vector<VkFormat> try_formats {
		VK_FORMAT_D32_SFLOAT_S8_UINT,
		VK_FORMAT_D24_UNORM_S8_UINT,
		VK_FORMAT_D16_UNORM_S8_UINT,
		VK_FORMAT_D32_SFLOAT,
		VK_FORMAT_D16_UNORM
	};
	for(auto f : try_formats) {
		VkFormatProperties format_properties {};
		vkGetPhysicalDeviceFormatProperties(renderer->gpu, f, &format_properties);
		if(format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
			depth_stencil_format = f;
			break;
		}
	}
	if(depth_stencil_format == VK_FORMAT_UNDEFINED) {
		assert( 0 && "Depth stencil format not selected." );
		std::exit(-1);
	}
	if( (depth_stencil_format == VK_FORMAT_D32_SFLOAT_S8_UINT) ||
		(depth_stencil_format == VK_FORMAT_D24_UNORM_S8_UINT) ||
		(depth_stencil_format == VK_FORMAT_D16_UNORM_S8_UINT) ||
		(depth_stencil_format == VK_FORMAT_S8_UINT)) {
		stencil_available = true;
	}

	VkImageCreateInfo image_create_info {};
	image_create_info.sType					= VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_create_info.flags					= 0;
	image_create_info.imageType				= VK_IMAGE_TYPE_2D;
	image_create_info.format				= depth_stencil_format;
	image_create_info.extent.width			= surface_size_x;
	image_create_info.extent.height			= surface_size_y;
	image_create_info.extent.depth			= 1;
	image_create_info.mipLevels				= 1;
	image_create_info.arrayLayers			= 1;
	image_create_info.samples				= VK_SAMPLE_COUNT_1_BIT;
	image_create_info.tiling				= VK_IMAGE_TILING_OPTIMAL;
	image_create_info.usage					= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	image_create_info.sharingMode			= VK_SHARING_MODE_EXCLUSIVE;
	image_create_info.queueFamilyIndexCount	= VK_QUEUE_FAMILY_IGNORED;
	image_create_info.pQueueFamilyIndices	= nullptr;
	image_create_info.initialLayout			= VK_IMAGE_LAYOUT_UNDEFINED;

	errorCheck(vkCreateImage(renderer->device, &image_create_info, nullptr, &depth_stencil_image));

	VkMemoryRequirements image_memory_requirements {};
	vkGetImageMemoryRequirements(renderer->device, depth_stencil_image, &image_memory_requirements);

	uint32_t memory_index = findMemoryTypeIndex(&(renderer->gpu_memory_properties), &image_memory_requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkMemoryAllocateInfo memory_allocate_info {};
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = image_memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = memory_index;

	errorCheck(vkAllocateMemory(renderer->device, &memory_allocate_info, nullptr, &depth_stencil_image_memory));
	errorCheck(vkBindImageMemory(renderer->device, depth_stencil_image, depth_stencil_image_memory, 0));

	VkImageViewCreateInfo image_view_create_info {};
	image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	image_view_create_info.image = depth_stencil_image;
	image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	image_view_create_info.format = depth_stencil_format;
	image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	image_view_create_info.components.a	= VK_COMPONENT_SWIZZLE_IDENTITY;
	image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | (stencil_available ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
	image_view_create_info.subresourceRange.baseMipLevel = 0;
	image_view_create_info.subresourceRange.levelCount = 1;
	image_view_create_info.subresourceRange.baseArrayLayer = 0;
	image_view_create_info.subresourceRange.layerCount = 1;

	errorCheck(vkCreateImageView(renderer->device, &image_view_create_info, nullptr, &depth_stencil_image_view));
}

void Window::destroyDepthStencilImage() {
	vkDestroyImageView(renderer->device, depth_stencil_image_view, nullptr);
	vkFreeMemory(renderer->device, depth_stencil_image_memory, nullptr);
	vkDestroyImage(renderer->device, depth_stencil_image, nullptr);
}

void Window::initRenderPass() {
	std::array<VkAttachmentDescription, 1> attachments {};
	attachments[0].flags = 0;
	attachments[0].format = surface_format.format;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	std::array<VkAttachmentReference, 1> sub_pass_0_color_attachments {};
	sub_pass_0_color_attachments[0].attachment = 0;
	sub_pass_0_color_attachments[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	std::array<VkSubpassDescription, 1> sub_passes {};
	sub_passes[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	sub_passes[0].colorAttachmentCount = sub_pass_0_color_attachments.size();
	sub_passes[0].pColorAttachments	= sub_pass_0_color_attachments.data();	

    VkSubpassDependency dependency {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo render_pass_create_info {};
	render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_create_info.attachmentCount	= attachments.size();
	render_pass_create_info.pAttachments = attachments.data();
	render_pass_create_info.subpassCount = sub_passes.size();
	render_pass_create_info.pSubpasses = sub_passes.data();
    render_pass_create_info.dependencyCount = 1;
    render_pass_create_info.pDependencies = &dependency;

	errorCheck(vkCreateRenderPass(renderer->device, &render_pass_create_info, nullptr, &render_pass));
}

void Window::destroyRenderPass() {
	vkDestroyRenderPass(renderer->device, render_pass, nullptr);
}

void Window::initFramebuffers() {
	framebuffers.resize(swapchain_image_count);
	for (uint32_t x = 0; x < swapchain_image_count; x++) {
		std::array<VkImageView, 1> attachments {};
		attachments[0]	= swapchain_image_views[x];

		VkFramebufferCreateInfo framebuffer_create_info {};
		framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer_create_info.renderPass = render_pass;
		framebuffer_create_info.attachmentCount	= attachments.size();
		framebuffer_create_info.pAttachments = attachments.data();
		framebuffer_create_info.width = surface_size_x;
		framebuffer_create_info.height = surface_size_y;
		framebuffer_create_info.layers = 1;

		errorCheck(vkCreateFramebuffer(renderer->device, &framebuffer_create_info, nullptr, &framebuffers[x]));
	}
}

void Window::destroyFramebuffers() {
	for (auto f : framebuffers) {
		vkDestroyFramebuffer(renderer->device, f, nullptr);
	}
}

void Window::initSynchronizations() {
	VkFenceCreateInfo fence_create_info {};
	fence_create_info.sType	= VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	vkCreateFence(renderer->device, &fence_create_info, nullptr, &swapchain_image_available);
}

void Window::destroySynchronizations() {
	vkDestroyFence(renderer->device, swapchain_image_available, nullptr);
}


void Window::initGraphicsPipeline() {
    auto vert_shader_code = readFile("shaders/vert.spv");
    auto frag_shader_code = readFile("shaders/frag.spv");

    auto binding_description = Vertex::getBindingDescription();
    auto attribute_descriptions = Vertex::getAttributeDescriptions();
    
    VkShaderModule vert_shader_module = initShaderModule(vert_shader_code);
    VkShaderModule frag_shader_module = initShaderModule(frag_shader_code);

    VkPipelineShaderStageCreateInfo vert_shader_stage_info {};
    vert_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_shader_stage_info.module = vert_shader_module;
    vert_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo frag_shader_stage_info {};
    frag_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_shader_stage_info.module = frag_shader_module;
    frag_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo shader_stages[] = { vert_shader_stage_info, frag_shader_stage_info };

    // Vertex Input
    VkPipelineVertexInputStateCreateInfo vertex_input_info {};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribute_descriptions.size());
    vertex_input_info.pVertexBindingDescriptions = &binding_description;
    vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions.data();

    // Input Assembly
    VkPipelineInputAssemblyStateCreateInfo input_assembly {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    // Viewport
    VkViewport viewport {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) swapchain_extent.width;
    viewport.height = (float) swapchain_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    // Scissor
    VkRect2D scissor {};
    scissor.offset = {0, 0};
    scissor.extent = swapchain_extent;

    VkPipelineViewportStateCreateInfo viewport_state {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 0.0f;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    // Color blending
    VkPipelineColorBlendAttachmentState color_blend_attachment {};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_FALSE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blending {};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.logicOp = VK_LOGIC_OP_COPY;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;
    color_blending.blendConstants[0] = 0.0f;
    color_blending.blendConstants[1] = 0.0f;
    color_blending.blendConstants[2] = 0.0f;
    color_blending.blendConstants[3] = 0.0f;

    VkPipelineLayoutCreateInfo pipeline_layout_info {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 0;
    pipeline_layout_info.pSetLayouts = nullptr;
    pipeline_layout_info.pushConstantRangeCount = 0;
    pipeline_layout_info.pPushConstantRanges = 0;

    errorCheck(vkCreatePipelineLayout(renderer->device, &pipeline_layout_info, nullptr, &pipeline_layout));

    VkGraphicsPipelineCreateInfo pipeline_info {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = nullptr;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = nullptr;
    pipeline_info.layout = pipeline_layout;
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

    errorCheck(vkCreateGraphicsPipelines(renderer->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &graphics_pipeline));

    vkDestroyShaderModule(renderer->device, frag_shader_module, nullptr);
    vkDestroyShaderModule(renderer->device, vert_shader_module, nullptr);
}

void Window::destroyGraphicsPipeline() {
    vkDestroyPipeline(renderer->device, graphics_pipeline, nullptr);
    vkDestroyPipelineLayout(renderer->device, pipeline_layout, nullptr);
}

void Window::initCommandPool() {
    VkCommandPoolCreateInfo pool_info {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = renderer->graphics_family_index;
    pool_info.flags = 0;
    errorCheck(vkCreateCommandPool(renderer->device, &pool_info, nullptr, &command_pool));
}

void Window::destroyCommandPool() {
    vkDestroyCommandPool(renderer->device, command_pool, nullptr);
}

void Window::initCommandBuffers() {
    command_buffers.resize(framebuffers.size());
    VkCommandBufferAllocateInfo alloc_info {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = (uint32_t) command_buffers.size();

    errorCheck(vkAllocateCommandBuffers(renderer->device, &alloc_info, command_buffers.data()));

    for (size_t x = 0; x < command_buffers.size(); x++) {
        VkCommandBufferBeginInfo begin_info {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = 0;
        begin_info.pInheritanceInfo = nullptr;

        errorCheck(vkBeginCommandBuffer(command_buffers[x], &begin_info));

        VkRenderPassBeginInfo render_pass_info {};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_info.renderPass = render_pass;
        render_pass_info.framebuffer = framebuffers[x];
        render_pass_info.renderArea.offset = {0, 0};
        render_pass_info.renderArea.extent = swapchain_extent;

        VkClearValue clear_color = { 0.0f, 0.0f, 0.0f, 0.0f };
        render_pass_info.clearValueCount = 1;
        render_pass_info.pClearValues = &clear_color;

        vkCmdBeginRenderPass(command_buffers[x], &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(command_buffers[x], VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
        VkBuffer vertex_buffers[] = { vertex_buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(command_buffers[x], 0, 1, vertex_buffers, offsets);
        vkCmdDraw(command_buffers[x], static_cast<uint32_t>(vertices.size()), 1, 0, 0);
        vkCmdEndRenderPass(command_buffers[x]);

        errorCheck(vkEndCommandBuffer(command_buffers[x]));
    }
}

void Window::destroyCommandBuffers() {
    vkFreeCommandBuffers(renderer->device, command_pool, static_cast<uint32_t>(command_buffers.size()), command_buffers.data());
}

void Window::initVertexBuffer() {
    VkBufferCreateInfo buffer_info {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = sizeof(vertices[0]) * vertices.size();
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    errorCheck(vkCreateBuffer(renderer->device, &buffer_info, nullptr, &vertex_buffer));

    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(renderer->device, vertex_buffer, &memory_requirements);
    VkMemoryAllocateInfo allocate_info {};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = memory_requirements.size;
    allocate_info.memoryTypeIndex = findMemoryTypeIndex(&(renderer->gpu_memory_properties), &memory_requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    errorCheck(vkAllocateMemory(renderer->device, &allocate_info, nullptr, &vertex_buffer_memory));
    vkBindBufferMemory(renderer->device, vertex_buffer, vertex_buffer_memory, 0);

    void* data; 
    vkMapMemory(renderer->device, vertex_buffer_memory, 0, buffer_info.size, 0, &data);
    memcpy(data, vertices.data(), (size_t) buffer_info.size);
    vkUnmapMemory(renderer->device, vertex_buffer_memory);
}

void Window::destroyVertexBuffer() {
    vkDestroyBuffer(renderer->device, vertex_buffer, nullptr);
    vkFreeMemory(renderer->device, vertex_buffer_memory, nullptr);
}

VkShaderModule Window::initShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = code.size();
    create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shader_module;
    errorCheck(vkCreateShaderModule(renderer->device, &create_info, nullptr, &shader_module));
    return shader_module;
}
