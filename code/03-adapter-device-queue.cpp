#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <webgpu/webgpu.h>

namespace {

constexpr uint64_t kWaitTimeoutNs = 5'000'000'000ULL;

std::string toString(WGPUStringView view) {
  if (view.data == nullptr) {
    return {};
  }
  if (view.length == WGPU_STRLEN) {
    return std::string(view.data);
  }
  return std::string(view.data, view.length);
}

const char *backendTypeName(WGPUBackendType type) {
  switch (type) {
  case WGPUBackendType_Null:
    return "Null";
  case WGPUBackendType_WebGPU:
    return "WebGPU";
  case WGPUBackendType_D3D11:
    return "D3D11";
  case WGPUBackendType_D3D12:
    return "D3D12";
  case WGPUBackendType_Metal:
    return "Metal";
  case WGPUBackendType_Vulkan:
    return "Vulkan";
  case WGPUBackendType_OpenGL:
    return "OpenGL";
  case WGPUBackendType_OpenGLES:
    return "OpenGLES";
  case WGPUBackendType_Undefined:
    return "Undefined";
  default:
    return "Unknown";
  }
}

const char *adapterTypeName(WGPUAdapterType type) {
  switch (type) {
  case WGPUAdapterType_DiscreteGPU:
    return "Discrete GPU";
  case WGPUAdapterType_IntegratedGPU:
    return "Integrated GPU";
  case WGPUAdapterType_CPU:
    return "CPU";
  case WGPUAdapterType_Unknown:
    return "Unknown";
  default:
    return "Unknown";
  }
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

struct DeviceRequestState {
  WGPURequestDeviceStatus status = WGPURequestDeviceStatus_Error;
  WGPUDevice device = nullptr;
  std::string message;
};

void onDeviceRequestEnded(WGPURequestDeviceStatus status, WGPUDevice device,
                          WGPUStringView message, void *userdata1,
                          void * /*userdata2*/
) {
  auto *state = static_cast<DeviceRequestState *>(userdata1);
  state->status = status;
  state->device = device;
  state->message = toString(message);
}

void onUncapturedError(WGPUDevice const * /*device*/, WGPUErrorType type,
                       WGPUStringView message, void * /*userdata1*/,
                       void * /*userdata2*/
) {
  std::cerr << "[WebGPU uncaptured error] type=" << static_cast<int>(type)
            << ": " << toString(message) << '\n';
}

void onDeviceLost(WGPUDevice const * /*device*/, WGPUDeviceLostReason reason,
                  WGPUStringView message, void * /*userdata1*/,
                  void * /*userdata2*/
) {
  // Dawn also reports normal device destruction through this callback.
  if (reason == WGPUDeviceLostReason_Destroyed) {
    return;
  }

  std::cerr << "[WebGPU device lost] reason=" << static_cast<int>(reason);

  const std::string text = toString(message);
  if (!text.empty()) {
    std::cerr << ": " << text;
  }
  std::cerr << '\n';
}

bool waitForOneFuture(WGPUInstance instance, WGPUFuture future,
                      const char *operationName) {
  WGPUFutureWaitInfo waitInfo = WGPU_FUTURE_WAIT_INFO_INIT;
  waitInfo.future = future;

  const WGPUWaitStatus waitStatus =
      wgpuInstanceWaitAny(instance, 1, &waitInfo, kWaitTimeoutNs);

  if (waitStatus == WGPUWaitStatus_TimedOut) {
    std::cerr << operationName << " timed out\n";
    return false;
  }

  if (waitStatus != WGPUWaitStatus_Success || waitInfo.completed != WGPU_TRUE) {
    std::cerr << "Failed while waiting for " << operationName << '\n';
    return false;
  }

  return true;
}

} // namespace

int main() {
  if (glfwInit() != GLFW_TRUE) {
    std::cerr << "Failed to initialize GLFW\n";
    return EXIT_FAILURE;
  }

  constexpr WGPUInstanceFeatureName requiredInstanceFeature =
      WGPUInstanceFeatureName_TimedWaitAny;

  if (wgpuHasInstanceFeature(requiredInstanceFeature) != WGPU_TRUE) {
    std::cerr << "TimedWaitAny is not supported\n";
    glfwTerminate();
    return EXIT_FAILURE;
  }

  WGPUInstanceDescriptor instanceDescriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
  instanceDescriptor.requiredFeatureCount = 1;
  instanceDescriptor.requiredFeatures = &requiredInstanceFeature;

  WGPUInstance instance = wgpuCreateInstance(&instanceDescriptor);
  if (instance == nullptr) {
    std::cerr << "Failed to create WebGPU instance\n";
    glfwTerminate();
    return EXIT_FAILURE;
  }

  AdapterRequestState adapterState;

  WGPURequestAdapterCallbackInfo adapterCallbackInfo =
      WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
  adapterCallbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
  adapterCallbackInfo.callback = onAdapterRequestEnded;
  adapterCallbackInfo.userdata1 = &adapterState;

  WGPURequestAdapterOptions adapterOptions = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
  adapterOptions.powerPreference = WGPUPowerPreference_HighPerformance;

  const WGPUFuture adapterFuture = wgpuInstanceRequestAdapter(
      instance, &adapterOptions, adapterCallbackInfo);

  if (!waitForOneFuture(instance, adapterFuture, "adapter request")) {
    wgpuInstanceRelease(instance);
    glfwTerminate();
    return EXIT_FAILURE;
  }

  if (adapterState.status != WGPURequestAdapterStatus_Success ||
      adapterState.adapter == nullptr) {
    std::cerr << "Adapter request failed";
    if (!adapterState.message.empty()) {
      std::cerr << ": " << adapterState.message;
    }
    std::cerr << '\n';
    wgpuInstanceRelease(instance);
    glfwTerminate();
    return EXIT_FAILURE;
  }

  WGPUAdapter adapter = adapterState.adapter;

  WGPUAdapterInfo adapterInfo = WGPU_ADAPTER_INFO_INIT;
  if (wgpuAdapterGetInfo(adapter, &adapterInfo) != WGPUStatus_Success) {
    std::cerr << "Failed to query adapter info\n";
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);
    glfwTerminate();
    return EXIT_FAILURE;
  }

