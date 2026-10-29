# Глава 03 — Adapter, Device и Queue: от найденного GPU к рабочему контексту

> Проверено: 30 октября 2026  
> **Dawn:** `40d98d8812bb3211592bb78cac8bd7b13b585e19`  
> **GLFW:** 3.6.0  
> **GCC:** 14.2.0  
> **CMake:** 3.31.6  
> **C++:** C++20  
> **Платформа проверки:** Linux amd64, GLFW/X11  
> **Основной стиль:** WebGPU C API в C++ коде

В прошлой главе мы дошли до `WGPUAdapter`.

Это важный момент: WebGPU уже нашёл графическую реализацию, но **мы всё ещё не можем создавать через Adapter буферы, текстуры, pipelines или отправлять команды GPU**.

Почему?

Потому что Adapter — это ещё не рабочее подключение приложения к GPU. Он скорее отвечает на вопрос:

> Что на этой машине доступно и что из этого я могу попросить?

А для реальной работы нужен другой объект — `WGPUDevice`.

В этой главе мы построим следующую часть модели WebGPU:

```text
Instance
   │
   └── ищет
        │
        ▼
     Adapter
        │
        │ выбираем нужные capabilities
        ▼
      Device
        │
        ├── создаёт GPU-ресурсы
        │
        └── владеет default Queue
                    │
                    ▼
              отправка работы GPU
```

К концу главы программа:

- получит сведения о выбранном Adapter;
- посмотрит его features и limits;
- запросит Device;
- установит обработчики ошибок;
- получит default Queue;
- выведет реальные capabilities Device;
- корректно освободит все handles.

Главная цель — понять **зачем WebGPU разделяет Adapter, Device и Queue**, а не просто запомнить три типа.

---

## 1. Почему Adapter — это ещё не «GPU, которым мы пользуемся»

После `RequestAdapter()` легко представить объект так:

```text
WGPUAdapter == GPU
```

Но это неточная модель.

Adapter лучше воспринимать как **описание доступной WebGPU-конфигурации**.

Через него можно спросить:

```text
какой backend выбран?
какой тип adapter?
какие optional features доступны?
какие максимальные limits доступны?
```

Например, один adapter может сообщить:

```text
maxTextureDimension2D = 16384
maxBindGroups         = 8
TimestampQuery        = supported
TextureCompressionBC  = supported
```

Но это ещё не означает, что наше приложение должно получить Device со всеми этими возможностями.

WebGPU заставляет приложение сделать второй шаг:

```text
Adapter capabilities
        │
        │ requestDevice(...)
        ▼
Device capabilities
```

Это осознанное разделение.

---

## 2. Зачем между Adapter и Device есть этап переговоров

Представим две машины.

На первой GPU умеет:

```text
feature A
feature B
feature C
maxBindGroups = 8
```

На второй:

```text
feature A
maxBindGroups = 4
```

Если приложение автоматически получало бы **всё, что умеет текущий GPU**, код легко начинал бы случайно зависеть от возможностей машины разработчика.

Например:

```text
у меня работает с 8 bind groups
           ↓
я незаметно начал использовать 6
           ↓
на другой машине максимум 4
           ↓
приложение ломается
```

WebGPU предлагает другую модель:

> Сначала посмотри, что Adapter способен дать. Затем явно попроси у Device только то, что приложению действительно необходимо.

Поэтому Device — это не просто копия Adapter.

Это **логическое устройство с конкретным набором разрешённых capabilities**.

Именно по capabilities Device WebGPU потом валидирует наши команды.

---

## 3. Features и Limits — два вида возможностей

WebGPU разделяет capabilities на две большие группы.

### Feature

Feature — дискретная возможность: либо она есть, либо её нет.

Например:

```text
TimestampQuery
TextureCompressionBC
ShaderF16
DepthClipControl
```

В C API это значения `WGPUFeatureName`.

Проверить конкретную feature можно так:

```cpp
if (wgpuAdapterHasFeature(
        adapter,
        WGPUFeatureName_TimestampQuery
    ) == WGPU_TRUE) {
    // Adapter умеет timestamp queries.
}
```

