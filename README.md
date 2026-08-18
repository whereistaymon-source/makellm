# MakeLLM

Генератор уровней Geometry Dash 2.2 через любую OpenAI-compatible LLM — прямо в редакторе уровней.

## Что это

Мод добавляет в редактор кнопку **LLM**. Вы описываете уровень текстом (RU/EN), мод:

1. Спрашивает LLM по OpenAI-протоколу (`POST {BaseURL}/chat/completions`)
2. Парсит ответ из компактного DSL (**MLS — MakeLLM Script**) в объекты GD
3. **Прогоняет уровень физическим ботом**: проходимость прыжков (апекс 65u / длина 134u), зазоры в полётных коридорах, цепочки орбов/падов, порталы скорости
4. Ловит **secret-way**: уровень не должен проходиться без нажатий; у полётных секций обязан быть потолок
5. Если валидатор нашёл проблемы — отправляет их обратно в LLM на исправление (до N раундов)
6. Ставит результат на отдельный слой редактора → **Accept / Deny**, отмена одним Ctrl+Z

## Установка (готовый .geode)

1. Скачайте `subkulturaakp.makellm.geode` из Releases
2. В игре: Geode → Mods → кнопка ручной установки → выберите файл
3. Перезапустите игру

## Настройка

Geode → Mods → MakeLLM → ⚙:

| Настройка | По умолчанию | Описание |
|---|---|---|
| Base URL | `http://localhost:20128/v1` | OpenAI-compatible endpoint (Omniroute). Ollama: `http://localhost:11434/v1`, LM Studio: `http://localhost:1234/v1` |
| API Key | *(пусто)* | Bearer-ключ; локальным серверам не нужен. Хранится локально |
| Model | `default` | Имя модели, которое ждёт ваш endpoint |
| Temperature | 0.9 | Выше — креативнее |
| Difficulty / Style / Length | medium / modern / medium | Рамки генерации |
| Max Objects | 800 | Жёсткий лимит объектов |
| Refinement Rounds | 2 | Сколько раз AI чинит уровень после валидатора |
| Unique Seed | on | Случайные тема/палитра/механика на каждую генерацию |
| Prevent Secret Ways | on | Отбраковка авто-проходов и полётов без потолка |
| Triggers & Colors | on | Триггеры move/color/pulse/alpha/rotate/spawn/toggle/stop |
| Ground Y | 105 | Если блоки «плавают» — уменьшите; «тонут» — увеличьте |
| Spawn Speed | 8 | Объектов за тик редактора |

## Сборка из исходников (Windows)

```powershell
# 1. Установите Geode CLI: https://geode-sdk.org/install
geode sdk install            # однократно

# 2. Клонируйте и соберите
git clone <repo-url> MakeLLM
cd MakeLLM
geode build                  # соберёт и установит мод в GD
```

Либо вручную: `cmake -B build -A win32 -T host=x86` + `cmake --build build --config Release`.
Зависимости подтягиваются автоматически: `geode.node-ids`.

## Структура

```
src/
  main.cpp       — хуки редактора, попап, спавн, Accept/Deny
  LLMClient.hpp  — OpenAI-compatible HTTP-клиент (geode web)
  Prompt.hpp     — системный промпт + сид уникальности
  MLS.hpp        — парсер DSL (макросы FLOOR/CORRIDOR/SPIKES/...)
  Validator.hpp  — физический бот (проходимость, secret-way)
  Catalog.hpp    — 70 проверенных ID объектов GD 2.2
test/
  driver.cpp     — автономные тесты ядра (без Geode): g++ -std=c++20 test/driver.cpp
```

## Безопасность

- API-ключ не покидает устройство (Geode save data)
- Ничего не отправляется никуда, кроме вашего Base URL
- Превью-объекты изолированы слоем и удаляются по Deny

Лицензия: MIT.
