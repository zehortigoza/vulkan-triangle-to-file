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
    uint32_t queueFamilyIndex = -1;

    for (uint32_t i = 0; i < deviceCount; i++) {
        VkQueueFamilyProperties queueFamilyProperties[16]; // Max 16 queue families
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[i], &queueFamilyCount, NULL);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[i], &queueFamilyCount, queueFamilyProperties);

        for (uint32_t j = 0; j < queueFamilyCount; j++) {
            // Find a queue that supports both graphics and compute
            if ((queueFamilyProperties[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                (queueFamilyProperties[j].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                physicalDevice = physicalDevices[i];
                queueFamilyIndex = j;
                break;
            }
        }
        if (physicalDevice != VK_NULL_HANDLE) {
            break;
        }
    }
    free(physicalDevices);

    if (physicalDevice == VK_NULL_HANDLE) {
        fprintf(stderr, "Failed to find a suitable physical device with graphics & compute queue!\n");
        return -1;
    }
    printf("Physical Device selected.\n");

    // 3. Logical Device Creation
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
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

    VkQueue queue;
    vkGetDeviceQueue(device, queueFamilyIndex, 0, &queue);
    printf("Graphics & Compute Queue obtained.\n");

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
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
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
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    // Subpass dependency to transition image layout
    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

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
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    VkPipelineLayout graphicsPipelineLayout;
    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, NULL, &graphicsPipelineLayout));
    printf("Graphics Pipeline Layout created.\n");

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
    pipelineInfo.layout = graphicsPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkPipeline graphicsPipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &graphicsPipeline));
    printf("Graphics Pipeline created.\n");

    vkDestroyShaderModule(device, fragShaderModule, NULL);
    vkDestroyShaderModule(device, vertShaderModule, NULL);


    // START: >>>>>>>>>> NEW COMPUTE SETUP SECTION <<<<<<<<<<

    // 8a. Create Compute Result Buffer
    VkBufferCreateInfo computeBufferInfo = {};
    computeBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    computeBufferInfo.size = sizeof(uint32_t) * 3;
    computeBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    computeBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer computeResultBuffer;
    VK_CHECK(vkCreateBuffer(device, &computeBufferInfo, NULL, &computeResultBuffer));

    VkMemoryRequirements computeMemReqs;
    vkGetBufferMemoryRequirements(device, computeResultBuffer, &computeMemReqs);

    VkMemoryAllocateInfo computeAllocInfo = {};
    computeAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    computeAllocInfo.allocationSize = computeMemReqs.size;
    computeAllocInfo.memoryTypeIndex = findMemoryType(physicalDevice, computeMemReqs.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkDeviceMemory computeResultBufferMemory;
    VK_CHECK(vkAllocateMemory(device, &computeAllocInfo, NULL, &computeResultBufferMemory));
    vkBindBufferMemory(device, computeResultBuffer, computeResultBufferMemory, 0);
    printf("Compute result buffer created.\n");

    // 8b. Create Compute Descriptor Set Layout
    VkDescriptorSetLayoutBinding bindings[2] = {};
    // Input image
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    // Output buffer
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo setLayoutInfo = {};
    setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setLayoutInfo.bindingCount = 2;
    setLayoutInfo.pBindings = bindings;

    VkDescriptorSetLayout computeSetLayout;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &setLayoutInfo, NULL, &computeSetLayout));

    // 8c. Create Compute Descriptor Pool and Set
    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1;

    VkDescriptorPool computeDescriptorPool;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, NULL, &computeDescriptorPool));

    VkDescriptorSetAllocateInfo setAllocInfo = {};
    setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool = computeDescriptorPool;
    setAllocInfo.descriptorSetCount = 1;
    setAllocInfo.pSetLayouts = &computeSetLayout;

    VkDescriptorSet computeDescriptorSet;
    VK_CHECK(vkAllocateDescriptorSets(device, &setAllocInfo, &computeDescriptorSet));

    // 8d. Update Descriptor Set
    VkDescriptorImageInfo descImageInfo = {};
    descImageInfo.imageView = offscreenImageView;
    descImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo descBufferInfo = {};
    descBufferInfo.buffer = computeResultBuffer;
    descBufferInfo.offset = 0;
    descBufferInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet writeSets[2] = {};
    writeSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeSets[0].dstSet = computeDescriptorSet;
    writeSets[0].dstBinding = 0;
    writeSets[0].descriptorCount = 1;
    writeSets[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writeSets[0].pImageInfo = &descImageInfo;

    writeSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeSets[1].dstSet = computeDescriptorSet;
    writeSets[1].dstBinding = 1;
    writeSets[1].descriptorCount = 1;
    writeSets[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writeSets[1].pBufferInfo = &descBufferInfo;

    vkUpdateDescriptorSets(device, 2, writeSets, 0, NULL);
    printf("Compute descriptor set created and updated.\n");

    // 8e. Create Compute Pipeline
    VkPipelineLayoutCreateInfo computePipelineLayoutInfo = {};
    computePipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computePipelineLayoutInfo.setLayoutCount = 1;
    computePipelineLayoutInfo.pSetLayouts = &computeSetLayout;

    VkPipelineLayout computePipelineLayout;
    VK_CHECK(vkCreatePipelineLayout(device, &computePipelineLayoutInfo, NULL, &computePipelineLayout));

    VkShaderModule computeShaderModule = createShaderModule(device, "check.comp.spv");

    VkPipelineShaderStageCreateInfo computeShaderStageInfo = {};
    computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStageInfo.module = computeShaderModule;
    computeShaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo computePipelineInfo = {};
    computePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineInfo.stage = computeShaderStageInfo;
    computePipelineInfo.layout = computePipelineLayout;

    VkPipeline computePipeline;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipelineInfo, NULL, &computePipeline));
    printf("Compute pipeline created.\n");

    vkDestroyShaderModule(device, computeShaderModule, NULL);

    // END: >>>>>>>>>> NEW COMPUTE SETUP SECTION <<<<<<<<<<

    // 9. Command Pool and Command Buffer Creation
    VkCommandPoolCreateInfo cmdPoolInfo = {};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.queueFamilyIndex = queueFamilyIndex;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkCommandPool commandPool;
    VK_CHECK(vkCreateCommandPool(device, &cmdPoolInfo, NULL, &commandPool));
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

    // ---- Graphics Pass ----
    VkClearValue clearColor = {0.0f, 0.0f, 0.0f, 1.0f}; // black color
    VkRenderPassBeginInfo renderPassBeginInfo = {};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = renderPass;
    renderPassBeginInfo.framebuffer = framebuffer;
    renderPassBeginInfo.renderArea.extent.width = IMAGE_WIDTH;
    renderPassBeginInfo.renderArea.extent.height = IMAGE_HEIGHT;
    renderPassBeginInfo.clearValueCount = 1;
    renderPassBeginInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);


    // START: >>>>>>>>>> NEW COMPUTE DISPATCH SECTION <<<<<<<<<<

    printf("Preparing for compute shader dispatch.\n");

    // The render pass automatically transitioned the image to VK_IMAGE_LAYOUT_GENERAL.
    // We add a barrier to ensure the graphics writes are finished before compute reads start.
    VkImageMemoryBarrier imageMemoryBarrier = {};
    imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.image = offscreenImage;
    imageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageMemoryBarrier.subresourceRange.levelCount = 1;
    imageMemoryBarrier.subresourceRange.layerCount = 1;
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL; // From render pass
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL; // Stays general

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // Wait for graphics to finish
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,          // Before compute starts
        0,
        0, NULL,
        0, NULL,
        1, &imageMemoryBarrier);

    // ---- Compute Pass ----
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &computeDescriptorSet, 0, NULL);

    // Dispatch the compute shader
    uint32_t groupCountX = (IMAGE_WIDTH + 15) / 16; // 16 is local_size_x
    uint32_t groupCountY = (IMAGE_HEIGHT + 15) / 16; // 16 is local_size_y
    vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1);
    printf("Compute shader dispatched.\n");

    // Add a barrier to ensure compute shader writes are visible to the host
    VkMemoryBarrier memoryBarrier = {};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, // After compute shader
        VK_PIPELINE_STAGE_HOST_BIT,           // Before host read
        0,
        1, &memoryBarrier,
        0, NULL,
        0, NULL);

    // END: >>>>>>>>>> NEW COMPUTE DISPATCH SECTION <<<<<<<<<<