### Limit

Limit — числовое ограничение.

Например:

```text
maxTextureDimension2D
maxBindGroups
maxBufferSize
maxVertexBuffers
maxVertexAttributes
```

То есть feature отвечает:

> Можно ли вообще делать X?

А limit:

> В каком максимальном объёме можно делать X?

---

## 4. Почему не стоит запрашивать «всё, что поддерживает Adapter»

Интуитивная стратегия могла бы выглядеть так:

```text
Adapter поддерживает 65 features
        ↓
попросим Device включить все 65
```

Для обычного приложения это плохая идея.

Device должен описывать **реальные требования программы**, а не возможности конкретной машины.

Если наш первый куб не требует optional feature, лучше запросить:

```cpp
deviceDescriptor.requiredFeatureCount = 0;
deviceDescriptor.requiredFeatures = nullptr;
```

А не превращать случайные возможности GPU пользователя в обязательные системные требования.

То же относится к limits.

Если приложению достаточно стандартного `maxBindGroups`, нет смысла требовать более высокое значение только потому, что Adapter его поддерживает.

Практическое правило:

> Запрашиваем capability только тогда, когда код действительно собирается её использовать.

---

## 5. Сначала посмотрим, какой Adapter нам достался

В прошлой главе мы получили Adapter и сразу освободили его.

Теперь сохраним его:

```cpp
WGPUAdapter adapter = adapterState.adapter;
```

И запросим информацию:

```cpp
WGPUAdapterInfo adapterInfo = WGPU_ADAPTER_INFO_INIT;

if (wgpuAdapterGetInfo(adapter, &adapterInfo) !=
    WGPUStatus_Success) {
    // ошибка
}
```

`WGPUAdapterInfo` содержит диагностические сведения:

```text
vendor
architecture
device
description
backendType
adapterType
vendorID
deviceID
```

Например, на обычной системе можно увидеть backend:

```text
Vulkan
D3D12
Metal
```

В моей виртуальной среде физического GPU нет, поэтому Dawn после неудачной попытки Vulkan выбирает свой `Null` backend.

Это нормально для проверки API, хотя настоящий rendering hardware он, конечно, не заменяет.

---

## 6. Backend и Adapter — это не одно и то же

Здесь полезно разделить ещё два понятия.

GPU — это аппаратная часть:

```text
AMD Radeon
NVIDIA GeForce
Intel Arc
Apple GPU
```

Backend — native graphics API, через который Dawn работает с системой:

```text
Vulkan
D3D12
Metal
OpenGL
```

Dawn находится между нашим WebGPU-кодом и backend:

```text
наш код
   │
   ▼
WebGPU API
   │
   ▼
Dawn
   │
   ├── Vulkan
   ├── D3D12
   ├── Metal
   └── OpenGL
        │
        ▼
       GPU
```

Поэтому строка:

```cpp
adapterInfo.backendType
```

не отвечает «какой у меня GPU?». Она отвечает:

> Через какой backend Dawn реализует этот Adapter?

---

## 7. `powerPreference` — пожелание, а не команда

При запросе Adapter мы теперь добавим:

```cpp
adapterOptions.powerPreference =
    WGPUPowerPreference_HighPerformance;
```

На ноутбуке с integrated и discrete GPU это может помочь implementation предпочесть более производительный вариант.

Но важно слово **preference**.

Это не жёсткое требование:

```text
HighPerformance != обязательно discrete GPU
```

Если подходящего discrete GPU нет, implementation всё равно может вернуть другой Adapter.

Для редактора, игры или 3D-приложения `HighPerformance` часто разумно. Для энергосберегающего приложения можно предпочесть `LowPower`.

В реальном движке это обычно становится настройкой приложения, а не константой, разбросанной по коду.

---

## 8. Важная оговорка про `compatibleSurface`

В `WGPURequestAdapterOptions` есть поле:

```cpp
compatibleSurface
```

Оно позволяет сказать:

> Выбери Adapter, который способен выводить изображение на этот Surface.

Для настоящего оконного renderer это важный критерий.

Но Surface у нас появится только в следующей главе, поэтому сейчас:

