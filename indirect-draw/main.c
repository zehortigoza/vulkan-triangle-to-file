#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#define WIDTH 512
#define HEIGHT 512

#define VK_CHECK(f) \
{ \
    VkResult res = (f); \
    if (res != VK_SUCCESS) { \
        printf("Fatal : VkResult is %d in %s at line %d\n", res, __FILE__, __LINE__); \
        assert(res == VK_SUCCESS); \
    } \
}

// Vertex structure
typedef struct {
    float pos[2];
    float color[3];
} Vertex;

// Vertex data: Two triangles. First is Red, Second is Blue.
const Vertex vertices[] = {
    // Triangle 0 (Red) - Vertices 0, 1, 2
    {{ 0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{ 0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}},
    {{-0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}},
    // Triangle 1 (Blue) - Vertices 3, 4, 5
    {{ 0.0f, -0.5f}, {0.0f, 0.0f, 1.0f}},
    {{ 0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}},
    {{-0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}}
};

// Helper: Find suitable memory type
uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    printf("Failed to find suitable memory type!\n");
    exit(1);
}

// Helper: Read file
char* readFile(const char* filename, size_t* size) {
    FILE* file = fopen(filename, "rb");
    if (!file) { printf("Failed to open %s\n", filename); exit(1); }
    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    rewind(file);
    char* buffer = (char*)malloc(*size);
    fread(buffer, 1, *size, file);
    fclose(file);
    return buffer;
}

