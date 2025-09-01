#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

// Define the dimensions of the output image
#define IMAGE_WIDTH 256
#define IMAGE_HEIGHT 256

#define DO_COPY 1

// Simple error checking macro
#define VK_CHECK(x)                                                              \
    do {                                                                         \
        VkResult err = x;                                                        \
        if (err) {                                                               \
            fprintf(stderr, "Detected Vulkan error: %d at %s:%d\n", err,         \
                    __FILE__, __LINE__);                                         \
            abort();                                                             \
        }                                                                        \
    } while (0)

static uint32_t *
readFile(const char *file, uint32_t *buffer_len)
{
    uint8_t *buffer = NULL;
    int fd, ret, len = 0, pos = 0;

    fd = open(file, O_RDONLY);
    if (fd < 0)
        return NULL;

    do {
        if (len == pos) {
            len += 512;
            buffer = realloc(buffer, len);
            if (!buffer)
                goto realloc_error;
        }

        ret = read(fd, buffer + pos, len - pos);
        if (ret == 0)
            break;
        if (ret < 0)
            goto error_reading;

        pos += ret;
    } while (1);

realloc_error:
    close(fd);
    *buffer_len = pos;
    return (uint32_t *)buffer;

error_reading:
    free(buffer);
    close(fd);
    return NULL;
}

// Helper function to create a shader module from SPIR-V bytecode
VkShaderModule createShaderModule(VkDevice device, const char *file) {
    VkShaderModuleCreateInfo createInfo = {};
    uint32_t *buffer;
    uint32_t buffer_len;

    buffer = readFile(file, &buffer_len);
    if (!buffer) {
        printf("Failed to read shader bytecode!\n");
        return NULL;
    }

    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = buffer_len;
    createInfo.pCode = buffer;

    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(device, &createInfo, NULL, &shaderModule));
    return shaderModule;
}

// Helper function to find a memory type index
uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    fprintf(stderr, "Failed to find suitable memory type!\n");
    abort();
}

