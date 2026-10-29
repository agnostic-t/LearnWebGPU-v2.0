# Глава 02 — От `Instance` к GPU: зачем нужен Adapter и откуда берутся Futures

> Проверено: 29 октября 2026  
> **Dawn:** `40d98d8812bb3211592bb78cac8bd7b13b585e19`  
> **GLFW:** 3.6.0  
> **GCC:** 14.2.0  
> **CMake:** 3.31.6  
> **C++:** C++20  
> **Платформа проверки:** Linux amd64, GLFW/X11  
> **Основной стиль:** WebGPU C API в C++ коде

В предыдущей главе мы создали `WGPUInstance` и остановились на вопросе:

> Как WebGPU решает, какой именно GPU использовать?

На первый взгляд хочется сразу создать `WGPUDevice` и начать отправлять команды. Но WebGPU намеренно разделяет **поиск подходящего графического устройства** и **создание логического устройства, с которым будет работать приложение**.

Именно поэтому между `Instance` и `Device` появляется `Adapter`.

В этой главе мы впервые попросим WebGPU найти adapter. По дороге столкнёмся с современной asynchronous model Dawn: callback'ами, `WGPUFuture`, callback modes и `wgpuInstanceWaitAny()`.

Цель главы не просто заставить `RequestAdapter` сработать, а понять, **зачем API устроен именно так**.

---

## 1. Почему WebGPU не создаёт `Device` сразу

Представим обычный ноутбук. В нём может быть несколько графических устройств:

```text
Integrated GPU
Discrete GPU
Software renderer
```

Даже если физический GPU один, WebGPU implementation может работать через разные backend'ы или предоставлять разные варианты adapter'а.

Приложение не должно строить свою архитектуру вокруг конкретной строки вроде:

```text
"AMD Radeon ..."
```

Вместо этого WebGPU вводит абстракцию **adapter**.

Упрощённо:

```text
physical / logical graphics implementation
                │
                ▼
          WGPUAdapter
```

Adapter — это найденный WebGPU кандидат, который описывает графическую реализацию, доступные features и limits и из которого позже можно запросить `WGPUDevice`.

Поэтому путь к GPU выглядит так:

```text
WGPUInstance
     │
     │ RequestAdapter
     ▼
WGPUAdapter
     │
     │ RequestDevice
     ▼
WGPUDevice
     │
     └── WGPUQueue
```

В этой главе остановимся на `WGPUAdapter`. Device и Queue будут следующей ступенью.

---

## 2. Тогда зачем нужен `WGPUInstance`?

Теперь роль instance становится понятнее.

`WGPUInstance` существует **до выбора конкретного adapter**. Это объект уровня WebGPU implementation, через который мы можем:

- создавать surface;
- искать adapter;
- обслуживать asynchronous events;
- ожидать futures;
- настраивать instance-level возможности.

То есть можно представить его так:

```text
               WGPUInstance
              /            \
             /              \
            ▼                ▼
       WGPUSurface      WGPUAdapter
                              │
                              ▼
                         WGPUDevice
```

Позже surface поможет adapter selection учитывать окно, на которое мы хотим выводить изображение. Пока окна у нас ещё нет, поэтому запросим любой доступный adapter.

---

## 3. Почему `RequestAdapter` — не обычная функция, возвращающая adapter

Если бы API был полностью синхронным, можно было бы ожидать что-то такое:

```cpp
WGPUAdapter adapter = wgpuGetAdapter(instance);
```

Но современный WebGPU устроен иначе:

```cpp
WGPUFuture future = wgpuInstanceRequestAdapter(
    instance,
    &options,
    callbackInfo
);
```

Сам adapter приходит позже через callback.

Почему?

Потому что WebGPU проектировался как API, в котором часть операций потенциально не обязана завершаться мгновенно. Поиск adapter, создание device, mapping buffer и другие действия могут проходить через implementation, driver или иной execution environment.

Особенно важно, что одна и та же модель WebGPU должна работать не только в нашей нативной программе, но и в средах, где блокирующее ожидание вообще нежелательно.

