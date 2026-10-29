<div align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/eliemichel/LearnWebGPU/main/images/webgpu-dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="https://raw.githubusercontent.com/eliemichel/LearnWebGPU/main/images/webgpu-light.svg">
    <img alt="Learn WebGPU Logo" src="images/webgpu-dark.svg" width="200">
  </picture>
</div>

# LearnWebGPU v2.0

Так как состояние официально продвигаемого "источника знаний" о WebGPU ([Learn WebGPU от eliemichel](https://eliemichel.github.io/LearnWebGPU/)) довольно печально (последний коммит год назад и более), то я решил сделать новый ресурс для изучения, опираясь на последние версии Dawn (Октябрь 2026 год).

Структура проекта:

1. `chapters`: Сборник глав для изучения темы
2. `code`: Исходный код, который используется в главах

Вся информация написана с помощью LLM, но проверена и протестирована на ошибки. Каждый пример обязан запускаться как минимум на Linux-дистрибутивах.

Тестирования на Windows, MacOS и Web не проводилось

## Библиотеки

Для удобства пользования обе библиотеки (GLFW и Dawn) собираются из источников скриптами в `scripts/`

```sh
chmod +x ./scripts/*.sh

./scripts/fetch-dawn.sh
./scripts/fetch-glfw3.sh

# Теперь там dawn и glfw с include/ и lib/
ls -la libs
```
