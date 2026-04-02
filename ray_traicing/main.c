#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define WIDTH  512
#define HEIGHT 512

// Ray Tracing function pointers
PFN_vkCreateAccelerationStructureKHR              p_vkCreateAccelerationStructureKHR;
PFN_vkDestroyAccelerationStructureKHR             p_vkDestroyAccelerationStructureKHR;
PFN_vkGetAccelerationStructureBuildSizesKHR       p_vkGetAccelerationStructureBuildSizesKHR;
PFN_vkCmdBuildAccelerationStructuresKHR           p_vkCmdBuildAccelerationStructuresKHR;
PFN_vkGetAccelerationStructureDeviceAddressKHR    p_vkGetAccelerationStructureDeviceAddressKHR;

// Globals
VkInstance       instance;
VkPhysicalDevice physicalDevice;
VkDevice         device;
VkQueue          queue;
uint32_t         queueFamilyIndex = 0;
VkCommandPool    commandPool;

uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    printf("Failed to find suitable memory type\n");
    exit(1);
}

void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags properties,
                  VkBuffer* buffer, VkDeviceMemory* bufferMemory) {
    VkBufferCreateInfo bufferInfo = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    if (vkCreateBuffer(device, &bufferInfo, NULL, buffer) != VK_SUCCESS) {
        printf("Failed to create buffer\n"); exit(1);
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, *buffer, &memReq);

    VkMemoryAllocateFlagsInfo allocFlags = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
    };
    VkMemoryAllocateInfo allocInfo = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext           = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
                               ? &allocFlags : NULL,
        .allocationSize  = memReq.size,
        .memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties)
    };
    if (vkAllocateMemory(device, &allocInfo, NULL, bufferMemory) != VK_SUCCESS) {
        printf("Failed to allocate buffer memory\n"); exit(1);
    }
    vkBindBufferMemory(device, *buffer, *bufferMemory, 0);
}

VkDeviceAddress getBufferDeviceAddress(VkBuffer buffer) {
    VkBufferDeviceAddressInfo info = {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer
    };
    return vkGetBufferDeviceAddress(device, &info);
}

VkCommandBuffer beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = commandPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(device, &allocInfo, &cb);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vkBeginCommandBuffer(cb, &beginInfo);
    return cb;
}

void endSingleTimeCommands(VkCommandBuffer cb) {
    vkEndCommandBuffer(cb);
    VkSubmitInfo submitInfo = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &cb
    };
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, commandPool, 1, &cb);
}

VkShaderModule createShaderModule(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) { printf("Failed to open shader file\n"); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* code = malloc(sz);
    fread(code, 1, sz, f);
    fclose(f);

    VkShaderModuleCreateInfo ci = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sz,
        .pCode    = (const uint32_t*)code
    };
    VkShaderModule mod;
    vkCreateShaderModule(device, &ci, NULL, &mod);
    free(code);
    return mod;
}

