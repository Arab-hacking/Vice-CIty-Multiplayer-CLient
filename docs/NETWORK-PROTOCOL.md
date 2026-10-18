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

## Дальше по протоколу (Этап 2–3)

1. Connection handshake своего формата: версия протокола (`shared/version.h`), ник, пароль сервера.
2. Слой надёжной доставки поверх фрейминга: очередь повторов по ACK (механика разобрана в PyRakBR `core/protocol.py`: resend ~по RTT, дедупликация по message_number, ordered-буферы по каналам, склейка split).
3. Игровые пакеты/RPC — прежний черновик ID из `shared/include/shared/protocol/packets.h`
   (onfoot/vehicle sync ненадёжно ~25 Гц, RPC и чат — ReliableOrdered).

## Проверка серверов из списка пользователя

`tools/netprobe/servers.example.txt` содержит локальный сервер и 3 сервера Black Russia.
`vc-netprobe` делает **только** offline-ping и SA-MP query (поверхностная проверка
«жив ли UDP-порт», как сервер-браузер). Из CI/песочницы внешние UDP недоступны —
запускать с обычной машины: `vc-netprobe --file tools/netprobe/servers.example.txt`.
Учитывай: BR использует кастомный протокол поверх RakNet, поэтому стандартные
ping/query она может просто игнорировать, даже когда сервер онлайн.
