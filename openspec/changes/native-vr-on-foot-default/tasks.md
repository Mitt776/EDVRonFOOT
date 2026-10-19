## 1. Код

- [x] 1.1 Удалить `native_vr_block_cinema`: ключ, флаг, подмену в хуке пересборки и её строки; хук пересборки остаётся для зонда. Проверка: grep `block_cinema|BlockCinema` по `edvr/src` и стендам пуст; стенд `native_vr_probe_test` проходит
- [x] 1.2 Удалить `native_vr_swap_eyes`: шаг и флаг в зонде, экспорт `edvrNativeVrSwapEyes`, перестановку в `eye_capture` и `native_runtime_host.h`, проверки стендов. `advanced.remlok_swap_eyes` не трогать. Проверка: grep `native_vr_swap_eyes|SwapEyes|swapEye` по `edvr/src` и стендам пуст (кроме remlok); стенды `native_vr_probe_test` и `openxr_capture_test` проходят
- [x] 1.3 `native_vr_on_foot` по умолчанию 1 в коде и эталонном `edvr/edvr.ini`, ключ видимый с понятным описанием, удалены строки двух ключей из эталона. Проверка: контракт настроек в сборке проходит

## 2. Документы, сборка, установка

- [x] 2.1 README `edvr/README_EDVRONFOOT.md`: убрать разделы блокировки и перестановки, раздел `native_vr_on_foot` — «включено по умолчанию, как выключить»; исследование — ключи в `## Status`. Проверка: в README нет `native_vr_block_cinema` и `native_vr_swap_eyes`
- [ ] 2.2 Полная сборка `edvr\build_edvronfoot.bat`, коммит, тег `v0.19.1-edvronfoot.7`, `--dll-only`, установка (игра закрыта), удалить строки `native_vr_block_cinema` и `native_vr_swap_eyes` из `edvr.ini` игры через Edit. Проверка: «Native OpenXR build and all gates passed», `edvronfoot_game.ps1 -Action Status` показывает `.7`

## 3. Проверка

- [ ] 3.1 Полёт Димы: запуск → на ноги → голова и глаза как в `.6` → в корабль → выход. Проверка по логу: `.7`, `head camera on`, нет строк `native-vr-block-cinema` и `native-vr-swap-eyes`