int main() {
    // 1. Vulkan Instance Creation
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Vulkan Offscreen Triangle";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    VkInstance instance;
    VK_CHECK(vkCreateInstance(&createInfo, NULL, &instance));
    printf("Vulkan Instance created successfully.\n");

    // 2. Physical Device Selection
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);
    if (deviceCount == 0) {
        fprintf(stderr, "Failed to find GPUs with Vulkan support!\n");
        return -1;
    }
    VkPhysicalDevice* physicalDevices = (VkPhysicalDevice*)malloc(sizeof(VkPhysicalDevice) * deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices);

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamilyIndex = -1;

    for (uint32_t i = 0; i < deviceCount; i++) {
        VkQueueFamilyProperties queueFamilyProperties[16]; // Max 16 queue families
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[i], &queueFamilyCount, NULL);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[i], &queueFamilyCount, queueFamilyProperties);

        for (uint32_t j = 0; j < queueFamilyCount; j++) {
            if (queueFamilyProperties[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                physicalDevice = physicalDevices[i];
                graphicsQueueFamilyIndex = j;
                break;
            }
        }
        if (physicalDevice != VK_NULL_HANDLE) {
            break;
        }
    }
    free(physicalDevices);

    if (physicalDevice == VK_NULL_HANDLE) {
        fprintf(stderr, "Failed to find a suitable physical device with graphics queue!\n");
        return -1;
    }
    printf("Physical Device selected.\n");

    // 3. Logical Device Creation
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    float queuePriority = 1.0f;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceCreateInfo = {};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pEnabledFeatures = NULL; // No specific device features needed

    VkDevice device;
    VK_CHECK(vkCreateDevice(physicalDevice, &deviceCreateInfo, NULL, &device));
    printf("Logical Device created successfully.\n");

    VkQueue graphicsQueue;
    vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
    printf("Graphics Queue obtained.\n");

    // 4. Offscreen Image Creation
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = IMAGE_WIDTH;
    imageInfo.extent.height = IMAGE_HEIGHT;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM; // 8 bits per channel, RGBA
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VkImage offscreenImage;
    VK_CHECK(vkCreateImage(device, &imageInfo, NULL, &offscreenImage));
    printf("Offscreen Image created.\n");

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, offscreenImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkDeviceMemory offscreenImageMemory;
    VK_CHECK(vkAllocateMemory(device, &allocInfo, NULL, &offscreenImageMemory));
    vkBindImageMemory(device, offscreenImage, offscreenImageMemory, 0);
    printf("Offscreen Image memory allocated and bound.\n");

    // 5. Image View Creation
    VkImageViewCreateInfo imageViewInfo = {};
    imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewInfo.image = offscreenImage;
    imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewInfo.subresourceRange.baseMipLevel = 0;
    imageViewInfo.subresourceRange.levelCount = 1;
    imageViewInfo.subresourceRange.baseArrayLayer = 0;
    imageViewInfo.subresourceRange.layerCount = 1;

    VkImageView offscreenImageView;
    VK_CHECK(vkCreateImageView(device, &imageViewInfo, NULL, &offscreenImageView));
    printf("Offscreen Image View created.\n");

    // 6. Render Pass Creation
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Clear before rendering
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // Store after rendering
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // Optimized for color attachment

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkRenderPass renderPass;
    VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, NULL, &renderPass));
    printf("Render Pass created.\n");

    // 7. Framebuffer Creation
    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &offscreenImageView;
    framebufferInfo.width = IMAGE_WIDTH;
    framebufferInfo.height = IMAGE_HEIGHT;
    framebufferInfo.layers = 1;

    VkFramebuffer framebuffer;
    VK_CHECK(vkCreateFramebuffer(device, &framebufferInfo, NULL, &framebuffer));
    printf("Framebuffer created.\n");

    // 8. Graphics Pipeline Creation
    printf("Will call createShaderModule(device, triangle.vert.spv)\n");
    VkShaderModule vertShaderModule = createShaderModule(device, "triangle.vert.spv");
    printf("Will call createShaderModule(device, triangle.frag.spv)\n");
    VkShaderModule fragShaderModule = createShaderModule(device, "triangle.frag.spv");

    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;   // No vertex bindings (hardcoded triangle)
    vertexInputInfo.vertexAttributeDescriptionCount = 0; // No vertex attributes

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)IMAGE_WIDTH;
    viewport.height = (float)IMAGE_HEIGHT;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = IMAGE_WIDTH;
    scissor.extent.height = IMAGE_HEIGHT;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    VkPipelineLayout pipelineLayout;
    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, NULL, &pipelineLayout));
    printf("Pipeline Layout created.\n");

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = NULL;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkPipeline graphicsPipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &graphicsPipeline));
    printf("Graphics Pipeline created.\n");

    vkDestroyShaderModule(device, fragShaderModule, NULL);
    vkDestroyShaderModule(device, vertShaderModule, NULL);

    // 9. Command Pool and Command Buffer Creation
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkCommandPool commandPool;
    VK_CHECK(vkCreateCommandPool(device, &poolInfo, NULL, &commandPool));
    printf("Command Pool created.\n");

    VkCommandBufferAllocateInfo allocCmdBufferInfo = {};
    allocCmdBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocCmdBufferInfo.commandPool = commandPool;
    allocCmdBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocCmdBufferInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    VK_CHECK(vkAllocateCommandBuffers(device, &allocCmdBufferInfo, &commandBuffer));
    printf("Command Buffer allocated.\n");

    // 10. Recording Commands
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
    printf("Command Buffer recording started.\n");

    // Image layout transition for offscreenImage from UNDEFINED to COLOR_ATTACHMENT_OPTIMAL
    VkImageMemoryBarrier imageLayoutBarrier = {};
    imageLayoutBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageLayoutBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageLayoutBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    imageLayoutBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageLayoutBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageLayoutBarrier.image = offscreenImage;
    imageLayoutBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageLayoutBarrier.subresourceRange.baseMipLevel = 0;
    imageLayoutBarrier.subresourceRange.levelCount = 1;
    imageLayoutBarrier.subresourceRange.baseArrayLayer = 0;
    imageLayoutBarrier.subresourceRange.layerCount = 1;
    imageLayoutBarrier.srcAccessMask = 0;
    imageLayoutBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0,
                         0, NULL,
                         0, NULL,
                         1, &imageLayoutBarrier);

    VkClearValue clearColor = {1.0f, 1.0f, 1.0f, 1.0f}; // black color
    VkRenderPassBeginInfo renderPassBeginInfo = {};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = renderPass;
    renderPassBeginInfo.framebuffer = framebuffer;
    renderPassBeginInfo.renderArea.offset.x = 0;
    renderPassBeginInfo.renderArea.offset.y = 0;
    renderPassBeginInfo.renderArea.extent.width = IMAGE_WIDTH;
    renderPassBeginInfo.renderArea.extent.height = IMAGE_HEIGHT;
    renderPassBeginInfo.clearValueCount = 1;
    renderPassBeginInfo.pClearValues = &clearColor;

    printf("Before vkCmdBeginRenderPass()\n");
    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    printf("After vkCmdBeginRenderPass()\n");

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    printf("Before vkCmdDraw()\n");
    // Draw a single triangle (3 vertices)
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    printf("After vkCmdDraw()\n");

    vkCmdEndRenderPass(commandBuffer);

