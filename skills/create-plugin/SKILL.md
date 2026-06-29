---
name: create-plugin
description: Use when creating, renaming, moving, refactoring, or reviewing an official plugin package, including plugin family, namespace, target, component, module prefix, contract id, and structure rules.
---

# Skill: Create Plugin

**Extends:** `create-library`. Плагин — это **C++ библиотека** с дополнительными
структурными правилами. Сначала применяй ВСЕ правила `create-library` (структура файлов,
расширения `.cppm/.hpp/.hxx/.cpp`, `details/`, парный инвариант, `_impl`, R1–R7). Этот
скилл **добавляет** поверх: организацию семейств, namespace/target/contract-маппинг,
публичные module-слайсы и методы проверки.

**Когда применять:** создание/переименование/перемещение плагина; ревью того, в том ли
семействе плагин и верен ли его namespace/target.

## 1. Расположение и namespace
- Плагин живёт под `plugins/<family>/<name>/` (а не `libraries/<lib>/`).
- Владеет **листовым** namespace `<ns>::plugins::<family>::<name>`.
- Структура файлов внутри — по `create-library` (`include/<ns>/plugins/<family>/<name>/*.cppm`,
  `details/*.hxx`, корневые `*.cpp`).

## 2. Детерминированный маппинг (5 форм из одного листа)
Всё выводится из `<ns>::plugins::<family>::<name>` по `:: ↔ _ ↔ .`:

| Форма | Значение |
|---|---|
| namespace | `<ns>::plugins::<family>::<name>` |
| CMake target | `<ns>_plugins_<family>_<name>` |
| package component | `plugins_<family>_<name>` |
| module-префикс | `<ns>.plugins.<family>.<name>` |
| contract id | `"<ns>.plugins.<family>.<name>"` |
| доп. contract id | суффикс: `"<ns>.plugins.<family>.<name>.<slice>"` |
| каталог / include | `plugins/<family>/<name>/` · `include/<ns>/plugins/<family>/<name>/` |

Все 5 форм обязаны соответствовать листу — рассинхрон любой из них = баг.

## 3. Правила семейства

**P1. Член = функциональная роль.** `<name>` называет, **что плагин делает/чем владеет**
(роль-существительное): `server`, `node`, `signer`, `keyvalue`. **Не** сторона контракта
(`provider`/`consumer`), **не** активность, **не** бэкенд-как-семейство.

**P2. Семейство = домен, зеркалит домен-либу.** `<family>` — домен, который плагин
обслуживает, зеркаля либу `<ns>::<family>`. **Бэкенд-как-член разрешён** (`store::sqlite`,
`db::rocksdb` — домен + бэкенд).

**P3. Группируем по тому, ЧЕМ артефакт является.** Плагин группируется по своему домену
(`plugins::<domain>::<role>`). Приложение группируется по app-hood, и т.д. — head noun
задаёт дерево.

**P4. Channel-rooted, не core-as-parent.** Если плагин/слайс **выставляет** ядро `X` по
каналу `C` — корни в канале: `C::X`, **никогда** `X::C`. Ядро, проваливающее is-a (видов
нет), — лист, не родитель (`http::api`, не `api::http`).

**P5. Промежуточный уровень пуст.** `<ns>::plugins::<family>` — пустая группировка; типы
только в листе `<ns>::plugins::<family>::<name>`.

**P6. Размещение по слою (produces/consumes).** Плагин живёт в домене, который **обслуживает**:
производит/держит состояние домена X → `X`; потребляет нижний слой, чтобы обслужить домен Y
→ `Y` (вход, не выход). Зависимости односторонние, без циклов/инверсий.

## 4. Публичные module-слайсы
Под `include/<ns>/plugins/<family>/<name>/` (по R1 из create-library):
- `plugin.cppm` — класс плагина + `descriptor()`
- `api.cppm` — типизированные локальные контракты
- `types.cppm` — config + DTO-типы
- `exceptions.cppm` — типизированные исключения
- опц. доп. публичный слайс (напр. `middleware.cppm`) — под тем же листовым namespace
- **Без** aggregate-only `.cppm`.

Приватный impl и `.cpp` — по `create-library` (`details/*.hxx`, корневые `*.cpp`, парные,
`_impl` только под крупный pimpl).

## 5. Методы проверки (перед добавлением/переименованием)

**V1. В том ли семействе?** is-a мастер-тест: «`<name>` — это **вид** `<family>` (kind/view)?»
Да → семейство верное. Нет, плагин **выставляет/зависит** от концепта `<family>` → P4
(channel-rooted) или другое размещение.

**V2. Семейство оправдано?** ≥2 is-a членов, ИЛИ uniform-nest политика проекта + правдоподобный
будущий сиблинг. Singleton-семейство по активности/бэкенду без сиблингов → пересмотреть.

**V3. Член по роли?** Не `provider`/`consumer`, не активность, не бэкенд-как-семейство (P1).

**V4. Маппинг консистентен?** Все 5 форм (§2) выводятся из листа по `:: ↔ _ ↔ .` — сверить
namespace/target/component/module/contract.

**V5. Группировка пуста?** В `<ns>::plugins::<family>` нет объявлений типов (P5).

**V6. Слой верный?** produces/consumes (P6); нет восходящих/циклических зависимостей; ядро
не родитель канала (P4).

## 6. Линт-гейты (grep)
```sh
P=plugins/<family>/<name>; NS=<ns>
# типы только в листе, группировочный уровень пуст (V5)
grep -rE '^(export )?(struct|class|enum)' "include/$NS/plugins/<family>/" \
  | grep -v "/plugins/<family>/<name>/"            # → пусто
# нет aggregate-only модулей
# 5 форм согласованы (V4): сверить target/component/module/contract против namespace листа
# ядро не родитель канала (P4): запрет namespace вида <ns>::plugins::api::*
grep -rE '<ns>::plugins::(api|core)::' .            # → пусто (должно быть <channel>::api)
# слой (P6): нижний слой не импортит домен этого плагина (направление задаётся проектом)
```

## 7. Что этот скилл НЕ переписывает
Структура файлов, расширения, `details/`, парный инвариант, `_impl`, разбиение `.cpp` —
всё в `create-library`. Плагин подчиняется ему целиком; здесь только плагин-надстройка
(семейства, маппинг, слайсы, проверка).

---

## Интеграция в AGENTS.md (ссылка, без дублирования правил)
```md
## Plugins
Структура и нейминг плагинов заданы скиллом `create-plugin` (который расширяет
`create-library`). Перед созданием, переименованием ИЛИ правкой плагина — загрузи
и применяй `create-plugin`. Не авторь структуру/namespace/target плагина по памяти.
Правила здесь не дублируются.
```
