#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_VK(res) if(res != VK_SUCCESS) { printf("Error at line %d: %d\n", __LINE__, res); exit(1); }

struct buffer_data {
    int32_t index1;
    int32_t index2;
    float multiplier_odd;
    float multiplier_even;
    float result;
};

// Helper function to find proper memory type index
uint32_t findMemoryType(VkPhysicalDeviceMemoryProperties memProps, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

int main() {
    // 1. Load the compiled SPIR-V binary from disk
    FILE *f = fopen("comp.spv", "rb");
    if (!f) {
        printf("[ERROR] Could not open comp.spv. Did you compile the compute shader?\n");
        printf("Run: glslangValidator -V shader.comp -o comp.spv\n");
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long spv_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint32_t *spv_code = malloc(spv_size);
    fread(spv_code, 1, spv_size, f);
    fclose(f);

    // 2. Initialize Vulkan Instance and Device
    VkInstance instance;
    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "SpillTestCompute", 1, "NoEngine", 1, VK_API_VERSION_1_0 };
    VkInstanceCreateInfo instInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &appInfo, 0, NULL, 0, NULL };
    CHECK_VK(vkCreateInstance(&instInfo, NULL, &instance));

    uint32_t gpuCount = 1;
    VkPhysicalDevice physicalDevice;
    vkEnumeratePhysicalDevices(instance, &gpuCount, &physicalDevice);

    uint32_t queueFamilyIndex = 0;
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, queueFamilyIndex, 1, &queuePriority };
    
    VkDevice device;
    VkDeviceCreateInfo deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &queueInfo, 0, NULL, 0, NULL, NULL };
    CHECK_VK(vkCreateDevice(physicalDevice, &deviceInfo, NULL, &device));

    VkQueue queue;
    vkGetDeviceQueue(device, queueFamilyIndex, 0, &queue);

    // 3. Setup SSBO Buffer
    VkBuffer buffer;
    VkBufferCreateInfo bufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, sizeof(struct buffer_data), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL };
    CHECK_VK(vkCreateBuffer(device, &bufInfo, NULL, &buffer));

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, buffer, &memReq);
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    VkDeviceMemory memory;
    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, memReq.size, findMemoryType(memProps, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    CHECK_VK(vkAllocateMemory(device, &allocInfo, NULL, &memory));
    vkBindBufferMemory(device, buffer, memory, 0);

    // Populate data
    struct buffer_data const_buffer_data = {
        .index1 = 15,
        .index2 = 468,
        .multiplier_odd = 4.0f,
        .multiplier_even = 2.0f,
        .result = 0.0f,
    };
    struct buffer_data *data;
    vkMapMemory(device, memory, 0, sizeof(struct buffer_data), 0, (void**)&data);
    memcpy(data, &const_buffer_data, sizeof(struct buffer_data));
    vkUnmapMemory(device, memory);

    // 4. Create Compute Pipeline & Descriptors
    VkShaderModuleCreateInfo smInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, spv_size, spv_code };
    VkShaderModule shaderModule;
    CHECK_VK(vkCreateShaderModule(device, &smInfo, NULL, &shaderModule));
    free(spv_code);

    // Note the stage flag is now VK_SHADER_STAGE_COMPUTE_BIT
    VkDescriptorSetLayoutBinding binding = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL };
    VkDescriptorSetLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 1, &binding };
    VkDescriptorSetLayout descriptorLayout;
    CHECK_VK(vkCreateDescriptorSetLayout(device, &layoutInfo, NULL, &descriptorLayout));

    VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 };
    VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 1, &poolSize };
    VkDescriptorPool descriptorPool;
    CHECK_VK(vkCreateDescriptorPool(device, &poolInfo, NULL, &descriptorPool));

    VkDescriptorSetAllocateInfo dsAllocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, descriptorPool, 1, &descriptorLayout };
    VkDescriptorSet descriptorSet;
    CHECK_VK(vkAllocateDescriptorSets(device, &dsAllocInfo, &descriptorSet));

    VkDescriptorBufferInfo bInfo = { buffer, 0, sizeof(struct buffer_data) };
    VkWriteDescriptorSet writeDS = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, descriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &bInfo, NULL };
    vkUpdateDescriptorSets(device, 1, &writeDS, 0, NULL);

    VkPipelineLayoutCreateInfo pipeLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &descriptorLayout, 0, NULL };
    VkPipelineLayout pipelineLayout;
    CHECK_VK(vkCreatePipelineLayout(device, &pipeLayoutInfo, NULL, &pipelineLayout));

    // Create the Compute Pipeline
    VkPipelineShaderStageCreateInfo shaderStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_COMPUTE_BIT, shaderModule, "main", NULL };
    
    VkComputePipelineCreateInfo pipelineInfo = {
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        NULL,
        0,
        shaderStage,
        pipelineLayout,
        VK_NULL_HANDLE,
        -1
    };
    VkPipeline pipeline;
    CHECK_VK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline));

    // 5. Record and Submit Commands
    VkCommandPoolCreateInfo poolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, 0, queueFamilyIndex };
    VkCommandPool commandPool;
    CHECK_VK(vkCreateCommandPool(device, &poolCreateInfo, NULL, &commandPool));

    VkCommandBufferAllocateInfo cbAllocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
    VkCommandBuffer cmd;
    CHECK_VK(vkAllocateCommandBuffers(device, &cbAllocInfo, &cmd));

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL };
    CHECK_VK(vkBeginCommandBuffer(cmd, &beginInfo));

    // Bind and Dispatch
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
    vkCmdDispatch(cmd, 1, 1, 1);
    
    // Memory Barrier to read back operations safely (Notice COMPUTE_SHADER_BIT instead of VERTEX_SHADER_BIT)
    VkBufferMemoryBarrier barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, NULL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, buffer, 0, sizeof(struct buffer_data) };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &barrier, 0, NULL);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cmd, 0, NULL };
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    // 6. Verify Results
    struct buffer_data* data_out;
    vkMapMemory(device, memory, 0, sizeof(struct buffer_data), 0, (void**)&data_out);

    float expected = 0;
    if (const_buffer_data.index1 % 2)
        expected += const_buffer_data.index1 * const_buffer_data.multiplier_odd;
    else
        expected += const_buffer_data.index1 * const_buffer_data.multiplier_even;

    if (const_buffer_data.index2 % 2)
        expected += const_buffer_data.index2 * const_buffer_data.multiplier_odd;
    else
        expected += const_buffer_data.index2 * const_buffer_data.multiplier_even;
    
    printf("\n--- Vulkan Scratch Buffer Result (Compute) ---\n");
    printf("GPU Computed Value: %f\n", data_out->result);
    printf("GPU Expected Value: %f\n", expected);
    if (data_out->result == expected) {
        printf("RESULT: PASS\n");
    } else {
        printf("RESULT: FAIL\n");
    }
    vkUnmapMemory(device, memory);

    return 0;
}
