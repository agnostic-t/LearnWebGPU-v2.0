# Глава 01 — Проект, Dawn, GLFW и CMake

> Проверено: 29 октября 2026
>
> **Dawn:** `40d98d8812bb3211592bb78cac8bd7b13b585e19`  
> **GLFW:** 3.6.0  
> **GCC:** 14.2.0  
> **CMake:** 3.31.6  
> **Ninja:** 1.12.1  
> **C++:** C++20  
> **Платформа проверки:** Linux amd64, GLFW X11  
> **Основной стиль API:** WebGPU C API из `webgpu.h`

В этой главе мы не будем ничего рисовать и пока не будем разбирать adapter, device, surface или render pipeline. Цель намного прозаичнее, но для Dawn она критически важна: получить **воспроизводимый C++ проект**, который действительно компилируется, линкуется с GLFW и Dawn и запускается.

К концу главы программа:

- инициализирует GLFW;
- выводит версию GLFW;
- создаёт `WGPUInstance` через C API;
- корректно освобождает ресурсы;
- собирается в Debug и Release.

Создание `WGPUInstance` здесь используется только как smoke-test линковки. Сам `Instance`, его настройки и современную Future/callback модель Dawn подробно разберём в следующей главе.

---

## 1. Что именно мы собираем

WebGPU — это API. На нативной платформе нам нужна реализация этого API.

В этой серии такой реализацией будет **Dawn**.

Упрощённо наша программа будет устроена так:

```text
C++ application
    │
    ├── GLFW
    │     └── окно, клавиатура, мышь
    │
    └── WebGPU C API
          │
          └── Dawn
                └── Vulkan / Metal / D3D12 / OpenGL / ...
```

GLFW не рисует WebGPU-графику. Он будет отвечать за окно и ввод.

Dawn не заменяет GLFW. Он реализует WebGPU и позже создаст GPU device, surface, buffers, pipelines и остальные графические объекты.

На Linux Dawn может работать поверх разных backend'ов. Какой backend и какой физический adapter будет выбран, нас пока не интересует — до этого дойдём в главе 03.

---

## 2. Почему код на C++, а WebGPU API — C

Проект компилируется как C++20, но основной WebGPU-код серии будет написан через C API.

Например:

```cpp
WGPUInstance instance = wgpuCreateInstance(&descriptor);
```

а не:

```cpp
wgpu::Instance instance = wgpu::CreateInstance(&descriptor);
```

Это сознательное решение.

C API делает механику WebGPU более явной:

- видно реальные handle-типы `WGPUDevice`, `WGPUBuffer`, `WGPUSurface`;
- видно, какие функции создают объекты;
- видно, где объект нужно release'ить;
- проще сопоставлять код со спецификацией и заголовком `webgpu.h`;
- меньше логики скрыто за C++ wrapper'ом.

При этом сам язык C++ нам всё равно полезен: позже мы сможем использовать классы приложения, `std::vector`, `std::array`, RAII-обёртки, математику камеры и другие возможности языка.

В конце важных разделов будет блок **«Как это выглядит через C++ wrapper»**.

---

# 3. Какие заголовки Dawn нам нужны

После установки Dawn в каталоге `include/webgpu` находятся несколько важных файлов.

Для начала нас интересуют три:

```text
webgpu/
├── webgpu.h
├── webgpu_cpp.h
└── webgpu_glfw.h
```

## `webgpu.h`

Основной заголовок серии.

```cpp
#include <webgpu/webgpu.h>
```

Он содержит C API:

```cpp
WGPUInstance
WGPUDevice
WGPUBuffer
WGPURenderPipeline
```

и функции вида:

```cpp
wgpuCreateInstance(...)
wgpuDeviceCreateBuffer(...)
wgpuQueueSubmit(...)
```

---

## `webgpu_cpp.h`

C++ wrapper поверх того же API:

```cpp
#include <webgpu/webgpu_cpp.h>
```

Он предоставляет типы:

```cpp
wgpu::Instance
wgpu::Device
wgpu::Buffer
```

В основной версии проекта мы его пока не подключаем.

---

## `webgpu_glfw.h`

Официальный Dawn helper для интеграции GLFW с WebGPU surface:

```cpp
#include <webgpu/webgpu_glfw.h>
```

Позже он позволит написать:

```cpp
WGPUSurface surface =
    wgpuGlfwCreateSurfaceForWindow(instance, window);
```

Вместо самостоятельного написания отдельного кода для X11, Wayland, Win32 и Metal.

В этой главе `webgpu_glfw.h` ещё не нужен. Он появится в главе про Surface.

---

