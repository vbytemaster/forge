# FCL Env / Dotenv Adapter v1

## Решение

`fcl_env` добавлен как отдельный source adapter для process environment и
явных `.env` файлов. Он строит `fcl::config::document` из
`fcl::config::component_registry`, но не входит в `fcl_app` lifecycle и не
задаёт product bootstrap policy.

Это симметрично текущим источникам конфигурации:

```text
fcl_yaml             file codec -> config::document / typed config
fcl_json             file/text codec -> config::document / typed config
fcl_program_options  argv -> config::document
fcl_env              process env / .env -> config::document
```

## Доноры

- Boost.Program_options environment parsing: принят registry-driven mapping
  pattern, отвергнуты Boost public types.
- Node/Python dotenv conventions: принята базовая `KEY=value` grammar,
  отвергнуты implicit discovery, expansion and global mutation.
- libenvpp/cpp-dotenv: рассмотрены как ready-made options, но отвергнуты как
  dependencies because they duplicate FCL schema/config ownership or encourage
  global environment behavior.

## Границы

- `fcl_env` зависит только от `fcl_config` and `fcl_schema`.
- `fcl_app` не читает env/dotenv напрямую.
- Required/range validation remains in `fcl_schema`/`fcl_config::decode`.
- Storlane-specific names such as workspace or config-file discovery stay in
  Storlane bootstrap code.

## Supported v1 Behavior

- `PREFIX_SECTION_FIELD` mapping with stable separator normalization.
- Empty config sections for flat names such as `FCL_LOG_LEVEL`.
- Canonical field names and schema aliases.
- Unknown prefixed variables warn by default and can become errors.
- Canonical/alias conflicts are errors by default; canonical wins.
- Deprecated fields are accepted with warning by default.
- `.env` parser supports comments, optional `export`, quotes and simple escapes.
- `.env.example` generation uses schema descriptions/defaults and never writes
  secret values.

## Explicitly Deferred

- Variable expansion such as `${HOME}`.
- `_FILE` secret convention.
- Indexed list variables.
- JSON object values in env vars.
- Product commands such as `config explain ENV_NAME`.
- Service-manager adapters.
