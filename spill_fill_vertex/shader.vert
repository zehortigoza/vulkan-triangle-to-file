#version 450
layout(std430, binding = 0) buffer Data {
    int index1;
    int index2;
    float multiplier_odd;
    float multiplier_even;
    float result;
};
void main() {
    float my_array[512];
    for (int i = 0; i < 512; i += 2) {
        my_array[i] = float(i) * multiplier_even;
        my_array[i + 1] = float(i + 1) * multiplier_odd;
    }
    result = my_array[index1] + my_array[index2];
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
