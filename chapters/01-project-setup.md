# Глава 01 — Из чего вообще состоит нативное WebGPU-приложение

> Проверено: 29 октября 2026  
> **Dawn:** `40d98d8812bb3211592bb78cac8bd7b13b585e19`  
> **GLFW:** 3.6.0  
> **GCC:** 14.2.0  
> **CMake:** 3.31.6  
> **C++:** C++20  
> **Платформа проверки:** Linux amd64, GLFW/X11  
> **Основной стиль:** WebGPU C API в C++ коде

В первой главе мы ещё ничего не нарисуем. Это намеренно.

Перед тем как создавать треугольник, полезно понять, **кто именно будет его рисовать** и зачем в одном небольшом проекте внезапно нужны сразу WebGPU, Dawn и GLFW.

К концу главы у нас будет минимальный проект, который действительно линкуется с Dawn и GLFW и создаёт первый объект WebGPU — `WGPUInstance`. Но важнее другое: станет понятна общая архитектура, на которую дальше будут нанизываться adapter, device, surface, pipeline и все остальные части WebGPU.

---

## 1. Мы хотим рисовать на GPU. Почему нельзя просто обратиться к GPU?

Если сильно упростить, наша программа работает на CPU, а изображение должен построить GPU.

Можно было бы представить желаемый код так:

```text
CPU program
    │
    └── draw triangle
            │
            ▼
           GPU
```

Но реальная система устроена сложнее.

У разных операционных систем и видеодрайверов разные графические API:

```text
Windows  -> Direct3D 12
Linux    -> Vulkan / OpenGL
macOS    -> Metal
```

Если приложение напрямую работает с ними, то значительная часть графического кода становится платформозависимой.

WebGPU вводит ещё один слой:

```text
наше приложение
      │
      │ WebGPU
      ▼
 реализация WebGPU
      │
      ├── Vulkan
      ├── Metal
      ├── D3D12
      └── ...
             │
             ▼
            GPU
```

Мы пишем код в терминах WebGPU: buffer, texture, render pipeline, command encoder. А реализация WebGPU решает, какими средствами конкретной платформы это выполнить.

В браузере эту реализацию предоставляет браузер. В нашем нативном C++ приложении ею будет **Dawn**.

---

## 2. Тогда что такое Dawn?

Dawn — это реализация WebGPU, разрабатываемая в экосистеме Chromium.

Для нас это означает следующее:

```cpp
WGPUInstance instance = wgpuCreateInstance(...);
```

Функция объявлена WebGPU C API, но её реальный код находится в библиотеке Dawn:

```text
libwebgpu_dawn.a
```

То есть важно различать два понятия:

```text
WebGPU = интерфейс и модель API
Dawn   = конкретная реализация этого API
```

Это похоже на ситуацию со стандартной библиотекой C++: язык описывает интерфейс и правила, а конкретный toolchain предоставляет реализацию.

В дальнейшем мы будем говорить «WebGPU», когда обсуждаем сам API, и «Dawn», когда речь идёт о конкретной реализации, её сборке, backend'ах или особенностях текущей версии.

---

## 3. А зачем тогда GLFW?

WebGPU умеет рисовать, но наше приложение всё ещё должно получить обычное окно операционной системы.

Создание окна само по себе сильно отличается между платформами:

```text
Windows -> Win32
Linux   -> X11 или Wayland
macOS   -> Cocoa
```

Нам не хочется одновременно учить WebGPU и писать три разных оконных backend'а. Поэтому эту задачу отдаём GLFW.

В будущем разделение обязанностей будет таким:

```text
                     наше приложение
                    /              \
                   /                \
                  ▼                  ▼
               GLFW                WebGPU
          окно / ввод              рендеринг
                  \                  /
                   \                /
                    ▼              ▼
                   операционная система
                          │
                          ▼
                         GPU
```

GLFW будет отвечать за:

- создание окна;
- события окна;
- клавиатуру;
- мышь;
- позже — захват курсора для FPS-камеры.

Dawn/WebGPU будет отвечать за:

- выбор графического adapter;
- создание device;
- работу с GPU-ресурсами;
- shaders;
- render pipeline;
- отправку команд GPU.

Позже официальный Dawn helper соединит эти два мира:

```cpp
wgpuGlfwCreateSurfaceForWindow(instance, window);
```

