# WebGPU + Dawn + GLFW на C++: план туториала

> Статус плана: 29 октября 2026.
>
> Основной стиль: **C API WebGPU/Dawn, вызываемый из C++20** (`WGPUDevice`, `wgpuDeviceCreateBuffer`, ...).
> В каждой главе рядом с основным C API будет небольшой блок **«Как это выглядит через C++ wrapper»** (`wgpu::Device`, `device.CreateBuffer(...)`, ...).

## Цель серии

Пошагово дойти от пустого C++ проекта до приложения, которое:

- создаёт окно через GLFW;
- инициализирует Dawn/WebGPU;
- выбирает adapter и создаёт device;
- создаёт и конфигурирует surface;
- выполняет render loop;
- компилирует WGSL shaders;
- рисует 2D примитив;
- работает с vertex/index/uniform buffers и bind groups;
- включает depth testing;
- рисует перспективный 3D куб;
- обрабатывает resize;
- считает delta time;
- получает ввод с клавиатуры и мыши через GLFW;
- управляет камерой в стиле FPS (WASD + mouse look).

Финальный результат первой части: **цветной 3D куб с depth buffer и свободной FPS-камерой**.

---

# Принципы серии

## 1. C API — основной

Код компилируется как C++, но WebGPU используется в C-стиле:

```cpp
WGPUDevice device = nullptr;
WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &descriptor);
```

А не как основной вариант:

```cpp
wgpu::Device device;
wgpu::Buffer buffer = device.CreateBuffer(&descriptor);
```

C++ wrapper показывается рядом, чтобы было понятно соответствие API, но он не скрывает механику основной версии.

## 2. Современный Dawn, а не API из старых туториалов

Особое внимание уделяем местам, которые заметно изменились:

- `WGPUFuture` и современная модель callback'ов;
- `WGPUCallbackMode`;
- `WGPUStringView`;
- современное создание adapter/device;
- `WGPUSurface`, `WGPUSurfaceCapabilities`, `WGPUSurfaceConfiguration`;
- `wgpuSurfaceGetCurrentTexture`;
- `wgpuSurfacePresent`;
- отказ от старого `WGPUSwapChain`-ориентированного кода;
- актуальные chained descriptors;
- lifetime/release WebGPU handles;
- актуальный GLFW helper Dawn.

## 3. Версия Dawn должна быть воспроизводимой

`main` Dawn меняется слишком часто, поэтому код серии нельзя привязывать только к «последнему Dawn».

Для каждой опубликованной версии туториала фиксируем:

- дату проверки;
- commit hash Dawn;
- версию GLFW;
- компилятор, на котором проверялось;
- платформы, на которых пример реально запускался.

В начале каждой главы будет блок:

```text
Tested with:
Dawn: <commit>
GLFW: <version>
CMake: <version>
C++: C++20
```

При обновлении Dawn сначала обновляется отдельная compatibility note, а затем главы, которые реально сломались.

## 4. Минимум «магических» helper-функций

Не будем использовать Dawn sample utilities вроде `ComboRenderPipelineDescriptor` как основу обучения.

Дескрипторы WebGPU заполняем самостоятельно, чтобы было видно, что реально передаётся API.

Допустимое исключение — официальный GLFW helper Dawn для создания platform-specific surface:

```cpp
WGPUSurface surface = wgpuGlfwCreateSurfaceForWindow(instance, window);
```

В отдельной заметке можно показать, что helper внутри создаёт platform-specific chained descriptor для Win32 / X11 / Wayland / Metal.

## 5. Каждый шаг должен запускаться

Каждая глава заканчивается конкретным состоянием программы, которое можно собрать и проверить.

Желательно хранить рядом snapshot исходников:

```text
chapters/
examples/
  01-project/
  02-instance/
  03-device/
  ...
```

Так читатель может начать с любой контрольной точки.

---

# План глав

## Глава 01 — Проект, Dawn, GLFW и CMake

**Файл:** `01-project-setup.md`

### Результат

Проект собирается; программа запускается и выводит диагностическое сообщение.

### Содержание

- что такое WebGPU и где здесь Dawn;
- Dawn как native implementation WebGPU;
- разница между `webgpu.h` и `webgpu_cpp.h`;
- почему основной код серии использует `webgpu.h`;
- структура проекта;
- C++20;
- сборка Dawn;
- подключение Dawn к проекту;
- подключение GLFW;
- CMake targets;
- Debug/Release;
- первая проверка линковки;
- фиксация commit Dawn;
- зачем не использовать бесконтрольный `main` в учебном проекте.

### Врезка C++ API

