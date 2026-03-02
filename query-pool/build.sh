glslc triangle.vert -o vert.spv
glslc triangle.frag -o frag.spv
gcc main.c -o main.bin -lvulkan
./main.bin
