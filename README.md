# Engine

Учебный движок на DirectX 12 (ECS, Assimp, текстуры, job system на enkiTS, профилирование Tracy).

Проект лежит в папке [`Engine--3/Engine-main`](Engine--3/Engine-main). Как открыть, собрать и запустить, описано в [`Engine--3/Engine-main/README.md`](Engine--3/Engine-main/README.md).

Коротко: Visual Studio 2022/2026 → «Файл → Открыть → Папка» → `Engine--3/Engine-main` → конфигурация x64 Debug/Release → Engine.exe → F5.
Библиотеки (Assimp, glm, stb, nlohmann-json, enkiTS, Tracy) ставит vcpkg автоматически, ручная настройка путей больше не нужна.

Что изменилось в ЛР1 (Job System) и какие есть кнопки: [`CHANGES.md`](Engine--3/Engine-main/CHANGES.md) / [`CHANGES.pdf`](Engine--3/Engine-main/CHANGES.pdf).