```cpp
adapterOptions.compatibleSurface = nullptr;
```

То есть текущая глава — учебная промежуточная точка.

После появления окна и Surface правильная архитектура станет такой:

```text
GLFW window
     │
     ▼
WGPUSurface
     │
     ▼
RequestAdapter(compatibleSurface = surface)
     │
     ▼
Adapter
     │
     ▼
Device
```

В главе 04 мы **переставим порядок инициализации**, чтобы финальный renderer выбирал adapter с учётом surface.

Это лучше, чем оставить скрытое архитектурное ограничение ради удобства учебного порядка.

---

## 9. Получаем список features Adapter

Для полного списка есть:

```cpp
WGPUSupportedFeatures adapterFeatures =
    WGPU_SUPPORTED_FEATURES_INIT;

wgpuAdapterGetFeatures(
    adapter,
    &adapterFeatures
);
```

После вызова структура содержит:

```cpp
adapterFeatures.featureCount
adapterFeatures.features
```

В проверенной среде получилось:

```text
Adapter feature count: 65
```

Это не означает, что Device автоматически получит все 65.

Именно это мы скоро увидим на практике.

У структуры есть выделенные implementation данные, поэтому после использования вызываем:

```cpp
wgpuSupportedFeaturesFreeMembers(adapterFeatures);
```

Это ещё один пример того, почему в C API lifetime виден явно.

---

## 10. Смотрим limits Adapter

Создаём структуру:

```cpp
WGPULimits adapterLimits = WGPU_LIMITS_INIT;
```

и заполняем:

```cpp
if (wgpuAdapterGetLimits(
        adapter,
        &adapterLimits
    ) != WGPUStatus_Success) {
    // ошибка
}
```

Для начала выведем только несколько значений:

```cpp
std::cout
    << adapterLimits.maxTextureDimension2D << '\n'
    << adapterLimits.maxBindGroups << '\n'
    << adapterLimits.maxBufferSize << '\n'
    << adapterLimits.maxVertexBuffers << '\n'
    << adapterLimits.maxVertexAttributes << '\n';
```

Не нужно сейчас запоминать числа.

Важно понять назначение:

```text
Adapter limits
     │
     └── максимальные capabilities,
         которые можно учитывать при requestDevice
```

Позже, когда появятся bind groups и vertex buffers, эти limits перестанут быть абстрактными числами.

---

## 11. Теперь зачем нужен Device

До этого Adapter был каталогом доступных возможностей.

Теперь приложение готово создать **рабочий логический контекст**:

```text
Adapter
   │
   │ requestDevice
   ▼
Device
```

`WGPUDevice` станет центральным объектом renderer.

Через него позже будем создавать:

```text
buffers
textures
shader modules
bind group layouts
bind groups
pipeline layouts
render pipelines
command encoders
```

То есть Adapter в основном участвует в выборе и negotiation, а Device — в повседневной работе приложения.

---

## 12. `WGPUDeviceDescriptor` — контракт при создании Device

Создадим descriptor:

```cpp
WGPUDeviceDescriptor deviceDescriptor =
    WGPU_DEVICE_DESCRIPTOR_INIT;
```

В нём нас сейчас интересуют четыре группы полей:

```text
label
requiredFeatures
requiredLimits
defaultQueue
callbacks диагностики
```

Добавим label:

```cpp
deviceDescriptor.label = {
    "Tutorial device",
    WGPU_STRLEN
};
```

Label не влияет на rendering. Он нужен человеку: validation messages, capture/debug tools и diagnostics становятся понятнее, если ресурсы имеют имена.

---

## 13. Почему сейчас мы не запрашиваем optional features

Для первого куба нам пока не требуется ничего дополнительного.

Поэтому defaults descriptor подходят:

```cpp
requiredFeatureCount = 0
requiredFeatures = nullptr
requiredLimits = nullptr
```

Это не «мы забыли заполнить descriptor».

Это осознанный контракт:

> Дай нам обычный Device с базовыми возможностями. Когда появится настоящая необходимость, расширим требования.

Позже это может выглядеть, например, так:

