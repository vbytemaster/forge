---
name: create-library
description: Use when creating, extending, refactoring, or reviewing a library under libraries/, including file layout, module/header/source placement, file naming, and structure lint gates.
---

# Skill: Library Structure & File Layout

**Назначение:** правила физической структуры новой библиотеки в `libraries/` —
расположение файлов, именование, расширения, разбиение на `.cpp`. Про **структуру**,
не про содержимое.

**Когда применять:** создание новой библиотеки или добавление файла в существующую;
ревью структуры либы; нормализация «поехавшего» стиля.

## Каноническая структура
```
libraries/<lib>/                         # или <group>/<lib>/
  CMakeLists.txt
  include/<ns_root>/<lib_path>/
    <component>.cppm                      # публичный модуль-интерфейс (1 на компонент)
    <name>.hpp                           # публичный НЕ-модульный хедер — редко (см. R1)
  details/
    <component>.hxx                      # приватный хедер
  <component>.cpp                        # реализация (в корне либы)
```

Три и только три места для файлов: **`include/`** (публичный интерфейс),
**`details/`** (приватные хедеры), **корень либы** (реализация).

## Расширения = семантика (строго)
| Расширение | Где лежит | Значение |
|---|---|---|
| `.cppm` | `include/<ns_root>/<lib_path>/` | публичный модуль-интерфейс |
| `.hpp` | `include/<ns_root>/<lib_path>/` | публичный **не**-модульный хедер (только макросы / экспорт-шаблоны) |
| `.hxx` | `details/` | приватный хедер |
| `.cpp` | корень либы | реализация |

Расширение однозначно говорит: публичное это или приватное, модуль или нет, интерфейс
или реализация. Структура самодокументируема.

## Правила

**R1. Три локации, ничего вне них.**
- Публичный интерфейс — только в `include/<ns_root>/<lib_path>/`.
- `.hpp` (публичный не-модульный хедер) — только когда модуль `.cppm` не может выразить
  (макросы, экспортируемые шаблоны). По умолчанию — `.cppm`. `.hpp` — исключение.
- Приватные хедеры — только в `details/` и только `.hxx`.
- Реализация (`.cpp`) — только в корне либы. Не в `include/`, не в `details/`, не в `src/`.

**R2. Парный инвариант.**
Каждый `X.cpp` в корне парен по базовому имени **ровно с одним** хедером:
- `include/<ns_root>/<lib_path>/X.cppm` — если реализует публичный компонент, **или**
- `details/X.hxx` — если это приватная реализация.

Не оба сразу, не ни одного. Декларации, которые реализует `.cpp`, живут в одном месте.

**R3. Header-only компонент — норма.**
`X.cppm` (или `details/X.hxx`) без парного `.cpp` допустим, если компонент состоит только
из деклараций (типы, перечисления, исключения, опции, чистые шаблоны).

**R4. Приватные внутренности → `details/X.hxx`.**
Внутренние хелперы/impl-хедеры идут в `details/` как `.hxx` (+ парный `X.cpp` в корне).
**Никогда** в корень либы, **никогда** `_X.hpp`, **никогда** `.hpp` для приватного.

**R5. Разбиение большого `.cpp`.**
Если реализация компонента велика — дробить как `X.cpp` + `X_<aspect>.cpp`
(напр. `parser.cpp` + `parser_simd.cpp`). Все части реализуют **одни и те же** декларации
(`X.cppm` или `details/X*.hxx`). Единый суффикс `_<aspect>`; не `_`-префикс, не разнобой.

**R6. Путь `include/` зеркалит namespace.**
`include/<ns_root>/<lib_path>/` соответствует namespace `<ns_root>::<lib_path>`.

**R7. Имя файла = имя сущности дословно.**
Базовое имя файла повторяет имя основной сущности, которую файл объявляет/реализует —
**включая суффиксы** (`_impl` и т.п.) и с маппингом вложенности `:: → _`. Не сокращать,
не опускать суффикс.
- `<component>::impl` → `details/<component>_impl.hxx` (+ `<component>_impl.cpp`).
  **Не** `<component>.hxx`.
- плоский тип `<name>_impl` → `<name>_impl.hxx` / `<name>_impl.cpp`. **Не** `<name>.hxx`.
- Следствие: публичный `<component>` (`<component>.cppm`) и приватный `<component>::impl`
  (`details/<component>_impl.hxx`) — **разные файлы**, имена не схлопываются.

## Куда положить новый файл (решение)
1. Публичный интерфейс? → `include/<ns_root>/<lib_path>/<entity>.cppm`
   (макросы/шаблоны, не выразимые модулем → `<entity>.hpp` там же).
2. Приватный хедер (видим только внутри либы)? → `details/<entity>.hxx`.
3. Реализация? → корень: `<entity>.cpp`, парный по имени с (1) или (2).
4. Реализация большая? → дробить `<entity>_<aspect>.cpp` (R5).

Во всех случаях `<entity>` — **дословное** имя сущности с `:: → _` (R7).

## Anti-pattern (признаки «поехавшего» стиля)
- Приватный хедер в **корне** либы (вместо `details/`).
- `.hpp` использован для **приватного** хедера (должен быть `.hxx` в `details/`).
- Приватные хедеры названы вразнобой: `_name.hpp` (префикс) **и** `name_backend.hpp`
  (суффикс) — должен быть единый `details/<name>.hxx`.
- **Опущен суффикс/квалификатор в имени файла:** `plugin.hxx` для класса `plugin::impl`
  (должно быть `plugin_impl.hxx`); `<name>.hxx` для типа `<name>_impl`.
- `.cppm` лежит вне `include/`; `.cpp` лежит в `include/` или `details/`.
- `.cpp` без парного хедера (декларации нигде или только инлайн в `.cpp`).
- `details/` есть в одних либах и нет в других при наличии приватных хедеров.

## Линт-гейты (grep, для скилла/CI)
```sh
LIB=libraries/<lib>
# приватные хедеры только в details/
find "$LIB" -name '*.hxx' -not -path '*/details/*'      # → пусто
# в details/ только .hxx (не .hpp)
find "$LIB" -path '*/details/*' -name '*.hpp'           # → пусто
# публичные модули только в include/
find "$LIB" -name '*.cppm' -not -path '*/include/*'     # → пусто
# реализация только в корне (не в include/ или details/)
find "$LIB" \( -path '*/include/*' -o -path '*/details/*' \) -name '*.cpp'   # → пусто
# приватных хедеров в корне нет
find "$LIB" -maxdepth 1 \( -name '*.hpp' -o -name '*.hxx' \)                 # → пусто
# каждый X.cpp имеет пару: include/.../X.cppm или details/X.hxx (по базовому имени)
```

## Пример
```
libraries/widget/
  CMakeLists.txt
  include/acme/widget/
    widget.cppm        # публичный модуль — интерфейс         ↔ widget.cpp (+ widget_simd.cpp)
    types.cppm         # публичный, header-only               ↔ (нет .cpp)
    macros.hpp         # публичный не-модульный (макросы)      ↔ (нет .cpp)
  details/
    widget_impl.hxx    # приватный класс widget::impl          ↔ widget_impl.cpp   (R7: не widget.hxx)
    cache.hxx          # приватный хелпер                      ↔ cache.cpp
  widget.cpp           # реализует widget.cppm
  widget_simd.cpp      # разбиение widget.cppm (R5), суффикс _simd
  widget_impl.cpp      # реализует details/widget_impl.hxx (widget::impl)
  cache.cpp            # реализует details/cache.hxx
```
