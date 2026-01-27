#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define WIDTH 800
#define HEIGHT 600
#define BINDLESS_ARRAY_SIZE 10

// Helper to check results
#define CHECK_VK(f) { VkResult r = (f); if (r != VK_SUCCESS) { printf("Fatal : VkResult is %d in %s at line %d\n", r, __FILE__, __LINE__); exit(1); } }

// Helper to read binary file (SPV)
static char* readFile(const char* filename, size_t* size) {
    FILE* file = fopen(filename, "rb");
    if (!file) { fprintf(stderr, "Failed to find %s\n", filename); exit(1); }
    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    rewind(file);
    char* buffer = (char*)malloc(*size);
    fread(buffer, 1, *size, file);
    fclose(file);
    return buffer;
}

// Find memory type index
uint32_t getMemoryTypeIndex(VkPhysicalDevice phys, uint32_t typeBits, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    return -1;
}

int main() {
    // -------------------------------------------------------------------------
    // 1. Instance Setup
    // -------------------------------------------------------------------------
    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.apiVersion = VK_API_VERSION_1_2; // Need 1.2 for core Descriptor Indexing

    VkInstanceCreateInfo instInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instInfo.pApplicationInfo = &appInfo;
    
    // Enable validation layers for debugging (optional, but recommended)
    // const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    // instInfo.enabledLayerCount = 1;
    // instInfo.ppEnabledLayerNames = layers;

    VkInstance instance;
    CHECK_VK(vkCreateInstance(&instInfo, NULL, &instance));

    // -------------------------------------------------------------------------
    // 2. Physical Device & Bindless Features
    // -------------------------------------------------------------------------
    VkPhysicalDevice physDevice = VK_NULL_HANDLE;
    uint32_t count = 1;
    vkEnumeratePhysicalDevices(instance, &count, &physDevice);

    // Prepare feature structures for enabling Bindless
    VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };
    indexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
    indexingFeatures.runtimeDescriptorArray = VK_TRUE; 
    indexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

    VkDeviceCreateInfo devInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    devInfo.pNext = &indexingFeatures; // Chain features

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qInfo.queueFamilyIndex = 0; // Assuming family 0 supports graphics (simple logic)
    qInfo.queueCount = 1;
    qInfo.pQueuePriorities = &prio;
    
    devInfo.queueCreateInfoCount = 1;
    devInfo.pQueueCreateInfos = &qInfo;
    
    // Core 1.2 features (usually standard now)
    VkPhysicalDeviceFeatures deviceFeatures = {0};
    devInfo.pEnabledFeatures = &deviceFeatures;

    VkDevice device;
    CHECK_VK(vkCreateDevice(physDevice, &devInfo, NULL, &device));

    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);

    // -------------------------------------------------------------------------
    // 3. Command Pool
    // -------------------------------------------------------------------------
    VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.queueFamilyIndex = 0;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool cmdPool;
    CHECK_VK(vkCreateCommandPool(device, &poolInfo, NULL, &cmdPool));

    // -------------------------------------------------------------------------
    // 4. Render Target Resources (Image to render into)
    // -------------------------------------------------------------------------
    VkImage renderImage;
    VkDeviceMemory renderImageMem;
    
    VkImageCreateInfo imgInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.extent.width = WIDTH;
    imgInfo.extent.height = HEIGHT;
    imgInfo.extent.depth = 1;
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    CHECK_VK(vkCreateImage(device, &imgInfo, NULL, &renderImage));
    
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device, renderImage, &memReq);
    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = getMemoryTypeIndex(physDevice, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    CHECK_VK(vkAllocateMemory(device, &allocInfo, NULL, &renderImageMem));
    CHECK_VK(vkBindImageMemory(device, renderImage, renderImageMem, 0));

    VkImageView renderImageView;
    VkImageViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = renderImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    CHECK_VK(vkCreateImageView(device, &viewInfo, NULL, &renderImageView));

    // -------------------------------------------------------------------------
    // 5. Create a "Dummy" Texture (FIXED)
    // -------------------------------------------------------------------------
    VkImage texImage;
    VkDeviceMemory texMem;
    imgInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT; // Usage
    imgInfo.extent.width = 1;
    imgInfo.extent.height = 1;
    imgInfo.tiling = VK_IMAGE_TILING_LINEAR; // Use Linear for direct CPU writing (simplification)
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED; 
    CHECK_VK(vkCreateImage(device, &imgInfo, NULL, &texImage));
    
    vkGetImageMemoryRequirements(device, texImage, &memReq);
    
    // KEY FIX: Use HOST_VISIBLE memory so we can write to it directly
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = getMemoryTypeIndex(physDevice, memReq.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CHECK_VK(vkAllocateMemory(device, &allocInfo, NULL, &texMem));
    CHECK_VK(vkBindImageMemory(device, texImage, texMem, 0));

    // KEY FIX: Write White Pixel (0xFFFFFFFF) directly to memory
    uint32_t* texData;
    vkMapMemory(device, texMem, 0, VK_WHOLE_SIZE, 0, (void**)&texData);
    // Determine layout of the pixel (assuming standard layout for simplicity in linear tiling)
    // For a robust app, you check subresourceLayout, but for 1x1 linear, this usually works:
    texData[0] = 0xFFFFFFFF; // White Color (RGBA)
    vkUnmapMemory(device, texMem);

    VkImageView texView;
    viewInfo.image = texImage;
    CHECK_VK(vkCreateImageView(device, &viewInfo, NULL, &texView));

    VkSampler sampler;
    VkSamplerCreateInfo sampInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampInfo.magFilter = VK_FILTER_NEAREST;
    sampInfo.minFilter = VK_FILTER_NEAREST;
    CHECK_VK(vkCreateSampler(device, &sampInfo, NULL, &sampler));

    // Transition Texture Layout manually (Preinitialized -> Shader Read)
    VkCommandBufferAllocateInfo cmdAlloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdAlloc.commandPool = cmdPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cmdAlloc, &cmd);

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED; // Changed from UNDEFINED to preserve data
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT; // We just wrote from host
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                         0, 0, NULL, 0, NULL, 1, &barrier);
    vkEndCommandBuffer(cmd);
    
    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
    vkDeviceWaitIdle(device);

    // -------------------------------------------------------------------------
    // 6. Bindless Descriptors Setup
    // -------------------------------------------------------------------------
    
    // Binding Flags
    VkDescriptorBindingFlags bindFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
    flagsInfo.bindingCount = 1;
    flagsInfo.pBindingFlags = &bindFlags;

    // Layout Binding
    VkDescriptorSetLayoutBinding dslBinding = {0};
    dslBinding.binding = 0;
    dslBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    dslBinding.descriptorCount = BINDLESS_ARRAY_SIZE;
    dslBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Set Layout
    VkDescriptorSetLayoutCreateInfo dslInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dslInfo.pNext = &flagsInfo;
    dslInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    dslInfo.bindingCount = 1;
    dslInfo.pBindings = &dslBinding;
    
    VkDescriptorSetLayout dsLayout;
    CHECK_VK(vkCreateDescriptorSetLayout(device, &dslInfo, NULL, &dsLayout));

    // Pool
    VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, BINDLESS_ARRAY_SIZE };
    VkDescriptorPoolCreateInfo poolCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolCreateInfo.maxSets = 1;
    poolCreateInfo.poolSizeCount = 1;
    poolCreateInfo.pPoolSizes = &poolSize;
    
    VkDescriptorPool descPool;
    CHECK_VK(vkCreateDescriptorPool(device, &poolCreateInfo, NULL, &descPool));

    // Allocate Set
    VkDescriptorSetAllocateInfo dsAlloc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsAlloc.descriptorPool = descPool;
    dsAlloc.descriptorSetCount = 1;
    dsAlloc.pSetLayouts = &dsLayout;
    
    VkDescriptorSet descSet;
    CHECK_VK(vkAllocateDescriptorSets(device, &dsAlloc, &descSet));

    // Update Descriptor Set (Populate index 2 with our texture)
    // We intentionally skip 0 and 1 to prove "Partially Bound" works.
    VkDescriptorImageInfo descImageInfo = {0};
    descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descImageInfo.imageView = texView;
    descImageInfo.sampler = sampler;

    VkWriteDescriptorSet writeDesc = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    writeDesc.dstSet = descSet;
    writeDesc.dstBinding = 0;
    writeDesc.dstArrayElement = 2; // WE PUT TEXTURE AT INDEX 2
    writeDesc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDesc.descriptorCount = 1;
    writeDesc.pImageInfo = &descImageInfo;

    vkUpdateDescriptorSets(device, 1, &writeDesc, 0, NULL);

    // -------------------------------------------------------------------------
    // 7. Pipeline Setup
    // -------------------------------------------------------------------------
    size_t vertSize, fragSize;
    char* vertCode = readFile("bindless.vert.spv", &vertSize);
    char* fragCode = readFile("bindless.frag.spv", &fragSize);

    VkShaderModule vertMod, fragMod;
    VkShaderModuleCreateInfo shInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    shInfo.codeSize = vertSize; shInfo.pCode = (uint32_t*)vertCode;
    vkCreateShaderModule(device, &shInfo, NULL, &vertMod);
    shInfo.codeSize = fragSize; shInfo.pCode = (uint32_t*)fragCode;
    vkCreateShaderModule(device, &shInfo, NULL, &fragMod);

    // Render Pass
    VkAttachmentDescription attDesc = {0};
    attDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
    attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attDesc.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; // Ready for copy

    VkAttachmentReference attRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &attRef;

    VkRenderPassCreateInfo rpInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &attDesc;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;

    VkRenderPass renderPass;
    CHECK_VK(vkCreateRenderPass(device, &rpInfo, NULL, &renderPass));

    // Pipeline Layout (Push Constants + Set Layout)
    VkPushConstantRange pushRange = { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int) };
    VkPipelineLayoutCreateInfo plInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &dsLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pushRange;

    VkPipelineLayout pipelineLayout;
    CHECK_VK(vkCreatePipelineLayout(device, &plInfo, NULL, &pipelineLayout));

    // Pipeline
    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertInput = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo inputAsm = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    VkViewport viewport = { 0.0f, 0.0f, (float)WIDTH, (float)HEIGHT, 0.0f, 1.0f };
    VkRect2D scissor = { {0, 0}, {WIDTH, HEIGHT} };
    VkPipelineViewportStateCreateInfo viewState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewState.viewportCount = 1; viewState.pViewports = &viewport;
    viewState.scissorCount = 1; viewState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rast = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rast.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAtt = {0};
    colorBlendAtt.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo colorBlend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorBlendAtt;

    VkGraphicsPipelineCreateInfo gpInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gpInfo.stageCount = 2;
    gpInfo.pStages = stages;
    gpInfo.pVertexInputState = &vertInput;
    gpInfo.pInputAssemblyState = &inputAsm;
    gpInfo.pViewportState = &viewState;
    gpInfo.pRasterizationState = &rast;
    gpInfo.pMultisampleState = &multisample;
    gpInfo.pColorBlendState = &colorBlend;
    gpInfo.layout = pipelineLayout;
    gpInfo.renderPass = renderPass;

    VkPipeline pipeline;
    CHECK_VK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpInfo, NULL, &pipeline));

    // Framebuffer
    VkFramebufferCreateInfo fbInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fbInfo.renderPass = renderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &renderImageView;
    fbInfo.width = WIDTH;
    fbInfo.height = HEIGHT;
    fbInfo.layers = 1;

    VkFramebuffer framebuffer;
    CHECK_VK(vkCreateFramebuffer(device, &fbInfo, NULL, &framebuffer));

    // -------------------------------------------------------------------------
    // 8. Rendering
    // -------------------------------------------------------------------------
    vkResetCommandPool(device, cmdPool, 0);
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkRenderPassBeginInfo rpBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpBegin.renderPass = renderPass;
    rpBegin.framebuffer = framebuffer;
    rpBegin.renderArea = scissor;
    VkClearValue clearColor = {{{0.2f, 0.2f, 0.2f, 1.0f}}};
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearColor;

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    
    // Bind the global set
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descSet, 0, NULL);
    
    // Push Constant: Tell shader to use texture at index 2
    int texIndex = 2;
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int), &texIndex);
    
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    vkEndCommandBuffer(cmd);
    vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
    vkDeviceWaitIdle(device);

    // -------------------------------------------------------------------------
    // 9. Save to Disk (Copy Image to Host Visible Buffer)
    // -------------------------------------------------------------------------
    VkBuffer outBuffer;
    VkDeviceMemory outBufferMem;
    VkBufferCreateInfo bufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufInfo.size = WIDTH * HEIGHT * 4;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    CHECK_VK(vkCreateBuffer(device, &bufInfo, NULL, &outBuffer));
    
    VkMemoryRequirements bufReq;
    vkGetBufferMemoryRequirements(device, outBuffer, &bufReq);
    allocInfo.allocationSize = bufReq.size;
    allocInfo.memoryTypeIndex = getMemoryTypeIndex(physDevice, bufReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CHECK_VK(vkAllocateMemory(device, &allocInfo, NULL, &outBufferMem));
    CHECK_VK(vkBindBufferMemory(device, outBuffer, outBufferMem, 0));

    // Command to Copy Image -> Buffer
    vkBeginCommandBuffer(cmd, &beginInfo);
    VkBufferImageCopy copyRegion = {0};
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent.width = WIDTH;
    copyRegion.imageExtent.height = HEIGHT;
    copyRegion.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, renderImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, outBuffer, 1, &copyRegion);
    vkEndCommandBuffer(cmd);
    vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
    vkDeviceWaitIdle(device);

    // Map and Write to PPM
    void* data;
    vkMapMemory(device, outBufferMem, 0, VK_WHOLE_SIZE, 0, &data);
    uint8_t* pixels = (uint8_t*)data;

    FILE* fout = fopen("output_bindless.ppm", "wb");
    fprintf(fout, "P3\n%d %d\n255\n", WIDTH, HEIGHT);
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        // Vulkan is usually BGRA or RGBA depending on format, R8G8B8A8 was requested
        // PPM needs R G B integers
        fprintf(fout, "%d %d %d ", pixels[i*4 + 0], pixels[i*4 + 1], pixels[i*4 + 2]);
    }
    fclose(fout);
    vkUnmapMemory(device, outBufferMem);
    printf("Render saved to output_bindless.ppm\n");

    // Cleanup (Simplified for brevity - OS will reclaim on exit)
    vkDestroyImageView(device, renderImageView, NULL);
    vkDestroyImage(device, renderImage, NULL);
    vkFreeMemory(device, renderImageMem, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    free(vertCode);
    free(fragCode);

    return 0;
}