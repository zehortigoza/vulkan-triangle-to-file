#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define WIDTH 512
#define HEIGHT 512

#define VK_CHECK(f) \
{ \
    VkResult res = (f); \
    if (res != VK_SUCCESS) { \
        fprintf(stderr, "Fatal : VkResult is %d in %s at line %d\n", res, __FILE__, __LINE__); \
        exit(1); \
    } \
}

// Utility to read SPIR-V files
char* read_shader(const char* filename, size_t* size) {
    FILE* f = fopen(filename, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buffer = malloc(*size);
    fread(buffer, 1, *size, f);
    fclose(f);
    return buffer;
}

uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    fprintf(stderr, "Failed to find suitable memory type!\n");
    exit(1);
}

int main() {
    VkInstance instance;
    VkApplicationInfo app_info = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_2 };
    VkInstanceCreateInfo create_info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app_info };
    VK_CHECK(vkCreateInstance(&create_info, NULL, &instance));

    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, NULL);
    VkPhysicalDevice* physical_devices = malloc(sizeof(VkPhysicalDevice) * device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, physical_devices);
    VkPhysicalDevice physical_device = physical_devices[0]; // Picking the first available device

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, NULL);
    uint32_t graphics_queue_index = 0; 

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphics_queue_index,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority
    };

    VkDevice device;
    VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info
    };
    VK_CHECK(vkCreateDevice(physical_device, &device_create_info, NULL, &device));

    VkQueue queue;
    vkGetDeviceQueue(device, graphics_queue_index, 0, &queue);

    VkCommandPool command_pool;
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = graphics_queue_index
    };
    VK_CHECK(vkCreateCommandPool(device, &pool_info, NULL, &command_pool));

    // --- Create Target Image ---
    VkImage image;
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {WIDTH, HEIGHT, 1},
        .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    VK_CHECK(vkCreateImage(device, &image_info, NULL, &image));

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device, image, &mem_reqs);
    VkDeviceMemory image_memory;
    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = find_memory_type(physical_device, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };
    VK_CHECK(vkAllocateMemory(device, &alloc_info, NULL, &image_memory));
    vkBindImageMemory(device, image, image_memory, 0);

    VkImageView image_view;
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    VK_CHECK(vkCreateImageView(device, &view_info, NULL, &image_view));

    // --- Render Pass & Framebuffer ---
    VkAttachmentDescription attachment = {
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, // Clear to Red initially
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL // Prepare for readback
    };
    VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &color_ref };
    VkRenderPass render_pass;
    VkRenderPassCreateInfo rp_info = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &subpass };
    VK_CHECK(vkCreateRenderPass(device, &rp_info, NULL, &render_pass));

    VkFramebuffer framebuffer;
    VkFramebufferCreateInfo fb_info = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = render_pass, .attachmentCount = 1, .pAttachments = &image_view, .width = WIDTH, .height = HEIGHT, .layers = 1 };
    VK_CHECK(vkCreateFramebuffer(device, &fb_info, NULL, &framebuffer));

    // --- Pipeline ---
    size_t vert_size, frag_size;
    char *vert_code = read_shader("vert.spv", &vert_size);
    char *frag_code = read_shader("frag.spv", &frag_size);
    assert(vert_code && frag_code && "Failed to read shaders! Compile them to vert.spv and frag.spv.");

    VkShaderModule vert_module, frag_module;
    VkShaderModuleCreateInfo vert_info = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = vert_size, .pCode = (uint32_t*)vert_code };
    VkShaderModuleCreateInfo frag_info = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = frag_size, .pCode = (uint32_t*)frag_code };
    vkCreateShaderModule(device, &vert_info, NULL, &vert_module);
    vkCreateShaderModule(device, &frag_info, NULL, &frag_module);

    VkPipelineShaderStageCreateInfo shader_stages[] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert_module, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag_module, .pName = "main" }
    };

    VkPipelineVertexInputStateCreateInfo vertex_input = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    
    VkViewport viewport = { 0.0f, 0.0f, (float)WIDTH, (float)HEIGHT, 0.0f, 1.0f };
    VkRect2D scissor = { {0, 0}, {WIDTH, HEIGHT} };
    VkPipelineViewportStateCreateInfo viewport_state = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor };
    VkPipelineRasterizationStateCreateInfo rasterizer = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1.0f, .cullMode = VK_CULL_MODE_NONE };
    VkPipelineMultisampleStateCreateInfo multisampling = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState color_blend_attachment = { .colorWriteMask = 0xF };
    VkPipelineColorBlendStateCreateInfo color_blending = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &color_blend_attachment };
    
    VkPipelineLayout pipeline_layout;
    VkPipelineLayoutCreateInfo pipeline_layout_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    vkCreatePipelineLayout(device, &pipeline_layout_info, NULL, &pipeline_layout);

    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = shader_stages,
        .pVertexInputState = &vertex_input, .pInputAssemblyState = &input_assembly, .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer, .pMultisampleState = &multisampling, .pColorBlendState = &color_blending,
        .layout = pipeline_layout, .renderPass = render_pass, .subpass = 0
    };
    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline));

    // --- Command Recording ---
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cmd_alloc = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VK_CHECK(vkAllocateCommandBuffers(device, &cmd_alloc, &cmd));

    VkCommandBufferBeginInfo begin_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cmd, &begin_info);

    VkClearValue clear_color = {{{1.0f, 0.0f, 0.0f, 1.0f}}}; // Initial Renderpass clear is RED
    VkRenderPassBeginInfo rp_begin = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = render_pass, .framebuffer = framebuffer, .renderArea = {{0, 0}, {WIDTH, HEIGHT}}, .clearValueCount = 1, .pClearValues = &clear_color };
    
    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    // --- vkCmdClearAttachments Test ---
    // We clear a center rectangle to GREEN to test driver bounds logic.
    VkClearAttachment clear_attachment = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .colorAttachment = 0, // Must map to subpass attachment index 0
        .clearValue = {{{0.0f, 1.0f, 0.0f, 1.0f}}} // Green
    };
    VkClearRect clear_rect = {
        .rect = { {WIDTH / 4, HEIGHT / 4}, {WIDTH / 2, HEIGHT / 2} }, // Center block
        .baseArrayLayer = 0,
        .layerCount = 1
    };
    vkCmdClearAttachments(cmd, 1, &clear_attachment, 1, &clear_rect);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0); // Draws a BLUE triangle on top

    vkCmdEndRenderPass(cmd);

    // --- Readback Buffer ---
    VkBuffer readback_buffer;
    VkBufferCreateInfo buf_info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = WIDTH * HEIGHT * 4, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT };
    vkCreateBuffer(device, &buf_info, NULL, &readback_buffer);
    VkMemoryRequirements buf_mem_reqs;
    vkGetBufferMemoryRequirements(device, readback_buffer, &buf_mem_reqs);
    VkDeviceMemory buf_memory;
    VkMemoryAllocateInfo buf_alloc = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = buf_mem_reqs.size, .memoryTypeIndex = find_memory_type(physical_device, buf_mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    vkAllocateMemory(device, &buf_alloc, NULL, &buf_memory);
    vkBindBufferMemory(device, readback_buffer, buf_memory, 0);

    // Copy Image to Buffer
    VkBufferImageCopy region = { .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {WIDTH, HEIGHT, 1} };
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_buffer, 1, &region);

    VK_CHECK(vkEndCommandBuffer(cmd));

    // Submit and Wait
    VkSubmitInfo submit_info = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd };
    vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    // --- Save to PPM ---
    void* data;
    vkMapMemory(device, buf_memory, 0, WIDTH * HEIGHT * 4, 0, &data);
    
    FILE* ppm = fopen("output.ppm", "wb");
    fprintf(ppm, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    uint8_t* pixels = (uint8_t*)data;
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        // Assuming R8G8B8A8, writing RGB
        fwrite(&pixels[i * 4], 1, 3, ppm);
    }
    fclose(ppm);
    vkUnmapMemory(device, buf_memory);

    printf("Image written to output.ppm\n");

    // Cleanup
    vkDestroyBuffer(device, readback_buffer, NULL);
    vkFreeMemory(device, buf_memory, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    vkDestroyShaderModule(device, vert_module, NULL);
    vkDestroyShaderModule(device, frag_module, NULL);
    vkDestroyFramebuffer(device, framebuffer, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    vkDestroyImageView(device, image_view, NULL);
    vkDestroyImage(device, image, NULL);
    vkFreeMemory(device, image_memory, NULL);
    vkDestroyCommandPool(device, command_pool, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    
    free(vert_code);
    free(frag_code);
    free(physical_devices);

    return 0;
}
