# Развёртывание сервера на реальном хостинге

`vc-server` — один самодостаточный бинарь без зависимостей (Windows и Linux сборки
есть в артефактах GitHub Actions; Linux собирается пресетом `linux-server`).

## Конфиг

Рядом с сервером положи `server.cfg` (образец — `server/config/server.cfg.example`):

```
hostname        My Vice City Server
port            7777
maxplayers      100
gamemode        freeroam
```

Поддерживаются: `hostname`, `port`, `maxplayers`, `gamemode`.
`password`, `rcon_password`, `announce`, `weburl` пока игнорируются (в планах).
Другой путь конфига: `vc-server --config /path/to/server.cfg`.
CLI `--port` / `--name` переопределяют конфиг.

## Windows (домашний ПК / Windows VPS)

1. Распакуй артефакт, рядом с `vc-server.exe` создай `server.cfg`.
2. Запусти `vc-server.exe`. При первом запуске Windows Firewall спросит доступ — разреши.
3. Если сервер за домашним роутером: пробрось **UDP-порт 7777** на IP этого ПК
   (port forwarding в настройках роутера) и узнай свой внешний IP.
4. Игроки в `vc-client.ini` указывают `[Server] IP=<твой внешний IP>`.

## Linux (VPS)

```bash
# сборка (или возьми готовый бинарь из CI-артефактов ветки linux)
cmake --preset linux-server && cmake --build --preset linux-server
sudo useradd -r -s /usr/sbin/nologin vcmp
sudo mkdir -p /opt/vcmp
sudo cp build/linux-server/server/vc-server /opt/vcmp/
sudo cp server/config/server.cfg.example /opt/vcmp/server.cfg   # отредактируй под себя
sudo cp server/config/vc-server.service /etc/systemd/system/
sudo chown -R vcmp:vcmp /opt/vcmp
sudo ufw allow 7777/udp        # или аналог в твоём фаерволе/панели VPS
sudo systemctl daemon-reload && sudo systemctl enable --now vc-server
journalctl -u vc-server -f     # живые логи подключений
```

## Проверка снаружи

С любой другой машины (не с самого сервера):

```
vc-netprobe.exe <публичный IP> 7777      # pong + SA-MP query (имя, онлайн)
vc-bot.exe --ip <публичный IP> --port 7777 --nick Probe   # полное подключение по протоколу
```

Если netprobe молчит, а локально всё работает — почти всегда виноват закрытый UDP
в фаерволе VPS/роутера или NAT без проброса порта.

## Честные ограничения текущей версии

- Авторизации нет: зайти может любой с любым ником; пароль сервера ещё не реализован.
- Античита нет: позиции клиентов сервер принимает на веру (как SA-MP в своё время).
- Объявлений на master-server нет: игроки подключаются по прямому IP из `vc-client.ini`.
