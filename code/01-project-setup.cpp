#include <cstdlib>
#include <iostream>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <webgpu/webgpu.h>

int main() {
  if (glfwInit() != GLFW_TRUE) {
    std::cerr << "Failed to initialize GLFW\n";
    return EXIT_FAILURE;
  }

  std::cout << "GLFW: " << glfwGetVersionString() << '\n';

  WGPUInstanceDescriptor descriptor{};
  WGPUInstance instance = wgpuCreateInstance(&descriptor);

  if (instance == nullptr) {
    std::cerr << "Failed to create WebGPU instance\n";
    glfwTerminate();
    return EXIT_FAILURE;
  }

  std::cout << "Dawn/WebGPU: instance created successfully\n";

  wgpuInstanceRelease(instance);
  glfwTerminate();

  return EXIT_SUCCESS;
}
