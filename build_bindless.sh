rm bindless.vert.spv
rm bindless.frag.spv
rm output_bindless.ppm

glslangValidator -V bindless.vert -o bindless.vert.spv
glslangValidator -V bindless.frag -o bindless.frag.spv

gcc -o bindless.bin bindless.c -lvulkan

./bindless.bin
eog output_bindless.ppm &