Какие headers и target'ы нужны, если основной код писать через `webgpu_cpp.h`.

---

## Глава 02 — Instance и современная асинхронная модель WebGPU

**Файл:** `02-instance-and-futures.md`

### Результат

Создаём `WGPUInstance` и понимаем, как Dawn обрабатывает asynchronous operations.

### Содержание

- `WGPUInstance`;
- `WGPUInstanceDescriptor`;
- instance features;
- зачем может понадобиться `TimedWaitAny`;
- `WGPUFuture`;
- `WGPUFutureWaitInfo`;
- `wgpuInstanceWaitAny`;
- `wgpuInstanceProcessEvents`;
- `WGPUCallbackMode_WaitAnyOnly`;
- `AllowProcessEvents`;
- `AllowSpontaneous`;
- почему современный callback API нельзя механически копировать из старых уроков;
- userdata в callback;
- `WGPUStringView`;
- проверка ошибок.

### Врезка C++ API

```cpp
wgpu::Instance
instance.RequestAdapter(...)
instance.WaitAny(...)
```

и соответствие этим вызовам в C API.

---

## Глава 03 — Adapter, Device, Queue и диагностика

**Файл:** `03-adapter-device-queue.md`

### Результат

Получаем adapter, создаём device и queue, выводим информацию о выбранном GPU/backend.

### Содержание

- `WGPURequestAdapterOptions`;
- power preference;
- compatible surface — пока без surface либо с подготовкой к следующей главе;
- callback запроса adapter;
- `WGPUAdapterInfo`;
- features и limits;
- создание `WGPUDevice`;
- `WGPUDeviceDescriptor`;
- required features / limits;
- queue;
- uncaptured error callback;
- device lost callback;
- logging/diagnostics Dawn;
- lifetime adapter/device/queue;
- release функций C API.

### Контрольная точка

В консоли видны сведения об adapter и программа чисто освобождает ресурсы.

---

## Глава 04 — GLFW окно и WebGPU Surface

**Файл:** `04-glfw-window-and-surface.md`

### Результат

Открывается GLFW окно без OpenGL context и создаётся `WGPUSurface`.

### Содержание

- `glfwInit`;
- `GLFW_CLIENT_API = GLFW_NO_API`;
- создание окна;
- почему OpenGL context нам не нужен;
- `webgpu_glfw.h`;
- `wgpuGlfwCreateSurfaceForWindow`;
- что helper делает на Win32 / X11 / Wayland / macOS;
- `WGPUSurfaceCapabilities`;
- surface formats;
- present modes;
- alpha modes;
- выбор формата;
- `WGPUSurfaceConfiguration`;
- `wgpuSurfaceConfigure`;
- framebuffer size vs window size;
- HiDPI;
- resize callback;
- reconfigure surface.

### Важный акцент

Отдельно показать, почему старый код через `WGPUSwapChain` больше не является правильной отправной точкой.

---

## Глава 05 — Первый кадр: очистка окна

**Файл:** `05-first-frame.md`

### Результат

Окно стабильно заливается выбранным цветом каждый кадр.

### Содержание

- render loop;
- `wgpuSurfaceGetCurrentTexture`;
- `WGPUSurfaceTexture`;
- статусы получения surface texture;
- texture view;
- command encoder;
- render pass descriptor;
- color attachment;
- load/store operations;
- clear color;
- завершение render pass;
- command buffer;
- `wgpuQueueSubmit`;
- `wgpuSurfacePresent`;
- обработка resize/outdated/lost surface;
- правильное освобождение per-frame handles.

### Почему эта глава отдельная

До появления pipeline мы уже проверяем всю цепочку:

`Surface -> Texture -> View -> CommandEncoder -> RenderPass -> Queue -> Present`.

---

## Глава 06 — WGSL и первый Render Pipeline

**Файл:** `06-wgsl-and-pipeline.md`

### Результат

Рисуем треугольник без vertex buffer: вершины вычисляются через `vertex_index`.

### Содержание

- WGSL shader module;
- chained descriptor источника WGSL;
- vertex shader;
- fragment shader;
- entry points;
- `WGPUVertexState`;
- `WGPUFragmentState`;
- color target state;
- primitive state;
- multisample state;
- pipeline layout `auto` / implicit layout;
- `WGPURenderPipelineDescriptor`;
- `wgpuDeviceCreateRenderPipeline`;
- bind pipeline;
- `wgpuRenderPassEncoderDraw`;
- shader compilation errors;
- coordinate system WebGPU.

### Врезка C++ API

Полный компактный эквивалент создания shader module и render pipeline через `wgpu::`.

---

## Глава 07 — Vertex Buffer и Index Buffer

