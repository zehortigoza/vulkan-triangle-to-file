#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#define WIDTH 256
#define HEIGHT 256

#define VK_CHECK(x) \
    do { \
        VkResult err = x; \
        if (err) { \
            fprintf(stderr, "Vulkan error %d at %s:%d\n", err, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

// Explicitly aligned structure to match the GLSL offsets
typedef struct {
    float pos0[2];      // Offset 0
    float pos1[2];      // Offset 8
    float pos2[2];      // Offset 16
    float padding[2];   // Offset 24 - Padding for 16-byte alignment of the vec4
    float color[4];     // Offset 32
} PushConstants;

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    assert(0 && "Failed to find suitable memory type!");
    return 0;
}

char* readFile(const char* filename, size_t* size) {
    FILE* file = fopen(filename, "rb");
    assert(file && "Failed to open file!");
    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    rewind(file);
    char* buffer = (char*)malloc(*size);
    fread(buffer, 1, *size, file);
    fclose(file);
    return buffer;
}

VkShaderModule createShaderModule(VkDevice device, const char* filename) {
    size_t size;
    char* code = readFile(filename, &size);
    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = (const uint32_t*)code
    };
    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(device, &createInfo, NULL, &shaderModule));
    free(code);
    return shaderModule;
}

int main() {
    // 1. Instance & Device Initialization
    VkApplicationInfo appInfo = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_0 };
    VkInstanceCreateInfo createInfo = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &appInfo };
    VkInstance instance;
    VK_CHECK(vkCreateInstance(&createInfo, NULL, &instance));

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);
    VkPhysicalDevice* physicalDevices = malloc(sizeof(VkPhysicalDevice) * deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices);
    VkPhysicalDevice physicalDevice = physicalDevices[0]; 

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0, 
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };

    VkDeviceCreateInfo deviceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo
    };
    VkDevice device;
    VK_CHECK(vkCreateDevice(physicalDevice, &deviceCreateInfo, NULL, &device));

    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);

    // 2. Command Pool & Buffer
    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    };
    VkCommandPool commandPool;
    VK_CHECK(vkCreateCommandPool(device, &poolInfo, NULL, &commandPool));

    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &cmd));

    // 3. Render Target Image
    VkImageCreateInfo imageInfo = {
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
    VkImage image;
    VK_CHECK(vkCreateImage(device, &imageInfo, NULL, &image));

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, image, &memReqs);
    VkMemoryAllocateInfo memAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };
    VkDeviceMemory imageMemory;
    VK_CHECK(vkAllocateMemory(device, &memAllocInfo, NULL, &imageMemory));
    vkBindImageMemory(device, image, imageMemory, 0);

    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    VkImageView imageView;
    VK_CHECK(vkCreateImageView(device, &viewInfo, NULL, &imageView));

    // 4. Render Pass & Framebuffer
    VkAttachmentDescription attachment = {
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    };
    VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &colorRef};
    VkRenderPassCreateInfo renderPassInfo = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &subpass};
    VkRenderPass renderPass;
    VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, NULL, &renderPass));

    VkFramebufferCreateInfo fbInfo = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = renderPass, .attachmentCount = 1, .pAttachments = &imageView, .width = WIDTH, .height = HEIGHT, .layers = 1};
    VkFramebuffer framebuffer;
    VK_CHECK(vkCreateFramebuffer(device, &fbInfo, NULL, &framebuffer));

    // 5. Pipeline Setup with Push Constants
    VkShaderModule vertShader = createShaderModule(device, "vert.spv");
    VkShaderModule fragShader = createShaderModule(device, "frag.spv");
    
    VkPipelineShaderStageCreateInfo shaderStages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertShader, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragShader, .pName = "main"}
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    
    VkViewport viewport = {0.0f, 0.0f, (float)WIDTH, (float)HEIGHT, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {WIDTH, HEIGHT}};
    VkPipelineViewportStateCreateInfo viewportState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor};
    
    VkPipelineRasterizationStateCreateInfo rasterizer = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1.0f, .cullMode = VK_CULL_MODE_BACK_BIT, .frontFace = VK_FRONT_FACE_CLOCKWISE};
    VkPipelineMultisampleStateCreateInfo multisampling = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {.colorWriteMask = 0xF};
    VkPipelineColorBlendStateCreateInfo colorBlending = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &colorBlendAttachment};

    // Define Push Constant Range
    VkPushConstantRange pushConstantRange = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(PushConstants)
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange
    };
    VkPipelineLayout pipelineLayout;
    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, NULL, &pipelineLayout));

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = shaderStages,
        .pVertexInputState = &vertexInputInfo, .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState, .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling, .pColorBlendState = &colorBlending,
        .layout = pipelineLayout, .renderPass = renderPass, .subpass = 0
    };
    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline));

    // 6. Query Pool Setup
    VkQueryPoolCreateInfo queryPoolInfo = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_OCCLUSION,
        .queryCount = 1
    };
    VkQueryPool queryPool;
    VK_CHECK(vkCreateQueryPool(device, &queryPoolInfo, NULL, &queryPool));

    // 7. Buffers for Query Results and Image Copy
    VkDeviceSize queryBufferSize = 2 * sizeof(uint64_t); 
    VkBufferCreateInfo queryBufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = queryBufferSize, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VkBuffer queryBuffer;
    VK_CHECK(vkCreateBuffer(device, &queryBufferInfo, NULL, &queryBuffer));
    vkGetBufferMemoryRequirements(device, queryBuffer, &memReqs);
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory queryBufferMemory;
    VK_CHECK(vkAllocateMemory(device, &memAllocInfo, NULL, &queryBufferMemory));
    vkBindBufferMemory(device, queryBuffer, queryBufferMemory, 0);

    void* mappedQueryBuf;
    vkMapMemory(device, queryBufferMemory, 0, queryBufferSize, 0, &mappedQueryBuf);
    memset(mappedQueryBuf, 0xAA, queryBufferSize); 
    vkUnmapMemory(device, queryBufferMemory);

    VkDeviceSize imageBufferSize = WIDTH * HEIGHT * 4;
    VkBufferCreateInfo imageBufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = imageBufferSize, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VkBuffer imageBuffer;
    VK_CHECK(vkCreateBuffer(device, &imageBufferInfo, NULL, &imageBuffer));
    vkGetBufferMemoryRequirements(device, imageBuffer, &memReqs);
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory imageBufferMemory;
    VK_CHECK(vkAllocateMemory(device, &memAllocInfo, NULL, &imageBufferMemory));
    vkBindBufferMemory(device, imageBuffer, imageBufferMemory, 0);

    // 8. Record Command Buffer
    VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    vkCmdResetQueryPool(cmd, queryPool, 0, 1);

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkRenderPassBeginInfo renderPassBeginInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = renderPass, .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {WIDTH, HEIGHT}}, .clearValueCount = 1, .pClearValues = &clearColor
    };
    
    vkCmdBeginRenderPass(cmd, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Supply our vertices and colors via push constants
    PushConstants pcData = {
        .pos0 = { 0.0f, -0.5f },
        .pos1 = { 0.5f,  0.5f },
        .pos2 = {-0.5f,  0.5f },
        .padding = { 0.0f, 0.0f },
        .color = { 1.0f, 0.0f, 0.0f, 1.0f } // Red
    };
    
    vkCmdPushConstants(
        cmd, 
        pipelineLayout, 
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
        0, 
        sizeof(PushConstants), 
        &pcData
    );

    vkCmdBeginQuery(cmd, queryPool, 0, 0);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndQuery(cmd, queryPool, 0);

    vkCmdEndRenderPass(cmd);

    // Copy Image to Buffer (Fixed layout order)
    VkBufferImageCopy region = {
        .bufferOffset = 0, .bufferRowLength = 0, .bufferImageHeight = 0,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageOffset = {0, 0, 0}, .imageExtent = {WIDTH, HEIGHT, 1}
    };
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, imageBuffer, 1, &region);

    // Copy Query Results to Buffer
    vkCmdCopyQueryPoolResults(
        cmd, 
        queryPool, 
        0, 1, 
        queryBuffer, 
        0, 
        2 * sizeof(uint64_t), 
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT
    );

    VK_CHECK(vkEndCommandBuffer(cmd));

    // 9. Submit & Wait
    VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));

    // 10. Verification of vkCmdCopyQueryPoolResults
    vkMapMemory(device, queryBufferMemory, 0, queryBufferSize, 0, &mappedQueryBuf);
    uint64_t* results = (uint64_t*)mappedQueryBuf;
    
    printf("--- vkCmdCopyQueryPoolResults Output ---\n");
    printf("Raw Bytes: ");
    uint8_t* rawBytes = (uint8_t*)mappedQueryBuf;
    for (int i = 0; i < 16; i++) {
        printf("%02X ", rawBytes[i]);
    }
    printf("\n");

    uint64_t pixel_count = results[0];
    uint64_t availability = results[1];
    
    printf("Parsed Pixel Count (Result 0): %lu\n", pixel_count);
    printf("Parsed Availability (Result 1): %lu\n", availability);

    if (availability != 1) {
        printf("[FAIL] Expected availability bit to be 1, got %lu\n", availability);
    } else {
        printf("[PASS] Availability bit is correctly set.\n");
    }

    if (pixel_count == 0xAAAAAAAAAAAAAAAA) {
        printf("[FAIL] Query payload untouched by driver.\n");
    } else if (pixel_count > 0) {
        printf("[PASS] Occlusion query payload written. Value makes sense.\n");
    } else {
        printf("[WARN] Occlusion payload is 0. Check rendering logic or driver write.\n");
    }
    printf("----------------------------------------\n");

    vkUnmapMemory(device, queryBufferMemory);

    // 11. Write Image to PPM file
    void* mappedImageBuf;
    vkMapMemory(device, imageBufferMemory, 0, imageBufferSize, 0, &mappedImageBuf);
    FILE* ppmFile = fopen("output.ppm", "wb");
    fprintf(ppmFile, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    uint8_t* pixels = (uint8_t*)mappedImageBuf;
    for (int i = 0; i < WIDTH * HEIGHT * 4; i += 4) {
        fwrite(&pixels[i], 1, 3, ppmFile); 
    }
    fclose(ppmFile);
    vkUnmapMemory(device, imageBufferMemory);
    printf("Saved render to output.ppm\n");

    // 12. Cleanup
    vkDestroyBuffer(device, imageBuffer, NULL);
    vkFreeMemory(device, imageBufferMemory, NULL);
    vkDestroyBuffer(device, queryBuffer, NULL);
    vkFreeMemory(device, queryBufferMemory, NULL);
    vkDestroyQueryPool(device, queryPool, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, pipelineLayout, NULL);
    vkDestroyShaderModule(device, vertShader, NULL);
    vkDestroyShaderModule(device, fragShader, NULL);
    vkDestroyRenderPass(device, renderPass, NULL);
    vkDestroyFramebuffer(device, framebuffer, NULL);
    vkDestroyImageView(device, imageView, NULL);
    vkDestroyImage(device, image, NULL);
    vkFreeMemory(device, imageMemory, NULL);
    vkDestroyCommandPool(device, commandPool, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    free(physicalDevices);

    return 0;
}