```cpp
if (wgpuAdapterHasFeature(
        adapter,
        WGPUFeatureName_TimestampQuery
    ) == WGPU_TRUE) {
    // Можно решить, нужен ли TimestampQuery приложению.
}
```

Сначала проверяем поддержку, затем только при необходимости добавляем feature в `requiredFeatures`.

---

## 14. RequestDevice снова асинхронный — и теперь это уже знакомо

Создание Device использует ту же модель, которую мы разобрали в главе 02:

```text
запуск операции
      │
      ├── WGPUFuture
      │
      └── callback с результатом
```

Создадим state:

```cpp
struct DeviceRequestState {
    WGPURequestDeviceStatus status =
        WGPURequestDeviceStatus_Error;

    WGPUDevice device = nullptr;
    std::string message;
};
```

Callback:

```cpp
void onDeviceRequestEnded(
    WGPURequestDeviceStatus status,
    WGPUDevice device,
    WGPUStringView message,
    void* userdata1,
    void*
) {
    auto* state =
        static_cast<DeviceRequestState*>(userdata1);

    state->status = status;
    state->device = device;
    state->message = toString(message);
}
```

И callback info:

```cpp
WGPURequestDeviceCallbackInfo callbackInfo =
    WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;

callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
callbackInfo.callback = onDeviceRequestEnded;
callbackInfo.userdata1 = &deviceState;
```

Теперь код уже выглядит знакомо, потому что мы понимаем модель, а не заучиваем конкретную функцию.

---

## 15. Запускаем запрос Device

```cpp
WGPUFuture deviceFuture =
    wgpuAdapterRequestDevice(
        adapter,
        &deviceDescriptor,
        callbackInfo
    );
```

И ждём его тем же `wgpuInstanceWaitAny()`.

Чтобы не копировать один и тот же boilerplate, в этой главе вынесем ожидание одного future в helper:

```cpp
bool waitForOneFuture(
    WGPUInstance instance,
    WGPUFuture future,
    const char* operationName
);
```

Это хороший пример helper-функции, которая **не скрывает WebGPU**.

Мы уже знаем, что внутри находятся:

```text
WGPUFutureWaitInfo
wgpuInstanceWaitAny
WGPUWaitStatus
completed
```

Helper лишь убирает повторение уже понятой механики.

---

## 16. Ошибки Device бывают не только во время создания

Допустим `RequestDevice` успешно завершился.

Это не означает, что больше ошибок быть не может.

Renderer будет жить долго и делать тысячи WebGPU calls. Поэтому Device имеет два особенно важных диагностических канала.

### Uncaptured error

Например, мы создали resource или command с некорректными параметрами, и ошибка не была перехвачена error scope.

Для этого задаём:

```cpp
deviceDescriptor.uncapturedErrorCallbackInfo.callback =
    onUncapturedError;
```

Наш callback пока просто выводит сообщение:

```cpp
void onUncapturedError(
    WGPUDevice const*,
    WGPUErrorType type,
    WGPUStringView message,
    void*,
    void*
) {
    std::cerr
        << "[WebGPU uncaptured error] type="
        << static_cast<int>(type)
        << ": "
        << toString(message)
        << '\n';
}
```

Игнорировать этот callback во время разработки — плохая идея. WebGPU активно валидирует API, и именно здесь часто появляется наиболее полезное объяснение ошибки.

---

## 17. Device lost — отдельный класс проблемы

GPU device может стать недоступным уже после успешного создания.

Причины могут быть разными:

```text
driver/GPU reset
системная ошибка
устройство стало недоступно
явное уничтожение Device
```

Поэтому есть device-lost callback:

```cpp
deviceDescriptor.deviceLostCallbackInfo.mode =
    WGPUCallbackMode_AllowSpontaneous;

deviceDescriptor.deviceLostCallbackInfo.callback =
    onDeviceLost;
```

Для long-lived renderer это не просто debugging nicety. В серьёзном приложении device loss означает, что renderer должен уметь остановиться и, возможно, полностью переинициализировать GPU state.

В нашей учебной версии мы только логируем проблему.

