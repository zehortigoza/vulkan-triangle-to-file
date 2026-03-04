glslangValidator -V shader.vert -o vert.spv
glslangValidator -V shader.frag -o frag.spv

gcc -o main.bin main.c -lvulkan