Но до surface мы ещё дойдём отдельно.

---

## 4. Почему проект написан на C++, а API используем C

Dawn предоставляет две удобные формы API.

C API:

```cpp
WGPUInstance instance = wgpuCreateInstance(&descriptor);
```

C++ wrapper:

```cpp
wgpu::Instance instance = wgpu::CreateInstance(&descriptor);
```

В этом туториале основной код будет на C++, но WebGPU мы будем изучать через **C API**.

Причина не в том, что C API «лучше». Для обучения он просто делает происходящее более явным.

Например, в C API сразу видно:

```cpp
WGPUInstance instance = ...;
wgpuInstanceRelease(instance);
```

То есть у нас есть handle, и мы явно управляем его lifetime.

C++ wrapper позже сможет скрыть часть этой работы за RAII. Это удобно в приложении, но на первых этапах полезно видеть, какие операции реально происходят.

Поэтому правило серии такое:

> Сначала разбираем механизм через C API. Рядом показываем, как тот же код выглядит через C++ wrapper.

При этом всё остальное приложение остаётся обычным современным C++20.

---

## 5. Что нам дали собранные зависимости

В проекте зависимости лежат локально:

```text
.modules/
├── dawn/
│   ├── DAWN_COMMIT
│   ├── include/
│   │   └── webgpu/
│   │       ├── webgpu.h
│   │       ├── webgpu_cpp.h
│   │       └── webgpu_glfw.h
│   └── lib/
│       ├── libwebgpu_dawn.a
│       └── libdawn_glfw.a
│
└── glfw3/
    ├── include/
    │   └── GLFW/
    │       ├── glfw3.h
    │       └── glfw3native.h
    └── lib/
        └── libglfw3.a
```

Пока нас интересуют только две библиотеки:

```text
libglfw3.a
libwebgpu_dawn.a
```

`libdawn_glfw.a` понадобится, когда мы начнём создавать WebGPU surface из GLFW window.

Из заголовков первым понадобится:

```cpp
#include <webgpu/webgpu.h>
```

Именно здесь находится C API с типами вида:

```cpp
WGPUInstance
WGPUAdapter
WGPUDevice
WGPUBuffer
```

и функциями вида:

```cpp
wgpuCreateInstance(...)
wgpuInstanceRequestAdapter(...)
wgpuDeviceCreateBuffer(...)
```

Пока последние две функции нам не нужны. Сейчас важно только проверить, что первый настоящий вызов Dawn вообще работает.

---

## 6. Почему мы фиксируем commit Dawn

Dawn обновляется постоянно. Для обычного приложения это хорошо, но для туториала создаёт проблему.

Если написать:

```bash
git clone --depth 1 https://dawn.googlesource.com/dawn
```

сегодня и повторить ту же команду через несколько месяцев, можно получить API с другими структурами, enum'ами или callback signatures.

Поэтому этот туториал привязан не к фразе «последний Dawn», а к конкретному commit:

```text
40d98d8812bb3211592bb78cac8bd7b13b585e19
```

Он сохранён в:

```text
.modules/dawn/DAWN_COMMIT
```

Проверить его можно так:

```bash
cat .modules/dawn/DAWN_COMMIT
```

Когда Dawn будет обновляться, правильно обновлять его осознанно: выбрать новый commit, пересобрать зависимости и прогнать все примеры заново.

---

## 7. Создаём каркас проекта

Начинаем с очень маленькой структуры:

```text
webgpu-tutorial/
├── .modules/
│   ├── dawn/
│   └── glfw3/
├── src/
│   └── main.cpp
└── CMakeLists.txt
```

Смысл такого старта простой: пока мы ещё не знаем, какие абстракции действительно понадобятся приложению, нет причин заранее создавать `Renderer`, `Application`, `Context`, `DeviceManager` и десяток других классов.

Мы сначала увидим WebGPU в работе, а структуру программы будем усложнять тогда, когда для этого появится реальная причина.

---

## 8. CMake: подключаем уже собранные библиотеки

