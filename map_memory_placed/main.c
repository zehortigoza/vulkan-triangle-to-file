#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h> // For allocating virtual address space

#define VK_CHECK(result)                                                       \
    do {                                                                       \
        if ((result) != VK_SUCCESS) {                                          \
            fprintf(stderr, "Vulkan error at %s:%d (Error code: %d)\n",        \
                    __FILE__, __LINE__, result);                               \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    } while (0)

#define MAGIC_VALUE_OFFSET_0 0xDEADC0DE
#define MAGIC_VALUE_OFFSET_N 0x12345678

// Helper to find a suitable memory type
uint32_t find_memory_type(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    fprintf(stderr, "Failed to find suitable memory type!\n");
    exit(EXIT_FAILURE);
}

int main() {
    VkInstance instance;
    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "MapMemoryPlacedValidator",
        .apiVersion = VK_API_VERSION_1_1 // Need at least 1.1 for physical device properties 2
    };

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
    };

    VK_CHECK(vkCreateInstance(&createInfo, NULL, &instance));

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);
    if (deviceCount == 0) {
        fprintf(stderr, "No Vulkan physical devices found.\n");
        return 1;
    }

    VkPhysicalDevice* physicalDevices = malloc(sizeof(VkPhysicalDevice) * deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices);
    VkPhysicalDevice physicalDevice = physicalDevices[0]; // Just picking the first one for simplicity
    free(physicalDevices);

    // Query for placed memory alignment requirements
    VkPhysicalDeviceMapMemoryPlacedPropertiesEXT placedProps = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAP_MEMORY_PLACED_PROPERTIES_EXT,
        .pNext = NULL
    };
    VkPhysicalDeviceProperties2 props2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &placedProps
    };
    vkGetPhysicalDeviceProperties2(physicalDevice, &props2);

    VkDeviceSize alignment = placedProps.minPlacedMemoryMapAlignment;
    if (alignment == 0) alignment = 4096; // Fallback just in case
    printf("[Info] minPlacedMemoryMapAlignment: %llu bytes\n", (unsigned long long)alignment);

    // Enable required extensions and features
    VkPhysicalDeviceMapMemoryPlacedFeaturesEXT placedFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAP_MEMORY_PLACED_FEATURES_EXT,
        .memoryMapPlaced = VK_TRUE,
        .pNext = NULL
    };

    const char* deviceExtensions[] = {
        VK_KHR_MAP_MEMORY_2_EXTENSION_NAME, // Corrected macro name
        VK_EXT_MAP_MEMORY_PLACED_EXTENSION_NAME
    };

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0, // Assuming family 0 exists
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };

    VkDeviceCreateInfo deviceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &placedFeatures,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = deviceExtensions
    };

    VkDevice device;
    VK_CHECK(vkCreateDevice(physicalDevice, &deviceCreateInfo, NULL, &device));

    // Load extension functions
    PFN_vkMapMemory2KHR pfn_vkMapMemory2KHR = (PFN_vkMapMemory2KHR)vkGetDeviceProcAddr(device, "vkMapMemory2KHR");
    PFN_vkUnmapMemory2KHR pfn_vkUnmapMemory2KHR = (PFN_vkUnmapMemory2KHR)vkGetDeviceProcAddr(device, "vkUnmapMemory2KHR");

    if (!pfn_vkMapMemory2KHR || !pfn_vkUnmapMemory2KHR) {
        fprintf(stderr, "Failed to load vkMapMemory2KHR / vkUnmapMemory2KHR.\n");
        return 1;
    }

    // Allocate memory (Size = alignment * 2 to test offsets safely)
    VkDeviceSize allocSize = alignment * 2;
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = allocSize,
        .memoryTypeIndex = find_memory_type(physicalDevice, 0xFFFFFFFF, 
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };

    VkDeviceMemory memory;
    VK_CHECK(vkAllocateMemory(device, &allocInfo, NULL, &memory));

    // Reserve virtual address space using the OS
    void* placed_addr = mmap(NULL, alignment, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (placed_addr == MAP_FAILED) {
        fprintf(stderr, "mmap failed to reserve virtual address space.\n");
        return 1;
    }
    printf("[Info] Reserved virtual address space at: %p\n", placed_addr);

    // =========================================================================
    // TEST 1: Map at Offset 0
    // =========================================================================
    printf("\n--- Test 1: Placed Memory Mapping at Offset 0 ---\n");
    
    VkMemoryMapPlacedInfoEXT placedInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_MAP_PLACED_INFO_EXT,
        .pNext = NULL,
        .pPlacedAddress = placed_addr
    };

    VkMemoryMapInfoKHR mapInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_MAP_INFO_KHR,
        .pNext = &placedInfo,
        .flags = VK_MEMORY_MAP_PLACED_BIT_EXT, 
        .memory = memory,
        .offset = 0,
        .size = alignment
    };

    void* returned_ptr = NULL;

    // 1. Map memory using the driver's placed mapping implementation (added 3rd argument)
    VK_CHECK(pfn_vkMapMemory2KHR(device, &mapInfo, &returned_ptr));
    
    // Quick sanity check that it returns the pointer we asked for
    if (returned_ptr != placed_addr) {
         printf("[WARNING] vkMapMemory2KHR returned %p, expected placed address %p\n", returned_ptr, placed_addr);
    }

    // 2. Change protections so we can test the write
    mprotect(placed_addr, alignment, PROT_READ | PROT_WRITE);

    // 3. Write data to the placed address
    volatile uint32_t* mapped_data = (uint32_t*)placed_addr;
    mapped_data[0] = MAGIC_VALUE_OFFSET_0;
    
    // 4. Unmap the placed memory
    VkMemoryUnmapInfoKHR unmapInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO_KHR,
        .memory = memory
    };
    VK_CHECK(pfn_vkUnmapMemory2KHR(device, &unmapInfo));

    // 5. Verify the data using a standard, non-placed map
    VkMemoryMapInfoKHR standardMapInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_MAP_INFO_KHR,
        .pNext = NULL, // No placed info
        .memory = memory,
        .offset = 0,
        .size = allocSize
    };
    
    void* standard_ptr = NULL;
    VK_CHECK(pfn_vkMapMemory2KHR(device, &standardMapInfo, &standard_ptr));

    if (((uint32_t*)standard_ptr)[0] == MAGIC_VALUE_OFFSET_0) {
        printf("[PASS] Offset 0 test passed.\n");
    } else {
        printf("[FAIL] Expected %X, got %X\n", MAGIC_VALUE_OFFSET_0, ((uint32_t*)standard_ptr)[0]);
    }

    VK_CHECK(pfn_vkUnmapMemory2KHR(device, &unmapInfo));

    // =========================================================================
    // TEST 2: Map at Offset > 0 (using alignment)
    // =========================================================================
    printf("\n--- Test 2: Placed Memory Mapping at Offset %llu ---\n", (unsigned long long)alignment);
    
    // Reset our reserved mapping protections
    mmap(placed_addr, alignment, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

    // 1. Map placed memory with an offset
    mapInfo.offset = alignment; 
    mapInfo.size = alignment;
    returned_ptr = NULL;
    
    // Map with offset (added 3rd argument)
    VK_CHECK(pfn_vkMapMemory2KHR(device, &mapInfo, &returned_ptr));
    mprotect(placed_addr, alignment, PROT_READ | PROT_WRITE);

    printf("returned_ptr=0x%p\n", returned_ptr);
    if (returned_ptr != placed_addr) {
        printf("[WARNING] vkMapMemory2KHR returned %p, expected placed address %p\n", returned_ptr, placed_addr);
    }

    // 2. Write data to the placed address (which now represents device memory at `offset = alignment`)
    //mapped_data = (uint32_t*)placed_addr;
    mapped_data[0] = MAGIC_VALUE_OFFSET_N;
    
    // 3. Unmap the placed memory
    VK_CHECK(pfn_vkUnmapMemory2KHR(device, &unmapInfo));

    // 4. Verify via standard map
    standard_ptr = NULL;
    VK_CHECK(pfn_vkMapMemory2KHR(device, &standardMapInfo, &standard_ptr)); // Maps entire allocSize

    if (((uint32_t*)standard_ptr)[0] == MAGIC_VALUE_OFFSET_0) {
        printf("[PASS] Offset 0 test passed.\n");
    } else {
        printf("[FAIL] Expected %X, got %X\n", MAGIC_VALUE_OFFSET_0, ((uint32_t*)standard_ptr)[0]);
    }

    // Check if the data landed exactly at the byte offset `alignment`
    uint32_t* ptr_at_offset = (uint32_t*)((char*)standard_ptr + alignment);
    
    if (ptr_at_offset[0] == MAGIC_VALUE_OFFSET_N) {
        printf("[PASS] Offset %llu test passed.\n", (unsigned long long)alignment);
    } else {
        printf("[FAIL] Expected %X, got %X at offset %llu\n", MAGIC_VALUE_OFFSET_N, ptr_at_offset[0], (unsigned long long)alignment);
    }

    VK_CHECK(pfn_vkUnmapMemory2KHR(device, &unmapInfo));

    // =========================================================================
    // Cleanup
    // =========================================================================
    munmap(placed_addr, alignment);
    vkFreeMemory(device, memory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);

    printf("\n[Info] All validations completed successfully.\n");
    return 0;
}