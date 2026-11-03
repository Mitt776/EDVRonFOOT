# Spec Delta

## REMOVED Requirements

### Requirement: Ключ зонда
**Reason**: временный зонд `native_vr_on_foot_head_aim_probe` выполнил задачу (`.36`: рычаги, знак, такт); прицел головой построен и облетан (`.40`).
**Migration**: строку `native_vr_on_foot_head_aim_probe` из `edvr.ini` убрать; прицел головой — ключ `native_vr_on_foot_head_aim` (`on-foot-aiming`).

### Requirement: Где зонд действует
**Reason**: зонд удаляется; где действует прицел головой, задаёт `on-foot-aiming` («Где прицел головой действует»).
**Migration**: не нужна.

### Requirement: Проверка кода игры перед хуком
**Reason**: хуки остаются у прицела головой; требование переезжает в `on-foot-aiming` («Проверка кода игры перед хуками прицела головой»), строки получают префикс `head aim:`.
**Migration**: искать в логе `head aim: hooked` / `head aim: inert` вместо `head aim probe: ...`.

### Requirement: Чтение углов (watch)
**Reason**: строка `watch` нужна была для выбора рычага (`.35`/`.36`); углы прицела видны в сводке `on-foot-aiming`.
**Migration**: не нужна.

### Requirement: Сдвиг угла игрока
**Reason**: пробные сдвиги на 10° и качание заменены прицелом головой.
**Migration**: не нужна.

### Requirement: Игрок не опознан
**Reason**: опознание игрока остаётся у прицела головой; требование переезжает в `on-foot-aiming` («Опознание игрока для прицела головой»).
**Migration**: искать в логе `head aim: player identified` / `head aim: player not identified`.

### Requirement: Вычитание поворота из позы головы для игры
**Reason**: знак выбран (`plus`), поворот позы назад делает прицел головой («Мир не едет за прицелом»); ключ `native_vr_on_foot_head_aim_probe_compensate` удаляется.
**Migration**: строку `native_vr_on_foot_head_aim_probe_compensate` из `edvr.ini` убрать.

### Requirement: Сводка зонда
**Reason**: сводка зонда удаляется вместе с ним; сводка и строка такта прицела головой остаются (`on-foot-aiming`).
**Migration**: не нужна.
