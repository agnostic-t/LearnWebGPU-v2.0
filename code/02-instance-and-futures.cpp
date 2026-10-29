#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <webgpu/webgpu.h>

namespace {

constexpr uint64_t kAdapterWaitTimeoutNs = 5'000'000'000ULL;

std::string toString(WGPUStringView view) {
  if (view.data == nullptr) {
    return {};
  }

  if (view.length == WGPU_STRLEN) {
    return std::string(view.data);
  }

  return std::string(view.data, view.length);
}

struct AdapterRequestState {
  WGPURequestAdapterStatus status = WGPURequestAdapterStatus_Error;

  WGPUAdapter adapter = nullptr;
  std::string message;
};

void onAdapterRequestEnded(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                           WGPUStringView message, void *userdata1,
                           void * /*userdata2*/
) {
  auto *state = static_cast<AdapterRequestState *>(userdata1);

  state->status = status;
  state->adapter = adapter;
  state->message = toString(message);
}

} // namespace

int main() {
  if (glfwInit() != GLFW_TRUE) {
    std::cerr << "Failed to initialize GLFW\n";
    return EXIT_FAILURE;
  }

  std::cout << "GLFW: " << glfwGetVersionString() << '\n';

  constexpr WGPUInstanceFeatureName requiredFeature =
      WGPUInstanceFeatureName_TimedWaitAny;

  if (wgpuHasInstanceFeature(requiredFeature) != WGPU_TRUE) {
    std::cerr << "TimedWaitAny is not supported by "
                 "this WebGPU implementation\n";

    glfwTerminate();
    return EXIT_FAILURE;
  }

  WGPUInstanceLimits instanceLimits = WGPU_INSTANCE_LIMITS_INIT;

  if (wgpuGetInstanceLimits(&instanceLimits) != WGPUStatus_Success) {

    std::cerr << "Failed to query WebGPU instance limits\n";

    glfwTerminate();
    return EXIT_FAILURE;
  }

  std::cout << "TimedWaitAny max future count: "
            << instanceLimits.timedWaitAnyMaxCount << '\n';

  WGPUInstanceDescriptor instanceDescriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;

  instanceDescriptor.requiredFeatureCount = 1;
  instanceDescriptor.requiredFeatures = &requiredFeature;

  WGPUInstance instance = wgpuCreateInstance(&instanceDescriptor);

  if (instance == nullptr) {
    std::cerr << "Failed to create WebGPU instance\n";
    glfwTerminate();
    return EXIT_FAILURE;
  }

  AdapterRequestState requestState;

  WGPURequestAdapterCallbackInfo callbackInfo =
      WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;

  callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;

  callbackInfo.callback = onAdapterRequestEnded;

  callbackInfo.userdata1 = &requestState;

  WGPURequestAdapterOptions adapterOptions = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;

  WGPUFuture future =
      wgpuInstanceRequestAdapter(instance, &adapterOptions, callbackInfo);

  WGPUFutureWaitInfo waitInfo = WGPU_FUTURE_WAIT_INFO_INIT;

  waitInfo.future = future;

  const WGPUWaitStatus waitStatus =
      wgpuInstanceWaitAny(instance, 1, &waitInfo, kAdapterWaitTimeoutNs);

  if (waitStatus == WGPUWaitStatus_TimedOut) {
    std::cerr << "Adapter request timed out\n";

    wgpuInstanceRelease(instance);
    glfwTerminate();
    return EXIT_FAILURE;
  }

  if (waitStatus != WGPUWaitStatus_Success || waitInfo.completed != WGPU_TRUE) {

    std::cerr << "Failed while waiting for adapter request\n";

    wgpuInstanceRelease(instance);
    glfwTerminate();
    return EXIT_FAILURE;
  }

  if (requestState.status != WGPURequestAdapterStatus_Success ||
      requestState.adapter == nullptr) {

    std::cerr << "Adapter request failed";

    if (!requestState.message.empty()) {
      std::cerr << ": " << requestState.message;
    }

    std::cerr << '\n';

    wgpuInstanceRelease(instance);
    glfwTerminate();
    return EXIT_FAILURE;
  }

  std::cout << "Adapter request completed successfully\n";

  wgpuAdapterRelease(requestState.adapter);
  wgpuInstanceRelease(instance);
  glfwTerminate();

  return EXIT_SUCCESS;
}
