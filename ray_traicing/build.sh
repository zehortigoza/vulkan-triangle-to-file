glslangValidator -V ray_query.comp -o ray_query.spv
gcc main.c -o vulkan_test -lvulkan
