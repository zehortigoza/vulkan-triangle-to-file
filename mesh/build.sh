glslc --target-env=vulkan1.3 triangle.mesh -o mesh.spv
glslc --target-env=vulkan1.3 triangle.frag -o frag.spv
gcc main.c -o main.bin -lvulkan