**Файл:** `07-vertex-and-index-buffers.md`

### Результат

Геометрия хранится в GPU buffers; рисуем индексированный цветной quad или несколько треугольников.

### Содержание

- buffer descriptor;
- `WGPUBufferUsage_CopyDst`;
- `Vertex` и `Index` usages;
- `wgpuQueueWriteBuffer`;
- структура `Vertex` в C++;
- alignment и stride;
- `WGPUVertexBufferLayout`;
- `WGPUVertexAttribute`;
- shader locations;
- vertex formats;
- index buffer;
- `Uint16` / `Uint32`;
- `wgpuRenderPassEncoderSetVertexBuffer`;
- `SetIndexBuffer`;
- `DrawIndexed`;
- interleaved vertex data.

---

## Глава 08 — Uniform Buffer, Bind Group и матрицы

**Файл:** `08-uniforms-bind-groups-matrices.md`

### Результат

Передаём CPU-side transform в shader и двигаем/вращаем геометрию через матрицу.

### Содержание

- uniform buffer;
- bind group layout;
- bind group;
- `@group` и `@binding` в WGSL;
- visibility;
- min binding size;
- uniform alignment;
- обновление uniform через queue;
- model/view/projection;
- WebGPU clip-space;
- z range `[0, 1]`;
- использование GLM;
- настройка GLM для WebGPU depth range;
- порядок умножения матриц;
- CPU/GPU memory layout;
- padding.

### Решение по математике

Основной вариант серии использует **GLM**, чтобы серия оставалась про WebGPU, а не превращалась в отдельный курс линейной алгебры.

При этом формулы и назначение матриц объясняются, а не скрываются.

---

## Глава 09 — 3D куб и Depth Buffer

**Файл:** `09-cube-and-depth-buffer.md`

### Результат

На экране появляется перспективный 3D куб с корректной видимостью граней.

### Содержание

- вершины куба;
- индексная геометрия;
- winding order;
- front face;
- back-face culling;
- depth texture;
- `WGPUTextureFormat_Depth24Plus` или выбранный актуальный depth format;
- texture usage `RenderAttachment`;
- depth texture view;
- `WGPUDepthStencilState`;
- depth compare;
- depth clear;
- depth attachment render pass;
- пересоздание depth texture при resize;
- perspective projection;
- aspect ratio.

---

## Глава 10 — Render Loop, время и resize

**Файл:** `10-render-loop-time-resize.md`

### Результат

Куб плавно вращается независимо от FPS, а resize окна не ломает изображение.

### Содержание

- `glfwPollEvents`;
- `glfwGetTime` или `std::chrono`;
- delta time;
- frame-independent animation;
- framebuffer resize;
- минимизированное окно (`width == 0 || height == 0`);
- повторный `wgpuSurfaceConfigure`;
- пересоздание depth resources;
- projection aspect ratio;
- обработка surface statuses;
- разделение update/render.

---

## Глава 11 — Ввод GLFW и модель камеры

**Файл:** `11-input-and-camera.md`

### Результат

Есть объект `Camera`, положение и ориентация которого обновляются с клавиатуры и мыши.

### Содержание

- GLFW key state;
- cursor position callback;
- захват мыши;
- `GLFW_CURSOR_DISABLED`;
- first mouse event;
- yaw и pitch;
- clamp pitch;
- front/right/up vectors;
- нормализация;
- world up;
- view matrix;
- sensitivity;
- movement speed;
- delta time;
- почему движение нельзя считать «пикселей/кадр» или «метров/кадр».

---

## Глава 12 — FPS Camera + куб: собираем всё вместе

**Файл:** `12-fps-camera.md`

### Результат

Финальный пример первой части:

- куб рендерится через Dawn/WebGPU;
- depth test работает;
- WASD двигает камеру;
- мышь меняет yaw/pitch;
- Escape освобождает/закрывает окно;
- resize корректен;
- движение не зависит от FPS.

### Содержание

- окончательная структура `Application`;
- `WebGPUContext`;
- `Renderer`;
- `Camera`;
- порядок кадра:
  1. poll input;
  2. calculate delta time;
  3. update camera;
  4. update uniforms;
  5. acquire surface texture;
  6. encode commands;
  7. submit;
  8. present;
- shutdown order;
- release всех C API handles;
- типичные ошибки;
- итоговый source tree.

### Финальная врезка

Показать, насколько короче тот же renderer выглядит с C++ wrapper и какие детали wrapper скрывает.

---

# Дополнительные заметки, которые стоит включать по ходу серии

## C API lifetime

В C-стиле читатель должен явно видеть ownership. Для каждого нового WebGPU handle в тексте отмечаем:

