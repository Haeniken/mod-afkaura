# mod-afkaura

Модуль автоматически вешает на игрока заданную ауру после периода бездействия.

Особенность этой реализации:
- аура включается только по внутреннему таймеру неактивности;
- ручной `/afk` сам по себе ауру не включает;
- по активности игрока аура снимается;
- при `AFKAura.AutoRefresh = 1` аура автоматически перевешивается после окончания;
- модуль не требует SQL и не требует core patch.

## Конфиг

Настройки читаются из `worldserver.conf`:

- `AFKAura.Enable`
- `AFKAura.AuraSpellId`
- `AFKAura.IdleTimeSeconds`
- `AFKAura.AutoRefresh`
- `AFKAura.IgnoreGameMasters`
- `AFKAura.IgnoreInCombat`
- `AFKAura.IgnoreBattlegrounds`
- `AFKAura.IgnoreFlight`
- `AFKAura.SetPlayerFlagAFK`

Значение по умолчанию для `AFKAura.AuraSpellId` равно `0`, то есть по умолчанию модуль
не вешает никакую ауру, пока не будет настроен явный spell id.

## Поведение

Активностью считаются:
- движение;
- чат, включая `/afk`;
- каст спелла;
- emote / text emote;
- логин.

Если `AFKAura.SetPlayerFlagAFK = 1`, модуль дополнительно ставит серверный `PLAYER_FLAGS_AFK`
только когда таймер истек. Ручной `/afk` при этом не используется как триггер для ауры.
