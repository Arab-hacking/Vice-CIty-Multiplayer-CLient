# Сетевой протокол (RakNet wire format)

> Транспорт строится на проволочном формате RakNet той же версии, что использует
> SA-MP. Формат датаграмм/заголовков/BitStream перенесён в C++ по реализации
> **PyRakBR-fork** (Sahiro01, лицензия MIT): `core/bitstream.py`, `core/protocol.py`,
> описание — `docs/PROTOCOL.md` в том репозитории. Атрибуция — [NOTICE](../NOTICE).
>
> **Сознательно НЕ перенесено:** логин в Black Russia/SA-MP (`core/br_auth.py`,
> `core/auth_keys.py`, авторегистрация, «security»-обфускация тел пакетов SA-MP).
> Этот стек связывает **наш reVC-клиент с нашим сервером**. Серверы Black Russia —
> это GTA: San Andreas: Vice City-клиент не может на них играть ни при каком
> RakNet-хендшейке (другой мир, модели, RPC и синхро-структуры).

## Реализовано сейчас (`shared/include/shared/net/`)

| Файл | Что |
|---|---|
| `bitstream.h/.cpp` | RakNet BitStream: биты от старшего, uint16/32 little-endian байтами, `WriteCompressed` |
| `raknet.h/.cpp` | Датаграммы, внутренние пакеты, ACK-диапазоны |
| `offline.h/.cpp` | RakNet unconnected ping/pong; SA-MP query `i` (ответы нашего сервера) |
| `udp_socket.h/.cpp` | Winsock2/POSIX UDP |

### Датаграмма

```
[has_acks : 1 бит]
[ACK-диапазоны — если has_acks]   count:compressed16, далее {single:1, min:u16LE, max:u16LE?}
[пакет][пакет]...                  до конца датаграммы
```

### Внутренний пакет

```
message_number    : u16 LE
reliability       : 3 бита   (0 Unreliable, 1 UnreliableSequenced, 2 Reliable,
                              3 ReliableOrdered, 4 ReliableSequenced)
ordering_index    : u16 LE   только reliability 1/3/4
~ordering_channel : 4 бита   только reliability 1/3/4 (канал инвертирован — как в RakNet)
has_split         : 1 бит
split_id          : u16 LE          если has_split
split_index/count : compressed32    если has_split
bitlen            : compressed16    длина тела в битах
align до байта, тело пакета         игровые пакеты из shared/protocol
```

Отличие от SA-MP/PyRakBR: у надёжных пакетов нет `checksum_byte` и обфускации тела —
нам она не нужна (свой сервер), честность важнее совместимости с чужими серверами.

### Offline (до соединения)

- RakNet **unconnected ping** `0x01` → **pong** `0x1C` с MOTD (`имя;онлайн;макс;режим;язык`) —
  этим `vc-netprobe` и лаунчер проверяют доступность и пинг.
- **SA-MP query** `'SAMP' + ip + port + 'i'` → наш сервер отвечает стандартным
  info-пакетом SA-MP, чтобы его видели и сторонние мониторинги.

## Слой надёжной доставки (реализован, Этап 2)

`shared/net/peer.*` (`vc::net::ReliablePeer`, по одному на собеседника):
нумерация `message_number`, ACK-диапазоны, переотправка через 500 мс (до 12 раз),
дедупликация надёжных, ordered-буферы по 16 каналам, отбрасывание устаревших sequenced.

## Сессия и игровые пакеты (реализовано, Этап 2)

Тела пакетов сериализует `shared/protocol/codec.*`; все числа little-endian,
первый байт — ID из `shared/protocol/packets.h`:

| Пакет | Направление | Reliability/канал | Формат после ID |
|---|---|---|---|
| `ConnectionRequest` (0) | К→С | ReliableOrdered/0 | `u32 protocol, u8 nickLen, nick` |
| `ConnectionAccepted` (100) | С→К | ReliableOrdered/0 | `u16 playerId, u16 maxPlayers` |
| `ConnectionRejected` (104) | С→К | ReliableOrdered/0 | `u16 len, reason` |
| `ServerMessage` (130) | С→К | ReliableOrdered/0 | `u16 len, text` |
| `ChatMessage` (20) | К→С | ReliableOrdered/0 | `u16 len, text` (сервер ретранслирует как ServerMessage всем) |
| `OnFootSync` (10) | К→С | UnreliableSequenced/1, 10 Гц | `f32 x, f32 y, f32 z` (расширится на Этапе 3) |
| `KeepAlive` (22) | К→С | Unreliable/0, раз в 3 с без спавна | пустой |
| `Disconnect` (34) | К→С | Unreliable/0 | пустой — сервер сразу закрывает сессию (иначе по таймауту 15 с) |

Сеть клиента работает в отдельном потоке: пауза/Alt-Tab/загрузка игры соединение не рвут.
Игровой поток только публикует свежую позицию (`SetPlayerPosition`).

Сервер: сессия по endpoint, таймаут 15 с, welcome-сообщение, онлайн в SA-MP query — живой.
Клиент: параметры подключения из `vc-client.ini` рядом с DLL (`[Server] IP/Port/Nick`,
лаунчер создаёт дефолт 127.0.0.1:7777/Player). Для тестов без игры — `tools/bot-client` (`vc-bot`).

## Видимые игроки (Этап 3, протокол v2)

| Пакет | Направление | Reliability/канал | Формат после ID |
|---|---|---|---|
| `PlayerStreamIn` (105) | С→К | ReliableOrdered/0 | `u16 id, u8 nickLen, nick, f32 x,y,z` — создать куклу |
| `PlayerStreamOut` (106) | С→К | ReliableOrdered/0 | `u16 id` — убрать куклу |
| `PlayerSyncUpdate` (107) | С→К | UnreliableSequenced/1 | `u16 id, f32 x,y,z` — ретрансляция чужого OnFootSync |

Сервер шлёт StreamIn новичку обо всех онлайн и всем о новичке; стриминга по радиусу
пока нет (все видят всех). Клиент спавнит `CCivilianPed` (модель 7) через пул игры,
отключает её ИИ хуком `CPed::ProcessControl` и двигает Teleport-ом каждый кадр
с экспоненциальным сглаживанием к целевой позиции.

## Дальше по протоколу

1. Расширенный OnFootSync: скорость, поворот (heading), HP, оружие, анимация бега.
2. Стриминг по радиусу (~200 м) вместо «все видят всех».
3. Пароль сервера в ConnectionRequest.

## Проверка серверов из списка пользователя

`tools/netprobe/servers.example.txt` содержит локальный сервер и 3 сервера Black Russia.
`vc-netprobe` делает **только** offline-ping и SA-MP query (поверхностная проверка
«жив ли UDP-порт», как сервер-браузер). Из CI/песочницы внешние UDP недоступны —
запускать с обычной машины: `vc-netprobe --file tools/netprobe/servers.example.txt`.
Учитывай: BR использует кастомный протокол поверх RakNet, поэтому стандартные
ping/query она может просто игнорировать, даже когда сервер онлайн.