Поэтому API не обещает:

> результат готов к моменту возврата функции.

Вместо этого он говорит:

> операция запущена; вот объект, по которому можно наблюдать её завершение, и callback, куда придёт результат.

На нативном Dawn конкретная операция иногда завершается очень быстро. Но код не должен зависеть от этого.

---

## 4. Две части одной асинхронной операции

Вызов `RequestAdapter` содержит две связанные, но разные идеи.

### Callback — куда придёт результат

Например:

```cpp
void onAdapterRequestEnded(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    WGPUStringView message,
    void* userdata1,
    void* userdata2
);
```

Именно сюда WebGPU передаст:

```text
успех / ошибку
полученный adapter
диагностическое сообщение
```

### Future — за чем мы наблюдаем

Сама функция возвращает:

```cpp
WGPUFuture future = wgpuInstanceRequestAdapter(...);
```

`future` не содержит adapter. Он представляет **незавершённую или уже завершившуюся операцию**, за которой можно следить через event system instance.

Полная модель выглядит так:

```text
RequestAdapter()
      │
      ├── возвращает WGPUFuture
      │
      └── когда результат становится наблюдаемым
                │
                ▼
             callback
                │
                ▼
             adapter
```

Это различие важно. Future отвечает на вопрос «завершилась ли операция?», а callback доставляет её результат.

---

## 5. Но мы пока хотим линейный код

Для игрового или интерактивного приложения асинхронная модель полезна, но во время начальной инициализации нам гораздо легче читать такой control flow:

```text
создать instance
попросить adapter
дождаться результата
проверить ошибку
продолжить
```

а не размазывать initialization между несколькими callback'ами.

Современный WebGPU C API позволяет выбрать, **когда callback имеет право выполниться**. Для этого существует `WGPUCallbackMode`.

В этой серии на этапе initialization мы будем использовать:

```cpp
WGPUCallbackMode_WaitAnyOnly
```

Смысл режима:

> callback этой операции будет наблюдаться нами через `wgpuInstanceWaitAny()`.

Тогда асинхронный API можно аккуратно встроить в последовательную инициализацию:

```text
RequestAdapter()
      │
      ▼
 WGPUFuture
      │
      ▼
WaitAny()
      │
      ▼
 callback
      │
      ▼
продолжаем main()
```

Мы не превращаем операцию в синхронную внутри WebGPU. Мы просто **синхронно ждём асинхронную операцию на своей стороне**.

---

## 6. Почему нужен timeout

Самый простой вариант ожидания — ждать бесконечно. Но это плохая диагностика.

Если из-за ошибки driver/implementation операция не завершится, программа просто зависнет на старте без объяснения.

Поэтому мы хотим сказать:

> подожди adapter, но не дольше пяти секунд.

В WebGPU это выглядит так:

```cpp
wgpuInstanceWaitAny(
    instance,
    1,
    &waitInfo,
    timeoutNS
);
```

Ненулевой timeout относится к instance feature:

```cpp
WGPUInstanceFeatureName_TimedWaitAny
```

То есть прежде чем пользоваться timed wait, мы явно спрашиваем implementation, поддерживает ли она эту возможность.

---

## 7. Первый шаг: проверяем `TimedWaitAny`

Добавим:

```cpp
constexpr WGPUInstanceFeatureName requiredFeature =
    WGPUInstanceFeatureName_TimedWaitAny;

if (wgpuHasInstanceFeature(requiredFeature) != WGPU_TRUE) {
    std::cerr << "TimedWaitAny is not supported\n";
    return EXIT_FAILURE;
}
```

Здесь полезно заметить архитектурную деталь.

Мы ещё **не создали instance**, поэтому проверяем feature функцией:

```cpp
wgpuHasInstanceFeature(...)
```

То есть спрашиваем саму WebGPU implementation:

> можешь ли ты создать instance с такой возможностью?

Если да, включаем feature при создании:

```cpp
WGPUInstanceDescriptor instanceDescriptor =
    WGPU_INSTANCE_DESCRIPTOR_INIT;

instanceDescriptor.requiredFeatureCount = 1;
instanceDescriptor.requiredFeatures = &requiredFeature;

WGPUInstance instance =
    wgpuCreateInstance(&instanceDescriptor);
```

В первой главе descriptor был практически пустым. Теперь видно, зачем он существует: через него мы описываем требования к создаваемому объекту.

---

## 8. Почему теперь используем `WGPU_..._INIT`

В первой главе мы писали:

```cpp
WGPUInstanceDescriptor descriptor{};
```

Начиная отсюда лучше привыкнуть к initializer macros из `webgpu.h`:

```cpp
WGPUInstanceDescriptor instanceDescriptor =
    WGPU_INSTANCE_DESCRIPTOR_INIT;
```

WebGPU structures не всегда имеют правильные API-defaults при простом заполнении нулями. В разных структурах встречаются специальные значения вроде `Undefined`, `WGPU_STRLEN` и другие sentinels.

Поэтому практическое правило будет таким:

> Если для WebGPU structure существует `WGPU_..._INIT`, начинаем с него, а затем меняем нужные поля.

Это делает код устойчивее к деталям конкретной структуры.

---

## 9. Проверяем limit для timed wait

Implementation также сообщает, сколько futures можно одновременно передать в timed `WaitAny`.

```cpp
WGPUInstanceLimits instanceLimits =
    WGPU_INSTANCE_LIMITS_INIT;

if (wgpuGetInstanceLimits(&instanceLimits) !=
    WGPUStatus_Success) {
    std::cerr << "Failed to query instance limits\n";
    return EXIT_FAILURE;
}
```

После этого можно посмотреть:

```cpp
std::cout
    << "TimedWaitAny max future count: "
    << instanceLimits.timedWaitAnyMaxCount
    << '\n';
```

В нашей проверенной сборке Dawn значение равно `64`.

Почему мы вообще говорим о limit сейчас, если ждём только один future? Чтобы увидеть ещё один общий принцип WebGPU:

> capabilities не следует угадывать или хардкодить, если implementation умеет сообщить их сама.

Позже этот принцип станет гораздо важнее при работе с adapter/device limits.

---

## 10. Готовим место, куда callback положит результат

Обычная C++ функция могла бы вернуть объект через `return`:

```cpp
WGPUAdapter getAdapter();
```

Callback так не работает. Его вызовет WebGPU, а наш `main()` должен получить результат каким-то другим способом.

Создадим небольшую структуру состояния:

```cpp
struct AdapterRequestState {
    WGPURequestAdapterStatus status =
        WGPURequestAdapterStatus_Error;

    WGPUAdapter adapter = nullptr;
    std::string message;
};
```

Это просто «почтовый ящик» между callback и кодом инициализации.

Callback заполнит его:

```cpp
void onAdapterRequestEnded(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    WGPUStringView message,
    void* userdata1,
    void* /*userdata2*/
) {
    auto* state =
        static_cast<AdapterRequestState*>(userdata1);

    state->status = status;
    state->adapter = adapter;
    // message скопируем ниже
}
```

А `main()` после завершения `WaitAny()` прочитает эти поля.

---

## 11. Зачем существует `userdata`

C callback не захватывает локальные переменные так, как C++ lambda.

Поэтому API предоставляет:

```cpp
userdata1
userdata2
```

Мы передаём pointer на своё состояние:

```cpp
AdapterRequestState requestState;

callbackInfo.userdata1 = &requestState;
```

а callback получает тот же pointer обратно:

```cpp
auto* state =
    static_cast<AdapterRequestState*>(userdata1);
```

Схема выглядит так:

```text
main()
 │
 ├── создаёт AdapterRequestState
 │
 ├── передаёт &state в userdata1
 │
 └── RequestAdapter()
          │
          ▼
       callback
          │
          └── записывает результат в тот же state
```