# 4. Статические библиотеки

Для текущей конфигурации зависимости выглядят так:

```text
.modules/
├── dawn/
│   ├── include/
│   │   └── webgpu/
│   │       ├── webgpu.h
│   │       ├── webgpu_cpp.h
│   │       └── webgpu_glfw.h
│   ├── lib/
│   │   ├── libwebgpu_dawn.a
│   │   └── libdawn_glfw.a
│   └── DAWN_COMMIT
│
└── glfw3/
    ├── include/
    │   └── GLFW/
    │       ├── glfw3.h
    │       └── glfw3native.h
    └── lib/
        └── libglfw3.a
```

Главная библиотека Dawn:

```text
libwebgpu_dawn.a
```

Именно в ней находится реализация WebGPU API, которую мы вызываем из `webgpu.h`.

`libdawn_glfw.a` понадобится позже, когда начнём использовать `wgpuGlfwCreateSurfaceForWindow()`.

Сейчас она нам не нужна.

---

# 5. Почему важно фиксировать версию Dawn

Dawn развивается быстро.

Если сегодня выполнить:

```bash
git clone --depth 1 https://dawn.googlesource.com/dawn
```

а через несколько месяцев повторить ту же команду, это уже может быть заметно другой API.

Поэтому фраза:

> «пример написан для последнего Dawn»

для учебного материала недостаточно точна.

В этой версии туториала используется commit:

```text
40d98d8812bb3211592bb78cac8bd7b13b585e19
```

Хорошая практика после сборки Dawn сохранить его commit рядом с установленными библиотеками:

```bash
git -C "$SOURCE_DIR" rev-parse HEAD \
    > "$INSTALL_DIR/DAWN_COMMIT"
```

Тогда версию можно проверить обычной командой:

```bash
cat .modules/dawn/DAWN_COMMIT
```

Для учебного проекта это намного надёжнее, чем пытаться помнить дату, когда зависимости были собраны.

---

# 6. Структура проекта

Начнём с минимальной структуры:

```text
webgpu-tutorial/
├── .modules/
│   ├── dawn/
│   └── glfw3/
│
├── src/
│   └── main.cpp
│
└── CMakeLists.txt
```

Позже проект разрастётся. Появятся файлы для приложения, камеры, geometry и WebGPU-ресурсов.

Но пока этого достаточно.

Создадим каталог исходников:

```bash
mkdir -p src
```

---

# 7. Первый `CMakeLists.txt`

Создадим в корне проекта файл `CMakeLists.txt`:

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

Разберём этот файл по частям.

---

## 7.1. C++20

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

Проект явно требует C++20.

`CMAKE_CXX_STANDARD_REQUIRED ON` запрещает CMake молча откатиться на более старый стандарт.

`CMAKE_CXX_EXTENSIONS OFF` просит использовать стандартный C++20, а не GNU-вариант вроде `gnu++20`.

Для GCC итоговый параметр выглядит примерно так:

```text
-std=c++20
```

---

## 7.2. Threads

```cmake
find_package(Threads REQUIRED)
```

Dawn и GLFW могут использовать системные threading primitives.

Вместо ручного добавления `-pthread` лучше дать CMake выбрать корректный способ через:

```cmake
Threads::Threads
```

---

## 7.3. Пути к локальным зависимостям

```cmake
set(GLFW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.modules/glfw3")
set(DAWN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.modules/dawn")
```

Мы намеренно храним зависимости рядом с проектом.

Это делает build более воспроизводимым: проект не зависит от случайно установленной системной версии GLFW или Dawn.

---

# 8. Imported target для GLFW

```cmake
add_library(third_party_glfw STATIC IMPORTED GLOBAL)
```

Эта команда говорит CMake:

> библиотека уже собрана; собирать её не нужно, но её можно использовать как target.

Указываем местоположение `.a` файла:

```cmake
IMPORTED_LOCATION "${GLFW_ROOT}/lib/libglfw3.a"
```

и include directory:

```cmake
INTERFACE_INCLUDE_DIRECTORIES "${GLFW_ROOT}/include"
```

Теперь любой target, который линкуется с `third_party_glfw`, автоматически получает путь к GLFW headers.

Для более удобного имени создаём alias:

```cmake
add_library(third_party::glfw ALIAS third_party_glfw)
```

После этого приложение может писать:

```cmake
target_link_libraries(webgpu_tutorial PRIVATE
    third_party::glfw
)
```

---

# 9. Imported target для Dawn

Точно так же подключаем основную библиотеку Dawn:

```cmake
add_library(third_party_webgpu_dawn STATIC IMPORTED GLOBAL)
set_target_properties(third_party_webgpu_dawn PROPERTIES
    IMPORTED_LOCATION "${DAWN_ROOT}/lib/libwebgpu_dawn.a"
    INTERFACE_INCLUDE_DIRECTORIES "${DAWN_ROOT}/include"
)
```

И создаём alias:

```cmake
add_library(third_party::webgpu_dawn ALIAS third_party_webgpu_dawn)
```

Теперь include:

```cpp
#include <webgpu/webgpu.h>
```

находится через `DAWN_ROOT/include`, а linker получает:

```text
libwebgpu_dawn.a
```

---

# 10. Почему здесь не используется `find_package(Dawn)`

У установленного Dawn есть CMake package files, поэтому технически можно встретить такой вариант:

```cmake
find_package(Dawn CONFIG REQUIRED)

target_link_libraries(app PRIVATE
    dawn::webgpu_dawn
)
```

А у GLFW:

```cmake
find_package(glfw3 CONFIG REQUIRED)
```

Если Dawn и GLFW собираются и используются **на одной машине**, это удобный вариант.

Но в этой серии мы хотим также уметь переносить уже собранный static bundle `.modules` между совместимыми Linux amd64 системами.

Экспортированные CMake targets статических библиотек могут содержать абсолютные пути к системным библиотекам машины, на которой выполнялась установка. Например:

```text
/usr/lib/librt.a
/usr/lib/libm.so
```

На другой Linux-системе те же библиотеки могут находиться, например, в multiarch-каталоге:

```text
/usr/lib/x86_64-linux-gnu/
```

Сама `libwebgpu_dawn.a` при этом может быть полностью рабочей, но импортированный CMake target перестанет быть переносимым.

Поэтому для prebuilt bundle в учебном проекте мы создаём собственные imported targets и просим **локальный CMake** найти системные зависимости.

Это не ограничение WebGPU и не требование Dawn. Это выбор структуры нашего проекта.

---

# 11. Linux system libraries

Для текущей статической сборки на Linux добавляем:

```cmake
if(UNIX AND NOT APPLE)
    target_link_libraries(webgpu_tutorial PRIVATE
        ${CMAKE_DL_LIBS}
        m
        rt
    )
endif()
```

Здесь:

- `${CMAKE_DL_LIBS}` — библиотека dynamic loader, обычно `dl`;
- `m` — стандартная math library;
- `rt` — POSIX realtime library там, где она нужна.

Этот блок относится к текущей Linux-конфигурации проекта.

На Windows или macOS набор системных зависимостей будет другим. WebGPU-код при этом останется практически тем же.

---

# 12. Первый `main.cpp`

Создадим `src/main.cpp`:

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

Это уже настоящий вызов Dawn C API.

---

# 13. Зачем нужен `GLFW_INCLUDE_NONE`

Перед GLFW header находится строка:

```cpp
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
```

Она важна.

По умолчанию GLFW может попытаться автоматически включить OpenGL headers.

Но наше приложение не использует OpenGL API напрямую. Нам нужен только GLFW как window/input library.

Поэтому мы явно говорим:

```cpp
#define GLFW_INCLUDE_NONE
```

То есть:

> не подключай OpenGL headers за нас.

Это избавляет проект от ненужной зависимости от `GL/gl.h` и уменьшает количество неявной магии в include chain.

Позже, когда мы создадим окно, мы также скажем GLFW не создавать OpenGL context:

```cpp
glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
```

Но это будет уже в главе про окно и Surface.

---

# 14. Инициализация GLFW

```cpp
if (glfwInit() != GLFW_TRUE) {
    std::cerr << "Failed to initialize GLFW\n";
    return EXIT_FAILURE;
}
```

`glfwInit()` инициализирует GLFW и его platform backend.

Пока мы даже не создаём окно.

После успешной инициализации можно посмотреть runtime-информацию:

```cpp
std::cout << "GLFW: " << glfwGetVersionString() << '\n';
```

На тестовой системе вывод выглядит так:

```text
GLFW: 3.6.0 Wayland X11 GLX Null EGL OSMesa monotonic
```

Нас особенно интересуют слова:

```text
Wayland X11
```

Они показывают, что текущая сборка GLFW содержит оба Linux platform backend'а.

---

# 15. Первый WebGPU descriptor

Теперь первая строка непосредственно из WebGPU:

```cpp
WGPUInstanceDescriptor descriptor{};
```

Тип начинается с префикса:

```text
WGPU
```

Это характерно для WebGPU C API.

`WGPUInstanceDescriptor` — обычная структура параметров.

Мы инициализируем её через:

```cpp
{}
```

поэтому поля получают нулевые/default значения.

В следующих главах мы будем встречать огромное количество подобных descriptor'ов:

```cpp
WGPUDeviceDescriptor
WGPUBufferDescriptor
WGPUTextureDescriptor
WGPURenderPipelineDescriptor
```

Это одна из центральных моделей WebGPU API:

```text
создать descriptor
       ↓
заполнить параметры
       ↓
передать descriptor в create-функцию
```

---

# 16. Создаём `WGPUInstance`

```cpp
WGPUInstance instance = wgpuCreateInstance(&descriptor);
```

Здесь уже участвуют три характерных элемента C API.

Тип handle:

```cpp
WGPUInstance
```

Функция:

```cpp
wgpuCreateInstance(...)
```

Descriptor:

```cpp
WGPUInstanceDescriptor
```

Паттерн повторится почти во всём WebGPU.

Мы пока не будем подробно обсуждать роль `Instance`.

В этой главе достаточно знать:

> это корневой объект, через который начинается работа с WebGPU/Dawn.

Подробнее — в главе 02.

---

# 17. Проверяем результат

C API возвращает opaque handle.

Проверяем его:

```cpp
if (instance == nullptr) {
    std::cerr << "Failed to create WebGPU instance\n";
    glfwTerminate();
    return EXIT_FAILURE;
}
```

Если всё прошло успешно:

```cpp
std::cout << "Dawn/WebGPU: instance created successfully\n";
```

Таким образом мы проверили уже не только compiler include path.

Если программа дошла сюда, значит:

```text
webgpu.h
   ↓
main.cpp compiled
   ↓
wgpuCreateInstance symbol resolved
   ↓
libwebgpu_dawn.a linked
   ↓
Dawn code executed
```

Это и есть хороший первый smoke-test.

---

# 18. Lifetime в C API

В C API объект нужно освободить явно:

```cpp
wgpuInstanceRelease(instance);
```

После этого завершаем GLFW:

```cpp
glfwTerminate();
```

Порядок сейчас простой:

```text
initialize GLFW
    ↓
create WGPUInstance
    ↓
use instance
    ↓
release WGPUInstance
    ↓
terminate GLFW
```

Позже lifetime станет намного важнее, потому что появятся:

```text
Instance
Surface
Adapter
Device
Queue
Buffer
Texture
TextureView
Pipeline
...
```

В C API мы будем явно следить за ownership и release-функциями.

---

# 19. Сборка Debug

Из корня проекта:

```bash
cmake \
    -S . \
    -B build/debug \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug
```

Затем:

```bash
cmake --build build/debug
```

Запуск:

```bash
./build/debug/webgpu_tutorial
```

Ожидаемый вывод примерно такой:

```text
GLFW: 3.6.0 Wayland X11 GLX Null EGL OSMesa monotonic
Dawn/WebGPU: instance created successfully
```

Точная строка GLFW может немного отличаться в зависимости от того, с какими backend'ами собрана библиотека.

---

# 20. Сборка Release

Создадим отдельный build directory:

```bash
cmake \
    -S . \
    -B build/release \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release
```

Сборка:

```bash
cmake --build build/release
```

Запуск:

```bash
./build/release/webgpu_tutorial
```

Раздельные каталоги удобнее, чем постоянно менять `CMAKE_BUILD_TYPE` в одном build tree:

```text
build/
├── debug/
└── release/
```

---

# 21. Что именно было проверено для этой главы

Код выше был собран без изменений в Debug и Release.

Для Debug linker command содержал:

```text
libglfw3.a
libwebgpu_dawn.a
-ldl
-lm
-lrt
```

Программа была запущена через X11 в виртуальном display и получила:

```text
GLFW: 3.6.0 Wayland X11 GLX Null EGL OSMesa monotonic
Dawn/WebGPU: instance created successfully
```

То есть контрольная точка главы проверена не только компиляцией, но и runtime-запуском.

---

# 22. Почему мы пока не создаём окно

Можно было бы уже сейчас написать:

```cpp
GLFWwindow* window = glfwCreateWindow(...);
```

Но мы специально этого не делаем.

Причина — разделение ответственности между главами.

Сейчас мы проверяем только инфраструктуру проекта:

```text
compiler
C++20
CMake
GLFW
Dawn
static linking
```

Создание окна важно уже в контексте WebGPU Surface:

```text
GLFWwindow
    ↓
WGPUSurface
```

Поэтому окно появится в отдельной главе, где мы одновременно разберём:

- `GLFW_NO_API`;
- framebuffer size;
- `webgpu_glfw.h`;
- `wgpuGlfwCreateSurfaceForWindow()`;
- X11 / Wayland;
- resize;
- surface configuration.

Так каждая новая строка кода будет появляться тогда, когда мы понимаем, зачем она нужна.

---

# 23. Как это выглядело бы через C++ wrapper

Основная версия использует:

```cpp
#include <webgpu/webgpu.h>
```

и:

```cpp
WGPUInstanceDescriptor descriptor{};
WGPUInstance instance = wgpuCreateInstance(&descriptor);

if (instance == nullptr) {
    // error
}

wgpuInstanceRelease(instance);
```

Через C++ wrapper код выглядел бы так:

```cpp
#include <webgpu/webgpu_cpp.h>

wgpu::InstanceDescriptor descriptor{};
wgpu::Instance instance = wgpu::CreateInstance(&descriptor);

if (!instance) {
    // error
}
```

Обратите внимание: явного

```cpp
wgpuInstanceRelease(instance);
```

здесь уже нет.

`wgpu::Instance` — C++ объект-обёртка, который управляет reference counting автоматически.

При этом отдельная C++ библиотека нам не нужна.

И C API:

```cpp
#include <webgpu/webgpu.h>
```

и wrapper:

```cpp
#include <webgpu/webgpu_cpp.h>
```

в нашем случае работают поверх той же:

```text
libwebgpu_dawn.a
```

Именно поэтому в следующих главах можно будет легко показывать два варианта рядом.

---

# 24. Типичная ошибка: подключить GLFW после OpenGL

Для этого курса лучше сразу придерживаться стабильного порядка includes:

```cpp
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <webgpu/webgpu.h>
```

Не нужно добавлять:

```cpp
#include <GL/gl.h>
```

или создавать OpenGL context.

Наша графическая API-модель — WebGPU.

GLFW используется только как platform/window/input layer.

---

# 25. Типичная ошибка: считать успешную компиляцию успешной линковкой

Такой код:

```cpp
#include <webgpu/webgpu.h>

int main() {
    return 0;
}
```

может успешно скомпилироваться даже при неправильном подключении Dawn library.

Почему?

Потому что compiler увидел только объявления из header'а. Ему не понадобилось искать реализацию ни одной WebGPU-функции.

Поэтому наш smoke-test действительно вызывает:

```cpp
wgpuCreateInstance(...)
```

И только после успешной линковки и запуска мы считаем dependency setup рабочим.

---

# 26. Типичная ошибка: всегда брать Dawn `main`

Для экспериментов нормально регулярно обновлять Dawn.

Для туториала — нет.

Если учебный код сегодня написан для одного commit, а читатель завтра получает другой, могут измениться:

- структуры;
- enum values;
- callback signatures;
- CMake targets;
- extension descriptors;
- lifetime rules;
- helper APIs.

Поэтому обновление Dawn в этом проекте должно быть отдельным осознанным действием:

```text
1. выбрать новый commit
2. пересобрать Dawn
3. прогнать все главы
4. исправить несовместимости
5. обновить Tested with
```

А не результатом случайного нового `git clone main`.

---

# 27. Контрольная точка главы

После этой главы структура проекта должна выглядеть примерно так:

```text
webgpu-tutorial/
├── .modules/
│   ├── dawn/
│   │   ├── DAWN_COMMIT
│   │   ├── include/
│   │   └── lib/
│   │
│   └── glfw3/
│       ├── include/
│       └── lib/
│
├── build/
│   ├── debug/
│   └── release/
│
├── src/
│   └── main.cpp
│
└── CMakeLists.txt
```

А программа должна вывести две диагностические строки:

```text
GLFW: ...
Dawn/WebGPU: instance created successfully
```

Если это работает, у нас есть базовый проект, поверх которого уже можно изучать сам WebGPU API.

---

# 28. Что дальше

В следующей главе разберём то, что здесь намеренно использовали почти как чёрный ящик:

```cpp
WGPUInstanceDescriptor descriptor{};
WGPUInstance instance = wgpuCreateInstance(&descriptor);
```

Нас будут интересовать:

- что такое `WGPUInstance`;
- какие возможности Instance может требовать;
- `WGPUFuture`;
- `WGPUFutureWaitInfo`;
- `wgpuInstanceWaitAny()`;
- `wgpuInstanceProcessEvents()`;
- `WGPUCallbackMode`;
- почему современный Dawn-код заметно отличается от callback-кода в старых WebGPU tutorials;
- как устроен `WGPUStringView`;
- как безопасно передавать userdata в callback.

После этого мы будем готовы асинхронно запросить первый `WGPUAdapter`.
