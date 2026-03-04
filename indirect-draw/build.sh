glslc shader.vert -o vert.spv
glslc shader.frag -o frag.spv
gcc -o main.bin main.c -lvulkan