Создадим `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.25)

project(webgpu_tutorial
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

set(GLFW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.modules/glfw3")
set(DAWN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.modules/dawn")

add_library(third_party_glfw STATIC IMPORTED GLOBAL)
set_target_properties(third_party_glfw PROPERTIES
    IMPORTED_LOCATION "${GLFW_ROOT}/lib/libglfw3.a"
    INTERFACE_INCLUDE_DIRECTORIES "${GLFW_ROOT}/include"
)
add_library(third_party::glfw ALIAS third_party_glfw)

add_library(third_party_webgpu_dawn STATIC IMPORTED GLOBAL)
set_target_properties(third_party_webgpu_dawn PROPERTIES
    IMPORTED_LOCATION "${DAWN_ROOT}/lib/libwebgpu_dawn.a"
    INTERFACE_INCLUDE_DIRECTORIES "${DAWN_ROOT}/include"
)
add_library(third_party::webgpu_dawn ALIAS third_party_webgpu_dawn)

add_executable(webgpu_tutorial
    src/main.cpp
)

target_link_libraries(webgpu_tutorial PRIVATE
    third_party::glfw
    third_party::webgpu_dawn
    Threads::Threads
)

if(UNIX AND NOT APPLE)
    target_link_libraries(webgpu_tutorial PRIVATE
        ${CMAKE_DL_LIBS}
        m
        rt
    )
endif()
```

Здесь много строк, но идея небольшая.

Dawn и GLFW уже собраны. Поэтому CMake не должен компилировать их заново — ему нужно только знать:

```text
где лежат headers?
где лежит .a?
```

Именно это описывает imported target:

```cmake
add_library(third_party_webgpu_dawn STATIC IMPORTED GLOBAL)
```

а затем:

```cmake
IMPORTED_LOCATION ".../libwebgpu_dawn.a"
INTERFACE_INCLUDE_DIRECTORIES ".../include"
```

После этого наш executable может зависеть не от набора случайных путей, а от понятной CMake-сущности:

```cmake
third_party::webgpu_dawn
```

Для текущего prebuilt bundle это также удобнее, чем полностью полагаться на экспортированные `find_package()` targets: системные зависимости статической библиотеки должны разрешаться уже на той машине, где собирается сам tutorial.

---

## 9. Первый `main.cpp`: пока без окна и без GPU

Теперь создадим `src/main.cpp`:

```cpp
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
```

На первый взгляд программа почти ничего не делает. Но в ней уже есть две важные границы системы.

Первая:

```cpp
glfwInit();
```

Мы инициализировали слой, который позже даст нам окно и input.

Вторая:

```cpp
wgpuCreateInstance(...);
```

Мы впервые вошли в WebGPU/Dawn.

При этом обратите внимание, чего здесь **ещё нет**:

```text
нет окна
нет surface
нет выбранного GPU
нет device
нет command queue
нет render pipeline
```

Это важно, потому что `WGPUInstance` не означает «мы получили GPU». Он находится уровнем выше. В следующей главе мы увидим, зачем WebGPU вообще нужен этот промежуточный объект.

---

## 10. Почему `GLFW_INCLUDE_NONE`

Перед GLFW мы написали:

```cpp
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
```

GLFW исторически часто используется вместе с OpenGL и может автоматически подключать OpenGL headers.

Но в нашем приложении OpenGL API напрямую не используется. Даже если Dawn позднее выберет OpenGL как внутренний backend, это будет деталь реализации Dawn, а не API нашего приложения.

Поэтому мы явно говорим GLFW:

> не подключай графический API за нас; нам от тебя нужны окно и input.

Это хорошо подчёркивает разделение ролей:

```text
GLFW    != renderer
WebGPU  != window library
```

---

## 11. Что такое descriptor и почему WebGPU использует их повсюду

Первая WebGPU-структура в нашем коде:

```cpp
WGPUInstanceDescriptor descriptor{};
```

WebGPU часто создаёт объекты не длинным списком аргументов, а через descriptor structure.

Вместо условного API:

```cpp
createSomething(width, height, format, usage, label, ...);
```

получается модель:

```cpp
WGPUSomethingDescriptor descriptor{};

descriptor.width = ...;
descriptor.height = ...;
descriptor.format = ...;

WGPUSomething object = wgpuCreateSomething(&descriptor);
```

По мере курса мы увидим:

```text
WGPUDeviceDescriptor
WGPUBufferDescriptor
WGPUTextureDescriptor
WGPURenderPipelineDescriptor
```