Есть одна тонкость Dawn: нормальное уничтожение Device тоже может доставить callback с причиной:

```cpp
WGPUDeviceLostReason_Destroyed
```

Поэтому наш callback считает этот случай ожидаемым и ничего не печатает:

```cpp
if (reason == WGPUDeviceLostReason_Destroyed) {
    return;
}
```

---

## 18. Что такое Queue и почему она не объединена с Device

После создания Device получаем:

```cpp
WGPUQueue queue = wgpuDeviceGetQueue(device);
```

Что такое Queue?

Очень упрощённо Device отвечает за **создание и описание работы**, а Queue — за **передачу готовой работы на выполнение**.

В следующих главах путь станет таким:

```text
Device
  │
  ├── createBuffer()
  ├── createTexture()
  ├── createRenderPipeline()
  │
  └── createCommandEncoder()
              │
              ▼
         CommandBuffer
              │
              ▼
            Queue
              │
              ▼
             GPU
```

Позже увидим вызов:

```cpp
wgpuQueueSubmit(...);
```

Queue также умеет загружать данные в GPU resources, например через `wgpuQueueWriteBuffer()`.

Но Queue — **не отдельный поток C++** и не command buffer.

Это объект WebGPU, представляющий последовательность отправки работы Device.

---

## 19. Почему у Device есть default Queue

В `WGPUDeviceDescriptor` есть:

```cpp
defaultQueue
```

Мы можем хотя бы дать ей label:

```cpp
deviceDescriptor.defaultQueue.label = {
    "Tutorial default queue",
    WGPU_STRLEN
};
```

После успешного создания Device получаем именно эту queue:

```cpp
WGPUQueue queue = wgpuDeviceGetQueue(device);
```

Для нашего renderer одной default Queue достаточно.

Поэтому базовая модель приложения будет:

```text
1 Device
   │
   └── 1 default Queue
```

---

## 20. Сравниваем capabilities Adapter и Device

После создания Device снова запросим limits:

```cpp
WGPULimits deviceLimits = WGPU_LIMITS_INIT;

wgpuDeviceGetLimits(
    device,
    &deviceLimits
);
```

И features:

```cpp
WGPUSupportedFeatures deviceFeatures =
    WGPU_SUPPORTED_FEATURES_INIT;

wgpuDeviceGetFeatures(
    device,
    &deviceFeatures
);
```

На моей проверочной конфигурации получилось:

```text
Adapter feature count: 65
Device feature count: 1
```

Это очень наглядно показывает модель:

```text
Adapter
  = что implementation способна предложить

Device
  = что данному logical device реально разрешено использовать
```

Не следует писать renderer, проверяя только Adapter и затем предполагая, что всё это автоматически доступно Device.

После создания Device источником истины для текущего рабочего контекста становится именно Device.

---

## 21. Почему Device limits могут быть «хуже» Adapter limits

Это не баг.

Adapter сообщает доступный максимум, а Device создаётся с конкретным capability contract.

WebGPU затем валидирует работу относительно **Device limits**, а не скрытых реальных возможностей железа.

Это повышает переносимость.

Например, если Device имеет:

```text
maxBindGroups = 4
```

то renderer не должен использовать 5 bind groups только потому, что Adapter или железо физически могли бы больше.

Если приложению действительно нужно 6, правильный путь:

```text
1. проверить adapterLimits.maxBindGroups >= 6
2. запросить required limit = 6
3. проверить успешность RequestDevice
4. писать renderer под этот контракт
```

А не просто надеяться на GPU разработчика.

---

## 22. Полный `main.cpp`

`CMakeLists.txt` менять не нужно.

Заменяем `src/main.cpp` на:

```cpp
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

const char* backendTypeName(WGPUBackendType type) {
    switch (type) {
        case WGPUBackendType_Null: return "Null";
        case WGPUBackendType_WebGPU: return "WebGPU";
        case WGPUBackendType_D3D11: return "D3D11";
        case WGPUBackendType_D3D12: return "D3D12";
        case WGPUBackendType_Metal: return "Metal";
        case WGPUBackendType_Vulkan: return "Vulkan";
        case WGPUBackendType_OpenGL: return "OpenGL";
        case WGPUBackendType_OpenGLES: return "OpenGLES";
        case WGPUBackendType_Undefined: return "Undefined";
        default: return "Unknown";
    }
}

const char* adapterTypeName(WGPUAdapterType type) {
    switch (type) {
        case WGPUAdapterType_DiscreteGPU: return "Discrete GPU";
        case WGPUAdapterType_IntegratedGPU: return "Integrated GPU";
        case WGPUAdapterType_CPU: return "CPU";
        case WGPUAdapterType_Unknown: return "Unknown";
        default: return "Unknown";
    }
}

struct AdapterRequestState {
    WGPURequestAdapterStatus status = WGPURequestAdapterStatus_Error;
    WGPUAdapter adapter = nullptr;
    std::string message;
};

void onAdapterRequestEnded(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    WGPUStringView message,
    void* userdata1,
    void* /*userdata2*/
) {
    auto* state = static_cast<AdapterRequestState*>(userdata1);
    state->status = status;
    state->adapter = adapter;
    state->message = toString(message);
}

struct DeviceRequestState {
    WGPURequestDeviceStatus status = WGPURequestDeviceStatus_Error;
    WGPUDevice device = nullptr;
    std::string message;
};

void onDeviceRequestEnded(
    WGPURequestDeviceStatus status,
    WGPUDevice device,
    WGPUStringView message,
    void* userdata1,
    void* /*userdata2*/
) {
    auto* state = static_cast<DeviceRequestState*>(userdata1);
    state->status = status;
    state->device = device;
    state->message = toString(message);
}

void onUncapturedError(
    WGPUDevice const* /*device*/,
    WGPUErrorType type,
    WGPUStringView message,
    void* /*userdata1*/,
    void* /*userdata2*/
) {
    std::cerr << "[WebGPU uncaptured error] type="
              << static_cast<int>(type)
              << ": " << toString(message) << '\n';
}

void onDeviceLost(
    WGPUDevice const* /*device*/,
    WGPUDeviceLostReason reason,
    WGPUStringView message,
    void* /*userdata1*/,
    void* /*userdata2*/
) {
    // Dawn also reports normal device destruction through this callback.
    if (reason == WGPUDeviceLostReason_Destroyed) {
        return;
    }

    std::cerr << "[WebGPU device lost] reason="
              << static_cast<int>(reason);

    const std::string text = toString(message);
    if (!text.empty()) {
        std::cerr << ": " << text;
    }
    std::cerr << '\n';
}

bool waitForOneFuture(
    WGPUInstance instance,
    WGPUFuture future,
    const char* operationName
) {
    WGPUFutureWaitInfo waitInfo = WGPU_FUTURE_WAIT_INFO_INIT;
    waitInfo.future = future;

    const WGPUWaitStatus waitStatus = wgpuInstanceWaitAny(
        instance,
        1,
        &waitInfo,
        kWaitTimeoutNs
    );

    if (waitStatus == WGPUWaitStatus_TimedOut) {
        std::cerr << operationName << " timed out\n";
        return false;
    }

    if (waitStatus != WGPUWaitStatus_Success ||
        waitInfo.completed != WGPU_TRUE) {
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
        instance,
        &adapterOptions,
        adapterCallbackInfo
    );

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
              << "  backend:      " << backendTypeName(adapterInfo.backendType) << '\n'
              << "  type:         " << adapterTypeName(adapterInfo.adapterType) << '\n';

    wgpuAdapterInfoFreeMembers(adapterInfo);

    WGPUSupportedFeatures adapterFeatures = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuAdapterGetFeatures(adapter, &adapterFeatures);
    std::cout << "Adapter feature count: " << adapterFeatures.featureCount << '\n';
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
              << "  maxTextureDimension2D: " << adapterLimits.maxTextureDimension2D << '\n'
              << "  maxBindGroups:         " << adapterLimits.maxBindGroups << '\n'
              << "  maxBufferSize:         " << adapterLimits.maxBufferSize << '\n'
              << "  maxVertexBuffers:      " << adapterLimits.maxVertexBuffers << '\n'
              << "  maxVertexAttributes:   " << adapterLimits.maxVertexAttributes << '\n';

    DeviceRequestState deviceState;

    WGPURequestDeviceCallbackInfo deviceCallbackInfo =
        WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    deviceCallbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
    deviceCallbackInfo.callback = onDeviceRequestEnded;
    deviceCallbackInfo.userdata1 = &deviceState;

    WGPUDeviceDescriptor deviceDescriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    deviceDescriptor.label = {"Tutorial device", WGPU_STRLEN};
    deviceDescriptor.defaultQueue.label = {"Tutorial default queue", WGPU_STRLEN};

    deviceDescriptor.uncapturedErrorCallbackInfo.callback =
        onUncapturedError;

    deviceDescriptor.deviceLostCallbackInfo.mode =
        WGPUCallbackMode_AllowSpontaneous;
    deviceDescriptor.deviceLostCallbackInfo.callback =
        onDeviceLost;

    const WGPUFuture deviceFuture = wgpuAdapterRequestDevice(
        adapter,
        &deviceDescriptor,
        deviceCallbackInfo
    );

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

```