#if DO_COPY
    // Image layout transition for offscreenImage from COLOR_ATTACHMENT_OPTIMAL to TRANSFER_SRC_OPTIMAL
    imageLayoutBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    imageLayoutBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    imageLayoutBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imageLayoutBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0, NULL,
                         0, NULL,
                         1, &imageLayoutBarrier);

    // Create a host-visible buffer to copy image data to
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = IMAGE_WIDTH * IMAGE_HEIGHT * 4; // RGBA, 4 bytes per pixel
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer stagingBuffer;
    VK_CHECK(vkCreateBuffer(device, &bufferInfo, NULL, &stagingBuffer));

    VkMemoryRequirements stagingMemRequirements;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &stagingMemRequirements);

    VkMemoryAllocateInfo stagingAllocInfo = {};
    stagingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAllocInfo.allocationSize = stagingMemRequirements.size;
    stagingAllocInfo.memoryTypeIndex = findMemoryType(physicalDevice, stagingMemRequirements.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkDeviceMemory stagingBufferMemory;
    VK_CHECK(vkAllocateMemory(device, &stagingAllocInfo, NULL, &stagingBufferMemory));
    vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0);
    printf("Staging buffer created and memory allocated.\n");

    // Copy image to buffer
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = 0;
    region.imageOffset.y = 0;
    region.imageOffset.z = 0;
    region.imageExtent.width = IMAGE_WIDTH;
    region.imageExtent.height = IMAGE_HEIGHT;
    region.imageExtent.depth = 1;

    printf("vkCmdCopyImageToBuffer()\n");
    vkCmdCopyImageToBuffer(commandBuffer,
                           offscreenImage,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           stagingBuffer,
                           1, &region);
#endif
    VK_CHECK(vkEndCommandBuffer(commandBuffer));
    printf("Command Buffer recording ended.\n");

    // 11. Submission and Synchronization
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(graphicsQueue));
    printf("Command Buffer submitted and queue idle.\n");

#if DO_COPY
    // 12. Readback and Save to PPM
    void* data;
    VK_CHECK(vkMapMemory(device, stagingBufferMemory, 0, bufferInfo.size, 0, &data));

    FILE* fp = fopen("output.ppm", "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open output.ppm for writing!\n");
        return -1;
    }

    fprintf(fp, "P6\n%d %d\n255\n", IMAGE_WIDTH, IMAGE_HEIGHT);
    // Write pixel data (RGBA to RGB for PPM)
    for (int y = 0; y < IMAGE_HEIGHT; y++) {
        for (int x = 0; x < IMAGE_WIDTH; x++) {
            unsigned char* pixel = (unsigned char*)data + (y * IMAGE_WIDTH + x) * 4;
            fwrite(pixel, 1, 3, fp); // Write R, G, B channels
        }
    }
    fclose(fp);
    printf("Rendered image saved to output.ppm\n");

    vkUnmapMemory(device, stagingBufferMemory);
#endif

    // 13. Cleanup
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    vkDestroyCommandPool(device, commandPool, NULL);

#if DO_COPY
    vkDestroyBuffer(device, stagingBuffer, NULL);
    vkFreeMemory(device, stagingBufferMemory, NULL);
#endif

    vkDestroyFramebuffer(device, framebuffer, NULL);
    vkDestroyRenderPass(device, renderPass, NULL);
    vkDestroyPipeline(device, graphicsPipeline, NULL);
    vkDestroyPipelineLayout(device, pipelineLayout, NULL);
    vkDestroyImageView(device, offscreenImageView, NULL);
    vkDestroyImage(device, offscreenImage, NULL);
    vkFreeMemory(device, offscreenImageMemory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);

    printf("Vulkan resources cleaned up. Exiting.\n");

    return 0;
}