Такой подход делает API расширяемым: в descriptor можно добавлять настройки, не превращая каждую функцию создания в десятки параметров.

В этой главе descriptor пустой, потому что нам пока не нужны дополнительные instance features. В следующей главе это изменится.

---

## 12. Что такое `WGPUInstance` на интуитивном уровне

Пока достаточно такой модели:

```text
WGPUInstance
    = точка входа в конкретную реализацию WebGPU
```

Это **не GPU** и **не device**.

Представьте компьютер с двумя видеоустройствами:

```text
Integrated AMD GPU
Discrete NVIDIA GPU
```

На момент создания `WGPUInstance` мы ещё не сказали WebGPU, какое из них хотим использовать. Более того, реализация может иметь и software adapter.

То есть путь будет примерно таким:

```text
создать WebGPU instance
        │
        ▼
попросить подходящий adapter
        │
        ▼
создать logical device
        │
        ▼
создавать GPU resources
```

В следующей главе начнём именно со второго шага и разберём, почему запрос adapter в современном WebGPU асинхронный.

---

## 13. Lifetime: почему есть `Release`

После использования instance вызываем:

```cpp
wgpuInstanceRelease(instance);
```

C API не использует C++ destructor для `WGPUInstance`, потому что сам тип является C handle.

Это даёт нам возможность явно видеть lifetime объекта:

```text
wgpuCreateInstance()
        │
        │ instance жив
        ▼
   используем его
        │
        ▼
wgpuInstanceRelease()
```

Позже C++ wrapper сделает эту часть удобнее, но пока явный `Release` помогает не терять понимание ownership.

И GLFW тоже имеет симметричную пару:

```cpp
glfwInit();
...
glfwTerminate();
```

---

## 14. Собираем и запускаем

Debug-сборка:

```bash
cmake -S . -B build/debug -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug

cmake --build build/debug
```

Запуск:

```bash
./build/debug/webgpu_tutorial
```

В тестовой среде вывод выглядит примерно так:

```text
GLFW: 3.6.0 Wayland X11 GLX Null EGL OSMesa monotonic
Dawn/WebGPU: instance created successfully
```

Первая строка подтверждает, что GLFW реально инициализировался.

Вторая важнее, чем может показаться. Если бы мы только подключили:

```cpp
#include <webgpu/webgpu.h>
```

это проверило бы лишь headers. Но вызов:

```cpp
wgpuCreateInstance(...)
```

заставляет linker найти реальную реализацию функции в Dawn. Поэтому это уже настоящий smoke-test всей связки include + static library + runtime.

---

## 15. А как выглядел бы этот кусок через C++ wrapper?

C API:

```cpp
WGPUInstanceDescriptor descriptor{};
WGPUInstance instance = wgpuCreateInstance(&descriptor);

// ...

wgpuInstanceRelease(instance);
```

C++ wrapper:

```cpp
#include <webgpu/webgpu_cpp.h>

wgpu::InstanceDescriptor descriptor{};
wgpu::Instance instance = wgpu::CreateInstance(&descriptor);
```

Главное отличие, которое уже заметно: `wgpu::Instance` является C++ object wrapper и управляет ссылкой удобнее.

Но концептуальная модель не меняется. Это всё тот же WebGPU instance. Поэтому сначала мы разберём модель на более прозрачном C API, а wrapper будем воспринимать как другую форму записи тех же операций.

---

## 16. Что мы теперь понимаем

После первой главы архитектура приложения должна выглядеть в голове примерно так:

```text
                         CPU application
                       /                 \
                      /                   \
                     ▼                     ▼
                  GLFW                 WebGPU API
             window + input                │
                                           ▼
                                          Dawn
                                           │
                                   native graphics API
                                           │
                                           ▼
                                          GPU
```

Но до GPU мы пока фактически ещё не дошли.

Мы только создали:

```text
WGPUInstance
```

то есть получили точку входа в WebGPU implementation.

Следующий естественный вопрос:

> Хорошо, WebGPU доступен. Но какой GPU будет выполнять наши команды?

И здесь появляется следующая сущность — **adapter**.

Однако `RequestAdapter` приводит нас к ещё одной важной особенности современного WebGPU: многие операции API асинхронны и возвращают `WGPUFuture`.

Этим и займёмся в следующей главе.