- кто его создаёт;
- кому он принадлежит;
- когда он больше не нужен;
- какой release вызов используется.

Это одна из причин, почему C API полезен именно как учебный вариант.

## Не скрывать ошибки

С первых глав подключаем:

- error callbacks;
- device lost callback;
- проверку status;
- понятный вывод ошибок;
- проверки `nullptr`.

## Отдельные блоки «Старый код / современный код»

Там, где это действительно полезно, показываем небольшой фрагмент старого подхода из эпохи ранних Dawn/WebGPU туториалов и рядом современный эквивалент.

Не переписываем старые уроки целиком — показываем только API-разницу.

Особенно полезно для:

- adapter/device callbacks;
- string handling;
- swapchain -> surface configuration;
- surface texture acquisition;
- WGSL descriptors;
- callback/event processing.

---

# Формат каждой главы

Каждый `.md` желательно строить одинаково:

1. **Что получим в конце главы**
2. **Что изменилось в современном Dawn**
3. **Теория ровно в необходимом объёме**
4. **Изменения CMake / структуры проекта**
5. **Код по шагам**
6. **Что делает каждый WebGPU descriptor**
7. **Полный код контрольной точки**
8. **Как это выглядит через C++ wrapper**
9. **Типичные ошибки**
10. **Что проверить перед следующей главой**

---

# Что можно проверять в текущей виртуальной среде ChatGPT

Проверенная среда:

- Debian GNU/Linux 13 (trixie);
- x86-64;
- GCC 14.2;
- Clang 17 присутствует, но текущий Dawn официально ориентируется на более новый Clang; GCC 14 подходит по заявленным требованиям;
- CMake 3.31.6;
- Ninja 1.12.1;
- Go 1.23.2;
- около 5 vCPU;
- около 5.8 GiB RAM;
- около 30 GiB свободного диска;
- Xvfb работает и может поднимать виртуальный X11 display;
- Mesa software rasterizer (`swrast_dri.so`) присутствует;
- Vulkan loader присутствует;
- физического GPU `/dev/dri` нет;
- Vulkan ICD в текущем образе не обнаружен;
- GLFW development package в текущем образе не установлен;
- Dawn в текущем образе не установлен;
- контейнерный shell не имеет прямого сетевого доступа для `git clone` / загрузки зависимостей.

### Практический вывод

В этой среде можно уверенно:

- генерировать C++/CMake/WGSL код;
- компилировать код, который не требует отсутствующих внешних библиотек;
- проверять C++ syntax и архитектуру проекта;
- проверять виртуальное X11 окружение;
- сверять API с актуальным исходным кодом Dawn через доступ к официальным web-источникам.

Но прямо сейчас нельзя честно заявлять, что полный GLFW + Dawn пример **запущен** здесь: для этого не хватает установленного Dawn/GLFW и доступной цепочки загрузки их зависимостей.

Если зависимости будут доступны локально в sandbox (архив, исходники или готовая сборка), можно будет собирать примеры здесь. Для графического запуска разумный первый кандидат — Dawn OpenGL backend поверх Xvfb + Mesa software rasterizer. Vulkan потребует software ICD (например, lavapipe/SwiftShader), которого сейчас в образе нет.

---

# Предлагаемый порядок написания

Первая итерация:

1. `01-project-setup.md`
2. `02-instance-and-futures.md`
3. `03-adapter-device-queue.md`
4. `04-glfw-window-and-surface.md`
5. `05-first-frame.md`

После этих пяти глав инфраструктура уже должна быть современной и устойчивой. Далее графическая часть становится заметно прямолинейнее:

6. shader + triangle;
7. buffers;
8. uniforms + matrices;
9. cube + depth;
10. robust loop + resize;
11. input + camera math;
12. FPS camera integration.

Именно первые 4–5 глав нужно проверять особенно тщательно при каждом обновлении Dawn, потому что API-инфраструктура меняется чаще, чем базовые идеи vertex buffers, depth testing или camera matrices.

---

# Официальные источники, на которые ориентируется серия

- Dawn repository: https://dawn.googlesource.com/dawn/
- Dawn CMake quickstart: https://dawn.googlesource.com/dawn/+/refs/heads/main/docs/quickstart-cmake.md
- Dawn GLFW helper: https://dawn.googlesource.com/dawn/+/refs/heads/main/include/webgpu/webgpu_glfw.h
- Dawn GLFW implementation example: https://dawn.googlesource.com/dawn/+/refs/heads/main/src/dawn/glfw/utils.cpp
- WebGPU native headers project: https://github.com/webgpu-native/webgpu-headers