---

## 23. Как читать полный пример как историю

В коде уже много строк, поэтому полезнее пересказать его без синтаксиса.

```text
1. Инициализировать GLFW.

2. Создать Instance с TimedWaitAny.

3. Попросить Instance найти high-performance Adapter.

4. Дождаться RequestAdapter.

5. Спросить Adapter:
   - кто ты?
   - через какой backend работаешь?
   - какие features доступны?
   - какие limits доступны?

6. Создать DeviceDescriptor.

7. Не запрашивать лишних optional capabilities,
   потому что текущему renderer они не нужны.

8. Подключить callbacks:
   - uncaptured errors;
   - device lost.

9. Попросить Adapter создать Device.

10. Дождаться RequestDevice.

11. Получить default Queue из Device.

12. Посмотреть реальные features и limits Device.

13. Освободить Queue, Device, Adapter и Instance.
```

Если эта история понятна, типы перестают быть набором похожих handles.

У каждого появляется отдельная роль.

---

## 24. Кто за что отвечает — теперь уже можно сделать таблицу

Таблица полезна **после** построения mental model:

| Объект | Главный вопрос |
|---|---|
| `WGPUInstance` | Какие WebGPU adapters/surfaces вообще доступны? |
| `WGPUAdapter` | Что конкретная graphics implementation способна предложить? |
| `WGPUDevice` | Какими capabilities разрешено пользоваться этому renderer? |
| `WGPUQueue` | Куда отправлять подготовленную GPU-работу? |

И ещё короче:

```text
Instance = вход в WebGPU
Adapter  = выбор + capabilities
Device   = рабочий GPU-контекст
Queue    = отправка работы
```

---

## 25. Lifetime: почему release идёт в обратном порядке

Создавали мы объекты так:

```text
Instance
   ↓
Adapter
   ↓
Device
   ↓
Queue
```

Освобождаем наоборот:

```cpp
wgpuQueueRelease(queue);
wgpuDeviceRelease(device);
wgpuAdapterRelease(adapter);
wgpuInstanceRelease(instance);
```

Это естественная модель зависимостей:

```text
родительский контекст живёт дольше объектов,
которые были созданы из него
```

В C API это видно напрямую.

Позже, если обернуть handles в RAII-классы, порядок destruction можно будет делегировать C++.

Но сначала полезно увидеть настоящий lifetime WebGPU объектов.

---

## 26. Как это выглядит через C++ wrapper

Архитектура не меняется:

```cpp
wgpu::Adapter adapter;
wgpu::Device device;
wgpu::Queue queue;
```

Adapter info:

```cpp
wgpu::AdapterInfo info;

if (adapter.GetInfo(&info) == wgpu::Status::Success) {
    std::cout
        << std::string_view(info.device)
        << '\n';
}
```

Limits:

```cpp
wgpu::Limits limits;
adapter.GetLimits(&limits);
```

Device request концептуально выглядит так:

```cpp
wgpu::DeviceDescriptor descriptor{};
descriptor.label = "Tutorial device";

auto future = adapter.RequestDevice(
    &descriptor,
    wgpu::CallbackMode::WaitAnyOnly,
    [](wgpu::RequestDeviceStatus status,
       wgpu::Device device,
       wgpu::StringView message) {
        // ...
    }
);
```

Queue:

```cpp
wgpu::Queue queue = device.GetQueue();
```

Главное преимущество wrapper здесь уже хорошо заметно: `wgpu::Adapter`, `wgpu::Device`, `wgpu::Queue`, `wgpu::AdapterInfo` и `wgpu::SupportedFeatures` используют C++ lifetime/RAII и уменьшают количество ручных `Release()`/`FreeMembers()`.

Но underlying модель остаётся той же самой.

---

## 27. Почему в основном туториале всё равно остаёмся на C API

C++ версия короче, но C API позволяет лучше увидеть устройство WebGPU:

```text
какие объекты реально возвращаются
кто чем владеет
что нужно освобождать
где callback
где future
где descriptor
```

После понимания этой модели перейти на wrapper легко.

Обратный путь сложнее: если сначала увидеть только RAII wrappers, часть архитектурных решений API выглядит как магия библиотеки.

---

## 28. Что мы пока ещё не умеем

У нас уже есть:

```text
Instance
Adapter
Device
Queue
```

Но приложение всё ещё ничего не выводит на экран.

Причина проста: Device знает, **как выполнять GPU-работу**, но мы ещё не сказали, **куда должен попасть готовый кадр**.

Для этого нужен `WGPUSurface`.

Surface связывает WebGPU с конкретной оконной системой:

```text
GLFW window
      │
      ▼
 WGPUSurface
      │
      ▼
 изображения для показа пользователю
```

И именно здесь станет понятно, зачем мы вообще подключили GLFW в первой главе.

---

## 29. Контрольная точка главы

После сборки программа должна вывести информацию примерно такого вида:

```text
Adapter
  vendor:       ...
  architecture: ...
  device:       ...
  description:  ...
  backend:      Vulkan / D3D12 / Metal / ...
  type:         Discrete GPU / Integrated GPU / ...

Adapter feature count: ...
Adapter limits
  maxTextureDimension2D: ...
  maxBindGroups:         ...
  maxBufferSize:         ...
  maxVertexBuffers:      ...
  maxVertexAttributes:   ...

Device created successfully
Device feature count: ...
Device maxBindGroups: ...
Default queue acquired
```

В моей VM физического GPU и Vulkan driver нет, поэтому проверочный запуск выглядит иначе:

```text
Adapter
  device:       Null backend
  backend:      Null
  type:         Unknown

Adapter feature count: 65
Adapter limits
  maxTextureDimension2D: 8192
  maxBindGroups:         4
  maxBufferSize:         268435456
  maxVertexBuffers:      8
  maxVertexAttributes:   16

Device created successfully
Device feature count: 1
Device maxBindGroups: 4
Default queue acquired
```

Перед этим Dawn сообщает, что Vulkan driver не найден. Для этой виртуальной машины это ожидаемо; сам C API, negotiation Device и Queue при этом успешно проверены через Null backend.

---

## 30. Что важно вынести из главы

Не синтаксис `wgpuAdapterGetLimits()`.

Главная идея выглядит так:

```text
Adapter не является рабочим Device.
```

Adapter нужен, чтобы **выбрать реализацию и узнать, что она может предложить**.

Device нужен, чтобы **зафиксировать конкретный capability contract приложения и создавать GPU-ресурсы**.

Queue нужна, чтобы **отправлять подготовленную работу на выполнение**.

Именно поэтому WebGPU startup постепенно строит цепочку:

```text
Instance
   ↓
Surface        ← добавим в следующей главе
   ↓
Adapter
   ↓
Device
   ↓
Queue
```

В следующей главе создадим настоящее GLFW-окно без OpenGL context, превратим его в `WGPUSurface`, изучим surface capabilities и затем исправим порядок initialization так, чтобы Adapter выбирался с `compatibleSurface`.