int main() {
    VkInstance instance;
    VkApplicationInfo appInfo = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_0 };
    VkInstanceCreateInfo createInfo = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &appInfo };
    VK_CHECK(vkCreateInstance(&createInfo, NULL, &instance));

    // Pick first physical device
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);
    VkPhysicalDevice* physicalDevices = malloc(sizeof(VkPhysicalDevice) * deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices);
    VkPhysicalDevice physicalDevice = physicalDevices[0];

    // Find queue family
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, NULL);
    VkQueueFamilyProperties* queueFamilies = malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies);
    uint32_t graphicsQueueFamily = 0;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { graphicsQueueFamily = i; break; }
    }

    // Create Logical Device
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = graphicsQueueFamily, .queueCount = 1, .pQueuePriorities = &queuePriority };
    
    // Enable multiDrawIndirect if you want to test drawCount > 1
    VkPhysicalDeviceFeatures deviceFeatures = {0};
    deviceFeatures.multiDrawIndirect = VK_TRUE; 

    VkDeviceCreateInfo deviceCreateInfo = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &queueCreateInfo, .pEnabledFeatures = &deviceFeatures };
    VkDevice device;
    VK_CHECK(vkCreateDevice(physicalDevice, &deviceCreateInfo, NULL, &device));

    VkQueue queue;
    vkGetDeviceQueue(device, graphicsQueueFamily, 0, &queue);

    // Create Command Pool
    VkCommandPool commandPool;
    VkCommandPoolCreateInfo poolInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = graphicsQueueFamily };
    VK_CHECK(vkCreateCommandPool(device, &poolInfo, NULL, &commandPool));

    // Create Color Image (Offscreen target)
    VkImage colorImage;
    VkImageCreateInfo imageInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {WIDTH, HEIGHT, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
    VK_CHECK(vkCreateImage(device, &imageInfo, NULL, &colorImage));

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, colorImage, &memReqs);
    VkDeviceMemory imageMemory;
    VkMemoryAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = memReqs.size, .memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
    VK_CHECK(vkAllocateMemory(device, &allocInfo, NULL, &imageMemory));
    vkBindImageMemory(device, colorImage, imageMemory, 0);

    VkImageView colorImageView;
    VkImageViewCreateInfo viewInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = colorImage, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
    VK_CHECK(vkCreateImageView(device, &viewInfo, NULL, &colorImageView));

    // Render Pass
    VkAttachmentDescription attachment = { .format = VK_FORMAT_R8G8B8A8_UNORM, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL };
    VkAttachmentReference colorAttachmentRef = { .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &colorAttachmentRef };
    VkRenderPassCreateInfo renderPassInfo = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &subpass };
    VkRenderPass renderPass;
    VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, NULL, &renderPass));

    // Framebuffer
    VkFramebufferCreateInfo framebufferInfo = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = renderPass, .attachmentCount = 1, .pAttachments = &colorImageView, .width = WIDTH, .height = HEIGHT, .layers = 1 };
    VkFramebuffer framebuffer;
    VK_CHECK(vkCreateFramebuffer(device, &framebufferInfo, NULL, &framebuffer));

    // Shaders
    size_t vertSize, fragSize;
    char *vertCode = readFile("vert.spv", &vertSize);
    char *fragCode = readFile("frag.spv", &fragSize);

    VkShaderModuleCreateInfo vertModuleInfo = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = vertSize, .pCode = (uint32_t*)vertCode };
    VkShaderModuleCreateInfo fragModuleInfo = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = fragSize, .pCode = (uint32_t*)fragCode };
    VkShaderModule vertModule, fragModule;
    vkCreateShaderModule(device, &vertModuleInfo, NULL, &vertModule);
    vkCreateShaderModule(device, &fragModuleInfo, NULL, &fragModule);

    // Pipeline
    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertModule, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragModule, .pName = "main" }
    };

    VkVertexInputBindingDescription bindingDescription = { .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription attributeDescriptions[] = {
        { .binding = 0, .location = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, pos) },
        { .binding = 0, .location = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, color) }
    };
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &bindingDescription, .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attributeDescriptions };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, .primitiveRestartEnable = VK_FALSE };
    VkViewport viewport = { .x = 0.0f, .y = 0.0f, .width = (float)WIDTH, .height = (float)HEIGHT, .minDepth = 0.0f, .maxDepth = 1.0f };
    VkRect2D scissor = { .offset = {0, 0}, .extent = {WIDTH, HEIGHT} };
    VkPipelineViewportStateCreateInfo viewportState = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor };
    VkPipelineRasterizationStateCreateInfo rasterizer = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1.0f, .cullMode = VK_CULL_MODE_BACK_BIT, .frontFace = VK_FRONT_FACE_CLOCKWISE };
    VkPipelineMultisampleStateCreateInfo multisampling = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState colorBlendAttachment = { .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT, .blendEnable = VK_FALSE };
    VkPipelineColorBlendStateCreateInfo colorBlending = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &colorBlendAttachment };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkPipelineLayout pipelineLayout;
    vkCreatePipelineLayout(device, &pipelineLayoutInfo, NULL, &pipelineLayout);

    VkGraphicsPipelineCreateInfo pipelineInfo = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = shaderStages, .pVertexInputState = &vertexInputInfo, .pInputAssemblyState = &inputAssembly, .pViewportState = &viewportState, .pRasterizationState = &rasterizer, .pMultisampleState = &multisampling, .pColorBlendState = &colorBlending, .layout = pipelineLayout, .renderPass = renderPass, .subpass = 0 };
    VkPipeline graphicsPipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &graphicsPipeline));

    // Create Buffers (Vertex, Indirect, and Readback)
    VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeof(vertices), .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT };
    VkBuffer vertexBuffer;
    vkCreateBuffer(device, &bufferInfo, NULL, &vertexBuffer);
    vkGetBufferMemoryRequirements(device, vertexBuffer, &memReqs);
    allocInfo.allocationSize = memReqs.size; allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory vertexMemory;
    vkAllocateMemory(device, &allocInfo, NULL, &vertexMemory);
    vkBindBufferMemory(device, vertexBuffer, vertexMemory, 0);
    void* data;
    vkMapMemory(device, vertexMemory, 0, sizeof(vertices), 0, &data);
    memcpy(data, vertices, sizeof(vertices));
    vkUnmapMemory(device, vertexMemory);

    // -- THE INDIRECT DRAW SANITY CHECK --
    // We request to draw 3 vertices, starting at vertex 3 (the blue triangle).
    VkDrawIndirectCommand indirectCmd;
    indirectCmd.vertexCount = 3;
    indirectCmd.instanceCount = 1;
    indirectCmd.firstVertex = 3;  // <--- Crucial test for the driver!
    indirectCmd.firstInstance = 0;

    bufferInfo.size = sizeof(indirectCmd); bufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    VkBuffer indirectBuffer;
    vkCreateBuffer(device, &bufferInfo, NULL, &indirectBuffer);
    vkGetBufferMemoryRequirements(device, indirectBuffer, &memReqs);
    allocInfo.allocationSize = memReqs.size; allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory indirectMemory;
    vkAllocateMemory(device, &allocInfo, NULL, &indirectMemory);
    vkBindBufferMemory(device, indirectBuffer, indirectMemory, 0);
    vkMapMemory(device, indirectMemory, 0, sizeof(indirectCmd), 0, &data);
    memcpy(data, &indirectCmd, sizeof(indirectCmd));
    vkUnmapMemory(device, indirectMemory);

    // Readback Buffer (To save the image)
    VkDeviceSize imageSize = WIDTH * HEIGHT * 4;
    bufferInfo.size = imageSize; bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer readbackBuffer;
    vkCreateBuffer(device, &bufferInfo, NULL, &readbackBuffer);
    vkGetBufferMemoryRequirements(device, readbackBuffer, &memReqs);
    allocInfo.allocationSize = memReqs.size; allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory readbackMemory;
    vkAllocateMemory(device, &allocInfo, NULL, &readbackMemory);
    vkBindBufferMemory(device, readbackBuffer, readbackMemory, 0);

    // Record Command Buffer
    VkCommandBufferAllocateInfo cmdAllocInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = commandPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer commandBuffer;
    VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &commandBuffer));

    VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkRenderPassBeginInfo renderPassInfoBegin = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = renderPass, .framebuffer = framebuffer, .renderArea = { .offset = {0, 0}, .extent = {WIDTH, HEIGHT} }, .clearValueCount = 1, .pClearValues = &clearColor };
    
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfoBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, offsets);
    
    // --- INDIRECT DRAW CALL ---
    // stride must be a multiple of 4 and >= sizeof(VkDrawIndirectCommand)
    vkCmdDrawIndirect(commandBuffer, indirectBuffer, 0, 1, sizeof(VkDrawIndirectCommand));

    vkCmdEndRenderPass(commandBuffer);

    // Copy Image to Buffer
    VkBufferImageCopy region = { .bufferOffset = 0, .bufferRowLength = 0, .bufferImageHeight = 0, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageOffset = {0, 0, 0}, .imageExtent = {WIDTH, HEIGHT, 1} };
    vkCmdCopyImageToBuffer(commandBuffer, colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readbackBuffer, 1, &region);

    vkEndCommandBuffer(commandBuffer);

    // Submit and Wait
    VkSubmitInfo submitInfo = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &commandBuffer };
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    // Save Image to PPM
    vkMapMemory(device, readbackMemory, 0, imageSize, 0, &data);
    uint8_t* pixels = (uint8_t*)data;
    FILE* file = fopen("output.ppm", "wb");
    fprintf(file, "P3\n%d %d\n255\n", WIDTH, HEIGHT);
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            int index = (y * WIDTH + x) * 4;
            // Reading out standard R8G8B8A8 unorm data
            fprintf(file, "%d %d %d ", pixels[index], pixels[index+1], pixels[index+2]);
        }
        fprintf(file, "\n");
    }
    fclose(file);
    vkUnmapMemory(device, readbackMemory);

    printf("Rendered to output.ppm successfully.\n");
    printf("Sanity Check: The triangle should be BLUE. If it is RED, your driver is ignoring the firstVertex offset in vkCmdDrawIndirect.\n");

    // Cleanup (abbreviated for the single-file sample)
    vkDestroyBuffer(device, readbackBuffer, NULL);
    vkFreeMemory(device, readbackMemory, NULL);
    vkDestroyBuffer(device, indirectBuffer, NULL);
    vkFreeMemory(device, indirectMemory, NULL);
    vkDestroyBuffer(device, vertexBuffer, NULL);
    vkFreeMemory(device, vertexMemory, NULL);
    vkDestroyShaderModule(device, fragModule, NULL);
    vkDestroyShaderModule(device, vertModule, NULL);
    vkDestroyPipeline(device, graphicsPipeline, NULL);
    vkDestroyPipelineLayout(device, pipelineLayout, NULL);
    vkDestroyRenderPass(device, renderPass, NULL);
    vkDestroyImageView(device, colorImageView, NULL);
    vkDestroyImage(device, colorImage, NULL);
    vkFreeMemory(device, imageMemory, NULL);
    vkDestroyFramebuffer(device, framebuffer, NULL);
    vkDestroyCommandPool(device, commandPool, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    
    free(vertCode);
    free(fragCode);
    free(physicalDevices);
    free(queueFamilies);

    return 0;
}