Здесь сразу возникает вопрос lifetime.

`requestState` находится на stack `main()`, но мы не выходим из его scope до завершения `WaitAny()`. Поэтому pointer остаётся валиден.

Если бы мы запускали операцию и возвращались из функции, не дожидаясь callback, такой подход мог бы оставить висячий pointer. Позже, когда появится более сложный asynchronous code, к lifetime нужно будет относиться особенно внимательно.

---

## 12. Почему `WGPUStringView` надо скопировать

Callback также получает:

```cpp
WGPUStringView message
```

Это view на строку, а не `std::string`, которым владеет наше приложение.

Если мы хотим сохранить сообщение после callback, его нужно скопировать.

Добавим helper:

```cpp
std::string toString(WGPUStringView view) {
    if (view.data == nullptr) {
        return {};
    }

    if (view.length == WGPU_STRLEN) {
        return std::string(view.data);
    }

    return std::string(view.data, view.length);
}
```

И в callback:

```cpp
state->message = toString(message);
```

Это небольшой пример более общего правила: API view обычно описывает чужую память. Не нужно автоматически считать, что она останется валидной столько же, сколько наш C++ объект.

---

## 13. Создаём callback info

Теперь собираем всё вместе:

```cpp
AdapterRequestState requestState;

WGPURequestAdapterCallbackInfo callbackInfo =
    WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;

callbackInfo.mode =
    WGPUCallbackMode_WaitAnyOnly;

callbackInfo.callback =
    onAdapterRequestEnded;

callbackInfo.userdata1 =
    &requestState;
```

Здесь `callbackInfo` отвечает на два разных вопроса:

```text
КУДА положить результат?  -> callback + userdata
КОГДА callback разрешён?  -> mode
```

Именно разделение этих вопросов делает современную asynchronous model более явной, чем старые варианты Dawn API с одним callback-параметром.

---

## 14. Наконец запускаем `RequestAdapter`

Options пока оставим default:

```cpp
WGPURequestAdapterOptions adapterOptions =
    WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
```

И запускаем операцию:

```cpp
WGPUFuture future =
    wgpuInstanceRequestAdapter(
        instance,
        &adapterOptions,
        callbackInfo
    );
```

Очень важно прочитать эту строку правильно.

Она **не означает**:

```text
adapter получен
```

Она означает:

```text
запрос adapter запущен
```

Наш `requestState.adapter` пока нельзя использовать.

Зато у нас есть `future`, который можно передать системе ожидания.

---

## 15. Превращаем future в объект ожидания

`WaitAny()` работает не напрямую с `WGPUFuture`, а с `WGPUFutureWaitInfo`:

```cpp
WGPUFutureWaitInfo waitInfo =
    WGPU_FUTURE_WAIT_INFO_INIT;

waitInfo.future = future;
```

После этого вызываем:

```cpp
constexpr uint64_t kAdapterWaitTimeoutNs =
    5'000'000'000ULL;

const WGPUWaitStatus waitStatus =
    wgpuInstanceWaitAny(
        instance,
        1,
        &waitInfo,
        kAdapterWaitTimeoutNs
    );
```

Аргументы читаются довольно естественно:

```text
instance        -> чью event system обслуживаем
1               -> ждём один future
&waitInfo       -> какой именно future
5 секунд        -> максимум ожидания
```

После возврата нам нужно проверить **два уровня результата**.

---

## 16. Почему есть два разных status

Первый status:

```cpp
WGPUWaitStatus waitStatus
```

отвечает на вопрос:

> Удалось ли корректно выполнить само ожидание?

Например:

```cpp
if (waitStatus == WGPUWaitStatus_TimedOut) {
    std::cerr << "Adapter request timed out\n";
}
```

Но успешный `WaitAny()` ещё не означает, что adapter найден.

Второй status лежит в нашем callback state:

```cpp
requestState.status
```

и отвечает уже на другой вопрос:

> Чем закончилась операция RequestAdapter?

То есть возможна ситуация:

```text
WaitAny            -> Success
RequestAdapter     -> Unavailable / Error
```

Это не противоречие. Ожидание успешно дождалось операции, а сама операция закончилась неудачей.

Поэтому проверяем оба уровня:

```cpp
if (waitStatus != WGPUWaitStatus_Success ||
    waitInfo.completed != WGPU_TRUE) {
    // проблема ожидания
}

if (requestState.status !=
        WGPURequestAdapterStatus_Success ||
    requestState.adapter == nullptr) {
    // сам adapter request не удался
}
```

Такое разделение будет встречаться и в других asynchronous APIs.

---

## 17. Что насчёт других callback modes?

Сейчас мы выбрали:

```cpp
WGPUCallbackMode_WaitAnyOnly
```

потому что он хорошо подходит для линейной startup initialization.

Есть ещё два режима.

### `AllowProcessEvents`

Callback может быть обработан, когда приложение вызывает:

```cpp
wgpuInstanceProcessEvents(instance);
```

Это удобно, когда основной цикл не должен блокироваться, а приложение периодически «прокачивает» WebGPU events.

Модель:

```text
main loop
   │
   ├── glfwPollEvents()
   ├── wgpuInstanceProcessEvents()
   ├── update()
   └── render()
```

### `AllowSpontaneous`

Implementation получает больше свободы вызвать callback без того, чтобы наш код явно вошёл в `WaitAny()` или `ProcessEvents()`.

Это удобно, но сразу делает вопросы thread-safety, lifetime и reentrancy важнее.

Для первых глав нам такой уровень сложности ничего не даёт, поэтому initialization будет строиться через `WaitAnyOnly`.

Главное не запоминать enum'ы, а понять идею:

> callback mode определяет, при каких условиях WebGPU разрешено доставить callback приложению.

---

## 18. Полный `main.cpp`

`CMakeLists.txt` из первой главы менять не нужно.

Заменим `src/main.cpp`:

```cpp
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <webgpu/webgpu.h>

namespace {

constexpr uint64_t kAdapterWaitTimeoutNs =
    5'000'000'000ULL;

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
    WGPURequestAdapterStatus status =
        WGPURequestAdapterStatus_Error;

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
    auto* state =
        static_cast<AdapterRequestState*>(userdata1);

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

    std::cout
        << "GLFW: "
        << glfwGetVersionString()
        << '\n';

    constexpr WGPUInstanceFeatureName requiredFeature =
        WGPUInstanceFeatureName_TimedWaitAny;

    if (wgpuHasInstanceFeature(requiredFeature) != WGPU_TRUE) {
        std::cerr
            << "TimedWaitAny is not supported by "
               "this WebGPU implementation\n";

        glfwTerminate();
        return EXIT_FAILURE;
    }

    WGPUInstanceLimits instanceLimits =
        WGPU_INSTANCE_LIMITS_INIT;

    if (wgpuGetInstanceLimits(&instanceLimits) !=
        WGPUStatus_Success) {

        std::cerr
            << "Failed to query WebGPU instance limits\n";

        glfwTerminate();
        return EXIT_FAILURE;
    }

    std::cout
        << "TimedWaitAny max future count: "
        << instanceLimits.timedWaitAnyMaxCount
        << '\n';

    WGPUInstanceDescriptor instanceDescriptor =
        WGPU_INSTANCE_DESCRIPTOR_INIT;

    instanceDescriptor.requiredFeatureCount = 1;
    instanceDescriptor.requiredFeatures = &requiredFeature;

    WGPUInstance instance =
        wgpuCreateInstance(&instanceDescriptor);

    if (instance == nullptr) {
        std::cerr << "Failed to create WebGPU instance\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    AdapterRequestState requestState;

    WGPURequestAdapterCallbackInfo callbackInfo =
        WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;

    callbackInfo.mode =
        WGPUCallbackMode_WaitAnyOnly;

    callbackInfo.callback =
        onAdapterRequestEnded;

    callbackInfo.userdata1 =
        &requestState;

    WGPURequestAdapterOptions adapterOptions =
        WGPU_REQUEST_ADAPTER_OPTIONS_INIT;

    WGPUFuture future =
        wgpuInstanceRequestAdapter(
            instance,
            &adapterOptions,
            callbackInfo
        );

    WGPUFutureWaitInfo waitInfo =
        WGPU_FUTURE_WAIT_INFO_INIT;

    waitInfo.future = future;

    const WGPUWaitStatus waitStatus =
        wgpuInstanceWaitAny(
            instance,
            1,
            &waitInfo,
            kAdapterWaitTimeoutNs
        );

    if (waitStatus == WGPUWaitStatus_TimedOut) {
        std::cerr << "Adapter request timed out\n";

        wgpuInstanceRelease(instance);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    if (waitStatus != WGPUWaitStatus_Success ||
        waitInfo.completed != WGPU_TRUE) {

        std::cerr
            << "Failed while waiting for adapter request\n";

        wgpuInstanceRelease(instance);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    if (requestState.status !=
            WGPURequestAdapterStatus_Success ||
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

    std::cout
        << "Adapter request completed successfully\n";

    wgpuAdapterRelease(requestState.adapter);
    wgpuInstanceRelease(instance);
    glfwTerminate();

    return EXIT_SUCCESS;
}
```

