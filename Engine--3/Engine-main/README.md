# Engine — учебный движок на DirectX 12

C++17, DirectX 12 (фреймворк Ф. Луны в `Common/`), ECS, загрузка моделей (Assimp) и текстур (stb_image), job system (**enkiTS**), профилирование (**Tracy**).
Все библиотеки ставит **vcpkg**: вручную ничего качать и никаких путей в свойствах проекта прописывать не нужно.

## Как открыть и запустить

Нужна **Visual Studio 2022 или 2026** с нагрузкой «Разработка классических приложений на C++». В неё уже входят CMake и vcpkg.

1. **Файл → Открыть → Папка…** → выбрать эту папку (где лежит `CMakeLists.txt`). Файла `.sln` больше нет.
2. Вверху выбрать конфигурацию **x64 Debug** или **x64 Release**.
3. **Первый раз** конфигурация займёт 5–10 минут: vcpkg скачает и соберёт Assimp, glm, stb, nlohmann-json, enkiTS и Tracy. Дальше они берутся из кэша.
4. Цель **Engine.exe** → **F5**.

Командная строка (Developer PowerShell for VS):
```powershell
cmake --preset x64-release
cmake --build --preset x64-release
.\out\build\x64-release\Engine.exe
```
Ошибка «vcpkg.cmake не найден» означает, что не установлен компонент VS «Диспетчер пакетов vcpkg». Добавьте его через Visual Studio Installer или поставьте vcpkg вручную и задайте переменную `VCPKG_ROOT`.

## Кнопки

| Где | Кнопка | Что делает |
|---|---|---|
| везде | **W** | показать или спрятать второе окно (крутящийся квадрат) |
| везде | **F5 / F6** | сохранить / загрузить сцену (`scene.json`) |
| везде | **Esc** | выход |
| меню | **Enter** | начать игру (в меню сцена скрыта, виден только фон) |
| игра | **↑ ↓ ← →** | двигать камеру вперёд, назад, влево, вправо |
| игра | **Q / E** | камера вниз / вверх |
| игра | **ПКМ + мышь** | вращать камеру |
| игра | **ЛКМ + мышь вверх/вниз**, **колесо** | приблизить / отдалить |
| игра | **B** | стресс-сцена: 20 000 анимированных кубов (вкл/выкл) |
| игра | **J** | job system: параллельно / на одном потоке |
| игра | **P** | пауза |
| пауза | **P / Backspace** | продолжить / выйти в меню |

В заголовке окна: состояние, режим job system, число видимых и всех объектов, FPS.
**Для демо:** Enter → B → несколько раз J, глядя на FPS.

## Настройки: `config.json` (копируется рядом с exe)
```json
{
  "background_color": [0.39, 0.58, 0.93, 1.0],
  "vsync": false,
  "jobs":  { "enabled": true, "worker_threads": 0 },
  "scene": { "stress_entities": 20000, "stress_on_start": false, "seed": 1337 },
  "benchmark": { "warmup_frames": 300, "measure_frames": 2000, "output": "bench_results.csv" }
}
```
Аргументы командной строки перекрывают файл: `--bench`, `--jobs=on|off`, `--workers=N`, `--entities=N`, `--warmup=N`, `--frames=N`, `--out=файл.csv`, `--label=текст`, `--vsync=on|off`.

## Замеры до/после (п. 2.4 ЛР1)
Только Release. Скрипт кладётся рядом с exe:
```powershell
cd out\build\x64-release
powershell -ExecutionPolicy Bypass -File .\bench.ps1        # 3 прогона без job system + 3 с ней
```
Результаты: `bench_results.csv` (сырые данные) и `bench_summary.csv` (медианы по прогонам и ускорение).

## Tracy
1. Скачайте профайлер **той же версии, что в vcpkg (0.14.1)**: `windows-0.14.1.zip` на <https://github.com/wolfpld/tracy/releases/tag/v0.14.1>.
2. Запустите `Engine.exe`, в Tracy нажмите **Connect**.
3. На трейсе видно: потоки `Main thread` / `Job Worker N`, зоны `StressAnimation`, `RenderSystem::Prepare`, `RenderSystem::Record`, `ParallelFor range`, ожидания, графики `Frame time (ms)`, `Visible objects`, `Jobs in flight`.
4. Трейсы «до» (J выключен) и «после» (J включён): File → Save trace.

## Если что-то упало
Движок пишет в `engine.log` (рядом с exe) время, поток и уровень каждого сообщения. При падении туда же записывается стек вызовов. В Debug-сборке в лог попадают и ошибки D3D12 debug layer.
