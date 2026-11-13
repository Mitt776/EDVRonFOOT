# Spec Delta

## REMOVED Requirements

### Requirement: Ключ зонда зума
**Reason**: временный зонд `scope-zoom-probe` закрыт после трёх полётов 13.11; линза живёт в возможности `on-foot-scope-zoom`.
**Migration**: ключ `native_vr_on_foot_scope_zoom` остаётся, теперь `on`/`off` (`on-foot-scope-zoom`); `census` и `preview` читаются как `on`.

### Requirement: Где зонд действует
**Reason**: временный зонд `scope-zoom-probe` закрыт после трёх полётов 13.11; линза живёт в возможности `on-foot-scope-zoom`.
**Migration**: ключ `native_vr_on_foot_scope_zoom` остаётся, теперь `on`/`off` (`on-foot-scope-zoom`); `census` и `preview` читаются как `on`.

### Requirement: Оружие и прицеливание в логе
**Reason**: строки начала и конца прицеливания и флаг игры были нужны для замеров; замеры сделаны (три полёта 13.11), линза от флага не зависит.
**Migration**: ключ `native_vr_on_foot_scope_zoom` остаётся, теперь `on`/`off` (`on-foot-scope-zoom`); `census` и `preview` читаются как `on`.

### Requirement: Зум игры в логе
**Reason**: строка на каждую смену FOV была нужна, чтобы найти зум игры; он найден, сила зума теперь требование «Сила зума как у игры» в `on-foot-scope-zoom`.
**Migration**: ключ `native_vr_on_foot_scope_zoom` остаётся, теперь `on`/`off` (`on-foot-scope-zoom`); `census` и `preview` читаются как `on`.

### Requirement: Снимок глаз по PAUSE
**Reason**: тихий снимок по `PAUSE` был нужен для замера стекла и центра; стекло Executioner снято, `PAUSE` снова снимает глаза как до зонда.
**Migration**: ключ `native_vr_on_foot_scope_zoom` остаётся, теперь `on`/`off` (`on-foot-scope-zoom`); `census` и `preview` читаются как `on`.

### Requirement: Пробная линза
**Reason**: временный зонд `scope-zoom-probe` закрыт после трёх полётов 13.11; линза живёт в возможности `on-foot-scope-zoom`.
**Migration**: ключ `native_vr_on_foot_scope_zoom` остаётся, теперь `on`/`off` (`on-foot-scope-zoom`); `census` и `preview` читаются как `on`.

### Requirement: Линза следует за стволом
**Reason**: временный зонд `scope-zoom-probe` закрыт после трёх полётов 13.11; линза живёт в возможности `on-foot-scope-zoom`.
**Migration**: ключ `native_vr_on_foot_scope_zoom` остаётся, теперь `on`/`off` (`on-foot-scope-zoom`); `census` и `preview` читаются как `on`.

### Requirement: Отказ без вреда
**Reason**: временный зонд `scope-zoom-probe` закрыт после трёх полётов 13.11; линза живёт в возможности `on-foot-scope-zoom`.
**Migration**: ключ `native_vr_on_foot_scope_zoom` остаётся, теперь `on`/`off` (`on-foot-scope-zoom`); `census` и `preview` читаются как `on`.

### Requirement: Сводка зонда
**Reason**: временный зонд `scope-zoom-probe` закрыт после трёх полётов 13.11; линза живёт в возможности `on-foot-scope-zoom`.
**Migration**: ключ `native_vr_on_foot_scope_zoom` остаётся, теперь `on`/`off` (`on-foot-scope-zoom`); `census` и `preview` читаются как `on`.