---

## 19. Прочитаем программу как историю, а не как набор API calls

Теперь весь код можно пересказать обычным языком:

```text
1. Инициализировать GLFW.

2. Спросить Dawn:
   поддерживается ли timed ожидание futures?

3. Создать WebGPU instance с этой возможностью.

4. Подготовить место, куда придёт результат поиска adapter.

5. Попросить WebGPU найти adapter.
   В ответ сразу получить не adapter, а future.

6. Сказать instance:
   подожди этот future максимум пять секунд.

7. Во время ожидания WebGPU вызывает наш callback,
   а callback сохраняет adapter в requestState.

8. Проверить сначала результат ожидания,
   затем результат самого RequestAdapter.

9. Adapter получен.
```

Если эта последовательность понятна, большая часть синтаксиса вокруг callback info уже перестаёт выглядеть случайным boilerplate.

---

## 20. Как выглядела бы та же идея через C++ wrapper

В C++ API типы меняются на namespace `wgpu`:

```cpp
wgpu::Instance
wgpu::Adapter
wgpu::Future
```

И вызовы становятся методами/обёртками, например концептуально:

```cpp
wgpu::Future future =
    instance.RequestAdapter(&options, callbackInfo);
```

C++ wrapper уменьшает количество явных `Release()` и делает типы удобнее для C++ кода.

Но принцип остаётся тем же:

```text
Instance
   │
   └── RequestAdapter
           │
           ├── Future
           └── Callback -> Adapter
```

Именно поэтому в основной части мы сначала изучаем C API: wrapper меняет форму записи, но не архитектуру WebGPU.

---

## 21. Что мы пока намеренно не изучаем про Adapter

Мы уже получили `WGPUAdapter`, но сразу его освобождаем:

```cpp
wgpuAdapterRelease(requestState.adapter);
```

Это может показаться странным: зачем было столько кода, чтобы тут же уничтожить результат?

Потому что в этой главе adapter понадобился нам как **первая настоящая asynchronous operation**.

Мы пока не разбирали:

- какой backend выбран;
- какой это GPU;
- integrated он или discrete;
- какие features поддерживает;
- какие limits доступны;
- как из него получить device.

Если добавить всё это сейчас, модель Future смешалась бы с новой большой темой GPU capabilities.

В следующей главе adapter уже станет главным объектом. Мы не просто получим его, а спросим:

> Что именно Dawn выбрал для нас и какие возможности этот adapter даёт приложению?

После этого из adapter создадим `WGPUDevice` и получим `WGPUQueue` — то есть впервые дойдём до объектов, через которые приложение действительно будет создавать ресурсы и отправлять работу GPU.