#if DO_COPY
    // Image layout transition for offscreenImage from GENERAL to TRANSFER_SRC_OPTIMAL
    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL; // It's now in GENERAL layout
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; // From compute read
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT; // For copy command

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0, NULL,
                         0, NULL,
                         1, &imageMemoryBarrier);

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
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
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

    VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));
    printf("Command Buffer submitted and queue idle.\n");

    // START: >>>>>>>>>> NEW COMPUTE RESULT READBACK SECTION <<<<<<<<<<

    uint32_t *computeData;
    VK_CHECK(vkMapMemory(device, computeResultBufferMemory, 0, sizeof(uint32_t) * 3, 0, (void *)&computeData));
    uint32_t triangleCount = computeData[0];
    uint32_t backgroundCount = computeData[1];
    uint32_t totalCount = computeData[2];
    vkUnmapMemory(device, computeResultBufferMemory);

    printf("----------------------------------------\n");
    printf("Compute Shader Result: triangleCount: %u backgroundCount: %u totalCount: %u\n", triangleCount, backgroundCount, totalCount);
    printf("----------------------------------------\n");

    // END: >>>>>>>>>> NEW COMPUTE RESULT READBACK SECTION <<<<<<<<<<

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

    // NEW: Cleanup compute resources
    vkDestroyPipeline(device, computePipeline, NULL);
    vkDestroyPipelineLayout(device, computePipelineLayout, NULL);
    vkDestroyDescriptorSetLayout(device, computeSetLayout, NULL);
    vkDestroyDescriptorPool(device, computeDescriptorPool, NULL);
    vkDestroyBuffer(device, computeResultBuffer, NULL);
    vkFreeMemory(device, computeResultBufferMemory, NULL);

    vkDestroyFramebuffer(device, framebuffer, NULL);
    vkDestroyRenderPass(device, renderPass, NULL);
    vkDestroyPipeline(device, graphicsPipeline, NULL);
    vkDestroyPipelineLayout(device, graphicsPipelineLayout, NULL);
    vkDestroyImageView(device, offscreenImageView, NULL);
    vkDestroyImage(device, offscreenImage, NULL);
    vkFreeMemory(device, offscreenImageMemory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);

    printf("Vulkan resources cleaned up. Exiting.\n");

    return 0;
}