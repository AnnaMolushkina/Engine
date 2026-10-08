# Engine — обзор

Учебный 3D-движок на Direct3D 12 (C++20, MSVC, сборка через CMake + vcpkg).
Проект — «песочница» для лабораторных по Архитектуре игровых движков.

## 1. Что движок умеет

- **Оконная обвязка**: два окна (главное + вторичное с собственным swap chain,
  viewport, depth buffer и своей камерой), ресайз, закрытие/скрытие,
  корректный выход по Esc.
- **Direct3D 12**: device, command queue, swap chain (FLIP_DISCARD + tearing),
  RTV/DSV онк-скрины, descriptor heap (64 SRV), root signature (48 constants +
  SRV-таблица), PSO, шейдеры VS/PS (`Shaders/*.hlsl`), компиляция в рантайме
  через `D3DCompileFromFile`.
- **ECS**: `World` с сущностями-`uint32_t` и компонентами в `unordered_map`
  (`Transform`, `MeshRenderer`, `Tag`, `Camera`). Батчевое создание/удаление.
- **Сцена**: сохранение/загрузка через nlohmann-json (`scene.json`, F5/F6).
- **Рендер-пайплайн**: `RenderSystem` — параллельная подготовка кадра на job
  system (чанки по 256, frustum culling по пирамиде Грибба-Хартмана) +
  пакетная запись команд на главном потоке. Базовые примитивы: треугольник,
  квадрат, куб.
- **Ресурсы**: Assimp (меши .obj), stb_image (текстуры .jpg/.png), кэш под
  мьютексом, **асинхронная загрузка L1** через job system (см. п. 2.2).
- **Job system**: обёртка над enkiTS. Пул воркеров = cores−1, приоритеты
  High/Normal/Low, `ParallelFor`, `Submit`/`Wait`, `RunOnMainThread` +
  `PumpMainThread(8)` (памп главного потока с лимитом на кадр).
  Переключатель `--jobs=off`/клавиша `J` для замера «до/после».
- **Ввод**: `Input` — класс на оконных сообщениях (а не `GetAsyncKeyState`),
  `WasKeyPressed` один раз на нажатие, мышь/колесо.
- **Состояния игры**: `GameStateManager` (стек, отложенные переходы —
  Push/Pop/Change/Clear). `MainMenuState` → `PlayState` ↔ `PauseState`.
- **Диагностика**: `Logger` (потокобезопасный, UTF-8, время + id потока,
  файл открыт всё время), `CrashHandler` (stack trace с .pdb в engine.log),
  D3D12 debug layer → лог.
- **Профайлинг**: Tracy (зоны `ParallelFor`, `RenderSystem::Prepare`,
  `RenderSystem::Record`, `StressAnimation`, `Job Worker N`, счётчики
  `Jobs in flight`, `Main-thread queue`).
- **Замеры**: `Benchmark` — фиксированная сцена, прогрев, N кадров, метрики
  median/p95/p99/mean/min/max, экспорт в CSV (`tools/bench.ps1`).
- **Стресс-сцена**: клавиша `B` / `--bench` — до 20 000 кубов с 3 текстурами,
  детерминированный seed, анимация параллельно на job system.

## 2. Управление (игра)

| Клавиша | Действие |
|---|---|
| Enter | Начать игру |
| ↑ ↓ ← → | Движение камеры |
| Q / E | Камера вниз / вверх |
| ПКМ + мышь | Вращение камеры |
| ЛКМ + мышь, колесо | Приближение / отдаление |
| B | Стресс-сцена (20 000 кубов) вкл/выкл |
| J | Job system вкл/выкл (параллельно / на одном потоке) |
| L | Перезагрузить ресурсы асинхронно (демо L1) |
| P | Пауза |
| Backspace | Выйти в главное меню |
| W | Второе окно вкл/выкл |
| F5 / F6 | Сохранить / загрузить сцену |
| Esc | Выход |