  std::cout << "Adapter\n"
            << "  vendor:       " << toString(adapterInfo.vendor) << '\n'
            << "  architecture: " << toString(adapterInfo.architecture) << '\n'
            << "  device:       " << toString(adapterInfo.device) << '\n'
            << "  description:  " << toString(adapterInfo.description) << '\n'
            << "  backend:      " << backendTypeName(adapterInfo.backendType)
            << '\n'
            << "  type:         " << adapterTypeName(adapterInfo.adapterType)
            << '\n';

  wgpuAdapterInfoFreeMembers(adapterInfo);

  WGPUSupportedFeatures adapterFeatures = WGPU_SUPPORTED_FEATURES_INIT;
  wgpuAdapterGetFeatures(adapter, &adapterFeatures);
  std::cout << "Adapter feature count: " << adapterFeatures.featureCount
            << '\n';
  wgpuSupportedFeaturesFreeMembers(adapterFeatures);

  WGPULimits adapterLimits = WGPU_LIMITS_INIT;
  if (wgpuAdapterGetLimits(adapter, &adapterLimits) != WGPUStatus_Success) {
    std::cerr << "Failed to query adapter limits\n";
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);
    glfwTerminate();
    return EXIT_FAILURE;
  }

  std::cout << "Adapter limits\n"
            << "  maxTextureDimension2D: "
            << adapterLimits.maxTextureDimension2D << '\n'
            << "  maxBindGroups:         " << adapterLimits.maxBindGroups
            << '\n'
            << "  maxBufferSize:         " << adapterLimits.maxBufferSize
            << '\n'
            << "  maxVertexBuffers:      " << adapterLimits.maxVertexBuffers
            << '\n'
            << "  maxVertexAttributes:   " << adapterLimits.maxVertexAttributes
            << '\n';

  DeviceRequestState deviceState;

  WGPURequestDeviceCallbackInfo deviceCallbackInfo =
      WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
  deviceCallbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
  deviceCallbackInfo.callback = onDeviceRequestEnded;
  deviceCallbackInfo.userdata1 = &deviceState;

  WGPUDeviceDescriptor deviceDescriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
  deviceDescriptor.label = {"Tutorial device", WGPU_STRLEN};
  deviceDescriptor.defaultQueue.label = {"Tutorial default queue", WGPU_STRLEN};

  deviceDescriptor.uncapturedErrorCallbackInfo.callback = onUncapturedError;

  deviceDescriptor.deviceLostCallbackInfo.mode =
      WGPUCallbackMode_AllowSpontaneous;
  deviceDescriptor.deviceLostCallbackInfo.callback = onDeviceLost;

  const WGPUFuture deviceFuture =
      wgpuAdapterRequestDevice(adapter, &deviceDescriptor, deviceCallbackInfo);

  if (!waitForOneFuture(instance, deviceFuture, "device request")) {
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);
    glfwTerminate();
    return EXIT_FAILURE;
  }

  if (deviceState.status != WGPURequestDeviceStatus_Success ||
      deviceState.device == nullptr) {
    std::cerr << "Device request failed";
    if (!deviceState.message.empty()) {
      std::cerr << ": " << deviceState.message;
    }
    std::cerr << '\n';
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);
    glfwTerminate();
    return EXIT_FAILURE;
  }

  WGPUDevice device = deviceState.device;
  WGPUQueue queue = wgpuDeviceGetQueue(device);

  if (queue == nullptr) {
    std::cerr << "Failed to get the device queue\n";
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);
    glfwTerminate();
    return EXIT_FAILURE;
  }

  WGPULimits deviceLimits = WGPU_LIMITS_INIT;
  if (wgpuDeviceGetLimits(device, &deviceLimits) != WGPUStatus_Success) {
    std::cerr << "Failed to query device limits\n";
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);
    glfwTerminate();
    return EXIT_FAILURE;
  }

  WGPUSupportedFeatures deviceFeatures = WGPU_SUPPORTED_FEATURES_INIT;
  wgpuDeviceGetFeatures(device, &deviceFeatures);

  std::cout << "Device created successfully\n"
            << "Device feature count: " << deviceFeatures.featureCount << '\n'
            << "Device maxBindGroups: " << deviceLimits.maxBindGroups << '\n'
            << "Default queue acquired\n";

  wgpuSupportedFeaturesFreeMembers(deviceFeatures);

  wgpuQueueRelease(queue);
  wgpuDeviceRelease(device);
  wgpuAdapterRelease(adapter);
  wgpuInstanceRelease(instance);
  glfwTerminate();

  return EXIT_SUCCESS;
}
