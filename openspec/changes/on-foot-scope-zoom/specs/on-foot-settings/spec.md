# Spec Delta

## MODIFIED Requirements

### Requirement: Страница On foot в меню F8
Меню F8 SHALL иметь страницу **On foot**, видимую при выключенном Developer mode. На ней SHALL быть ровно шесть строк,
в этом порядке: Native VR on foot, On-foot interface, On-foot aiming eye, On-foot head aim, On-foot aim dot, On-foot
scope zoom. Каждая строка SHALL менять свой ключ `experimental.native_vr_on_foot*` на лету, как сейчас на странице
Experimental.

#### Scenario: Без режима разработчика
- **WHEN** игрок с выключенным Developer mode открывает F8 и листает страницы Tab
- **THEN** среди страниц есть On foot, на ней шесть строк настроек на ногах; страниц Advanced, Experimental и
  Instruments нет

#### Scenario: Изменение на лету
- **WHEN** игрок на ногах на странице On foot ставит On-foot aim dot в `show`
- **THEN** точка прицела появляется без перезапуска игры, а в `edvr.ini` записано `native_vr_on_foot_aim_dot = show`
  в секции `[experimental]`

#### Scenario: Зум в прицеле на лету
- **WHEN** игрок на ногах на странице On foot ставит On-foot scope zoom в `off`
- **THEN** при следующем прицеливании из Executioner линзы нет, а в `edvr.ini` записано
  `native_vr_on_foot_scope_zoom = off` в секции `[experimental]`

### Requirement: Одна настройка — одна строка
Шесть настроек на ногах SHALL NOT показываться на странице Experimental: при включённом Developer mode они остаются
только на странице On foot. Остальные ключи `[experimental]` SHALL остаться на Experimental, как были.

#### Scenario: С режимом разработчика
- **WHEN** игрок включает Developer mode и открывает страницу Experimental
- **THEN** строк Native VR on foot, On-foot interface, On-foot aiming eye, On-foot head aim, On-foot aim dot и On-foot
  scope zoom на ней нет; на странице On foot они есть

### Requirement: Раздел в установщике
Окно настроек установщика SHALL показывать те же шесть настроек под заголовком «Native VR on foot (EDVRonFOOT)», с
вариантами и описанием из `edvr.ini`. Значение SHALL записываться в секцию `[experimental]` файла `edvr.ini` и
сохраняться при обновлении с включённым «Keep the settings I have changed».

#### Scenario: Смена в установщике
- **WHEN** игрок в установщике ставит On-foot interface в `visor` и нажимает Install
- **THEN** в `edvr.ini` в игре `[experimental]` содержит `native_vr_on_foot_hud = visor`, и в игре лист держится
  перед глазами

#### Scenario: Обновление сохраняет выбор
- **WHEN** у игрока в `edvr.ini` стоит `native_vr_on_foot_aim_eye = left`, и он ставит новую версию с «Keep the
  settings I have changed»
- **THEN** после установки значение по-прежнему `left`, и установщик показывает его как изменённое игроком

### Requirement: Имена ключей не меняются
Ключи SHALL остаться `native_vr_on_foot`, `native_vr_on_foot_hud`, `native_vr_on_foot_aim_eye`,
`native_vr_on_foot_head_aim`, `native_vr_on_foot_aim_dot` и `native_vr_on_foot_scope_zoom` в секции `[experimental]`.
Файл `edvr.ini` от 0.1.0 и 0.1.1 SHALL читаться без переноса.

#### Scenario: Старый файл настроек
- **WHEN** игрок с `edvr.ini` от 0.1.1, где `native_vr_on_foot_head_aim = off`, ставит новую версию
- **THEN** прицел головой выключен, и страница On foot показывает `off`
