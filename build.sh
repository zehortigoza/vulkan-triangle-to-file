rm triangle.vert.spv
rm triangle.frag.spv
rm output.ppm

glslangValidator -V triangle.vert -o triangle.vert.spv
glslangValidator -V triangle.frag -o triangle.frag.spv
glslangValidator -V check.comp -o check.comp.spv

gcc -o main.bin main.c -lvulkan

./main.bin
eog output.ppm &