int main() {
    // 1. Create Instance
    VkApplicationInfo appInfo = {
        .sType      = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_2
    };
    const char* instanceExts[] = { "VK_KHR_get_physical_device_properties2" };
    VkInstanceCreateInfo instanceInfo = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo        = &appInfo,
        .enabledExtensionCount   = 1,
        .ppEnabledExtensionNames = instanceExts
    };
    vkCreateInstance(&instanceInfo, NULL, &instance);

    // 2. Pick Physical Device
    uint32_t deviceCount = 1;
    vkEnumeratePhysicalDevices(instance, &deviceCount, &physicalDevice);

    // 3. Create Logical Device
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCI = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queueFamilyIndex,
        .queueCount       = 1,
        .pQueuePriorities = &queuePriority
    };
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = {
        .sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
        .rayQuery = VK_TRUE
    };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures = {
        .sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .pNext                 = &rayQueryFeatures,
        .accelerationStructure = VK_TRUE
    };
    VkPhysicalDeviceBufferDeviceAddressFeatures bdaFeatures = {
        .sType               = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
        .pNext               = &asFeatures,
        .bufferDeviceAddress = VK_TRUE
    };
    const char* deviceExts[] = {
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
    };
    VkDeviceCreateInfo deviceInfo = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = &bdaFeatures,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &queueCI,
        .enabledExtensionCount   = 4,
        .ppEnabledExtensionNames = deviceExts
    };
    vkCreateDevice(physicalDevice, &deviceInfo, NULL, &device);
    vkGetDeviceQueue(device, queueFamilyIndex, 0, &queue);

    // 4. Load RT Function Pointers
    p_vkCreateAccelerationStructureKHR =
        (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR");
    p_vkDestroyAccelerationStructureKHR =
        (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR");
    p_vkGetAccelerationStructureBuildSizesKHR =
        (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR");
    p_vkCmdBuildAccelerationStructuresKHR =
        (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR");
    p_vkGetAccelerationStructureDeviceAddressKHR =
        (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR");

    // 5. Create Command Pool
    VkCommandPoolCreateInfo poolInfo = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = queueFamilyIndex
    };
    vkCreateCommandPool(device, &poolInfo, NULL, &commandPool);

    // 6. Geometry & Acceleration Structures
    float vertices[] = { 0.0f, -0.5f, 0.0f,  0.5f, 0.5f, 0.0f,  -0.5f, 0.5f, 0.0f };
    VkBuffer vertexBuffer; VkDeviceMemory vertexBufferMemory;
    createBuffer(sizeof(vertices),
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &vertexBuffer, &vertexBufferMemory);
    void* data;
    vkMapMemory(device, vertexBufferMemory, 0, sizeof(vertices), 0, &data);
    memcpy(data, vertices, sizeof(vertices));
    vkUnmapMemory(device, vertexBufferMemory);

    VkAccelerationStructureGeometryKHR geometry = {
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        // FIX (Bug 1, option B): mark as opaque so hits are committed automatically.
        // Without this, the non-opaque path requires rayQueryConfirmIntersectionEXT
        // in the shader loop; missing that causes every intersection to be discarded.
        .flags        = VK_GEOMETRY_OPAQUE_BIT_KHR,
        .geometry.triangles = {
            .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
            .vertexStride = sizeof(float) * 3,
            // FIX (Bug 4): maxVertex is the highest vertex INDEX (0-based), not count.
            // 3 vertices → indices 0,1,2 → maxVertex = 2
            .maxVertex          = 2,
            .vertexData.deviceAddress = getBufferDeviceAddress(vertexBuffer),
            .indexType          = VK_INDEX_TYPE_NONE_KHR
        }
    };

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {
        .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries   = &geometry
    };

    uint32_t maxPrimCount = 1;
    VkAccelerationStructureBuildSizesInfoKHR sizes = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };
    p_vkGetAccelerationStructureBuildSizesKHR(device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &maxPrimCount, &sizes);

    VkBuffer blasBuffer; VkDeviceMemory blasBufferMemory;
    // FIX (Bug 2): acceleration structure storage must be DEVICE_LOCAL.
    // HOST_VISIBLE-only memory (system RAM on dGPU) is not accessible by the RT
    // hardware, causing silent build failures or incorrect traversal results.
    createBuffer(sizes.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &blasBuffer, &blasBufferMemory);

    VkAccelerationStructureCreateInfoKHR createAsInfo = {
        .sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = blasBuffer,
        .size   = sizes.accelerationStructureSize,
        .type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
    };
    VkAccelerationStructureKHR blas;
    p_vkCreateAccelerationStructureKHR(device, &createAsInfo, NULL, &blas);

    VkBuffer scratchBuffer; VkDeviceMemory scratchBufferMemory;
    // FIX (Bug 3): scratch buffers for AS builds must also be DEVICE_LOCAL.
    createBuffer(sizes.buildScratchSize,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &scratchBuffer, &scratchBufferMemory);

    buildInfo.dstAccelerationStructure   = blas;
    buildInfo.scratchData.deviceAddress  = getBufferDeviceAddress(scratchBuffer);

    VkCommandBuffer cmd = beginSingleTimeCommands();
    VkAccelerationStructureBuildRangeInfoKHR rangeInfo = { .primitiveCount = 1 };
    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfos = &rangeInfo;
    p_vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfos);
    endSingleTimeCommands(cmd);

    // TLAS
    VkAccelerationStructureDeviceAddressInfoKHR blasAddrInfo = {
        .sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .accelerationStructure = blas
    };
    VkDeviceAddress blasAddress =
        p_vkGetAccelerationStructureDeviceAddressKHR(device, &blasAddrInfo);

    VkAccelerationStructureInstanceKHR instanceRef = {
        .transform                       = { .matrix = { {1,0,0,0}, {0,1,0,0}, {0,0,1,0} } },
        .mask                            = 0xFF,
        .flags                           = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
        .accelerationStructureReference  = blasAddress
    };

    VkBuffer instanceBuffer; VkDeviceMemory instanceBufferMemory;
    createBuffer(sizeof(instanceRef),
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &instanceBuffer, &instanceBufferMemory);
    vkMapMemory(device, instanceBufferMemory, 0, sizeof(instanceRef), 0, &data);
    memcpy(data, &instanceRef, sizeof(instanceRef));
    vkUnmapMemory(device, instanceBufferMemory);

    VkAccelerationStructureGeometryKHR tlasGeometry = {
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry.instances = {
            .sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .data.deviceAddress = getBufferDeviceAddress(instanceBuffer)
        }
    };

    VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo = {
        .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries   = &tlasGeometry
    };

    VkAccelerationStructureBuildSizesInfoKHR tlasSizes = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };
    p_vkGetAccelerationStructureBuildSizesKHR(device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &tlasBuildInfo, &maxPrimCount, &tlasSizes);

    VkBuffer tlasBuffer; VkDeviceMemory tlasBufferMemory;
    // FIX (Bug 2 + Bug 5): DEVICE_LOCAL memory and add SHADER_DEVICE_ADDRESS usage.
    createBuffer(tlasSizes.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &tlasBuffer, &tlasBufferMemory);

    createAsInfo.buffer = tlasBuffer;
    createAsInfo.size   = tlasSizes.accelerationStructureSize;
    createAsInfo.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    VkAccelerationStructureKHR tlas;
    p_vkCreateAccelerationStructureKHR(device, &createAsInfo, NULL, &tlas);

    VkBuffer tlasScratchBuffer; VkDeviceMemory tlasScratchBufferMemory;
    // FIX (Bug 3): DEVICE_LOCAL scratch buffer for TLAS build.
    createBuffer(tlasSizes.buildScratchSize,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &tlasScratchBuffer, &tlasScratchBufferMemory);

    tlasBuildInfo.dstAccelerationStructure  = tlas;
    tlasBuildInfo.scratchData.deviceAddress = getBufferDeviceAddress(tlasScratchBuffer);

    cmd = beginSingleTimeCommands();
    p_vkCmdBuildAccelerationStructuresKHR(cmd, 1, &tlasBuildInfo, &pRangeInfos);
    endSingleTimeCommands(cmd);

    // 7. Output Buffer
    VkBuffer outputBuffer; VkDeviceMemory outputBufferMemory;
    VkDeviceSize outputBufferSize = WIDTH * HEIGHT * 4 * sizeof(float);
    createBuffer(outputBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &outputBuffer, &outputBufferMemory);

    // 8. Descriptors & Pipeline
    VkDescriptorSetLayoutBinding bindings[2] = {
        { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
          .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT }
    };
    VkDescriptorSetLayoutCreateInfo dslInfo = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings    = bindings
    };
    VkDescriptorSetLayout dsl;
    vkCreateDescriptorSetLayout(device, &dslInfo, NULL, &dsl);

    VkDescriptorPoolSize poolSizes[2] = {
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }
    };
    VkDescriptorPoolCreateInfo descPoolInfo = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = 1,
        .poolSizeCount = 2,
        .pPoolSizes    = poolSizes
    };
    VkDescriptorPool descriptorPool;
    vkCreateDescriptorPool(device, &descPoolInfo, NULL, &descriptorPool);

    VkDescriptorSetAllocateInfo allocSetInfo = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &dsl
    };
    VkDescriptorSet descriptorSet;
    vkAllocateDescriptorSets(device, &allocSetInfo, &descriptorSet);

    VkWriteDescriptorSetAccelerationStructureKHR writeAs = {
        .sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .accelerationStructureCount = 1,
        .pAccelerationStructures    = &tlas
    };
    VkDescriptorBufferInfo bufferInfoDesc = {
        .buffer = outputBuffer, .offset = 0, .range = VK_WHOLE_SIZE
    };
    VkWriteDescriptorSet writes[2] = {
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext = &writeAs,
          .dstSet = descriptorSet, .dstBinding = 0, .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR },
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet = descriptorSet, .dstBinding = 1, .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .pBufferInfo = &bufferInfoDesc }
    };
    vkUpdateDescriptorSets(device, 2, writes, 0, NULL);

    VkPipelineLayoutCreateInfo layoutInfo = {
        .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts    = &dsl
    };
    VkPipelineLayout pipelineLayout;
    vkCreatePipelineLayout(device, &layoutInfo, NULL, &pipelineLayout);

    VkShaderModule shaderModule = createShaderModule("ray_query.spv");
    VkComputePipelineCreateInfo pipelineInfo = {
        .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout = pipelineLayout,
        .stage  = {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shaderModule,
            .pName  = "main"
        }
    };
    VkPipeline pipeline;
    vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline);

    // 9. Dispatch
    cmd = beginSingleTimeCommands();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
    vkCmdDispatch(cmd, WIDTH / 16, HEIGHT / 16, 1);
    endSingleTimeCommands(cmd);

    // 10. Save to PPM
    vkMapMemory(device, outputBufferMemory, 0, outputBufferSize, 0, &data);
    float* pixels = (float*)data;
    FILE* f = fopen("output.ppm", "wb");
    fprintf(f, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    for (int i = 0; i < WIDTH * HEIGHT; ++i) {
        fputc((unsigned char)(pixels[i * 4 + 0] * 255.0f), f);
        fputc((unsigned char)(pixels[i * 4 + 1] * 255.0f), f);
        fputc((unsigned char)(pixels[i * 4 + 2] * 255.0f), f);
    }
    fclose(f);
    vkUnmapMemory(device, outputBufferMemory);

    printf("Render complete. Output saved to output.ppm\n");
    return 0;
}