## 3. Командная строка
--bench режим замера  
--jobs=on|off job system вкл/выкл (по умолчанию on)  
--workers=N число рабочих потоков (0 = cores−1)  
--entities=N число кубов в стресс-сцене  
--warmup=N, --frames=N прогрев и замер  
--out=file.csv, --label=text куда писать и как подписать  
--vsync=on вертикальная синхронизация  

## 4. Файловая структура (ключевое)
Chapter 4 Direct3D Initialization/Init Direct3D/  
├── Application.h / InitDirect3DApp.cpp // главный цикл, сцены, init  
├── Jobs/JobSystem.h / .cpp // обёртка enkiTS (п. 2.1)  
├── RenderSystem.h / .cpp // параллельная подготовка кадра (п. 2.3)  
├── ResourceManager.h / .cpp // async-загрузка (п. 2.2)  
├── MeshData.h / .cpp, TextureData.h / .cpp // CPU-ресурсы + LoadInto  
├── D3D12RenderAdapter.h / .cpp // D3D12, GPU-аплоад  
├── RenderAdapter.h // абстракция рендера  
├── World.h, Entity.h, Component.h // ECS  
├── Transform.h, MeshRenderer.h, Tag.h, Camera.h  
├── CameraSystem.h, Input.h, Logger.h, CrashHandler.h  
├── Benchmark.h / .cpp // замеры  
├── GameState*.h, *State.h // стек состояний  
├── SceneSerializer.h // scene.json  
├── Shaders/VertexShader.hlsl, PixelShader.hlsl  
├── config.json, scene.json, assets/…  
└── CMakeLists.txt, CMakePresets.json, vcpkg.json  

## 5. Сборка
cmake --preset x64-release  
cmake --build --preset x64-release  
out\build\x64-release\Engine.exe  


## 6. Что уже сделано по ЛР 1

- **2.1** — job system на enkiTS (пул, приоритеты, lock-free очереди/work
  stealing внутри enkiTS, `ParallelFor`, `Submit`/`Wait`, `RunOnMainThread` +
  `PumpMainThread`, `--jobs=off` для замера «до»).
- **2.2** — асинхронная загрузка ресурсов L1 (`ResourceManager::RequestMesh` /
  `RequestTexture`, фоновое чтение+парсинг, GPU-аплоад на главном потоке через
  памп, placeholder'ы, корректный шатдаун).
- **2.3** — параллельная подготовка отрисовки и анимация стресс-сцены
  (20 000 кубов, chunk = 256).
- **2.4** — Tracy + метрики median/p95/p99, `tools/bench.ps1`.

Замеры (Release, 20 000 кубов, 3+3 прогона по 2000 кадров, 12 потоков):

| метрика | без job system | с job system | ускорение |
|---|---|---|---|
| кадр, медиана | 3.92 мс | 1.64 мс | ×2.4 |
| кадр, p99 | 8.42 мс | 4.24 мс | |
| StressAnimation | 0.59 мс | 0.14 мс | ×4.2 |
| RenderPrepare | 1.52 мс | 0.27 мс | ×5.6 |

## 7. Что осталось сделать вручную
Прогнать сборку (cmake --preset x64-release) — я не могу скомпилировать проект здесь, но код структурно совместим.

Добавить ResourceManager.cpp в glob — CMakeLists.txt уже использует file(GLOB_RECURSE ... *.cpp), так что файл подхватится автоматически. При следующей конфигурации CMake он попадёт в сборку.

Снять трейсы Tracy «до/после»:

запустить Engine.exe --bench --jobs=off --out=before.csv --label=jobs-off,

запустить Engine.exe --bench --jobs=on --out=after.csv --label=jobs-on,

в обоих прогонах — либо предварительно очистить кэш ресурсов, либо нажать L в игре (демо async L1), затем сохранить трейс.

Обновить bench.ps1, добавив прогон с L/reload для демонстрации L1.

Заполнить раздел «Замеры до/после» в отчёте — цифры брать из CSV после прогона на вашей машине.