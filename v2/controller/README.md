# RicinoNext — contrôleur (Python / MicroPython) · v1.0.0

Compteur de tours pour Mini-Z (émetteurs IR type Robitronic). Le même code tourne sur PC
(simulateur) et sur ESP32 (MicroPython), à partir du firmware d'origine
`Software/Controller/RicinoNext-controller.ino`.

**Installation sur le matériel : voir [INSTALL.md](INSTALL.md).** Firmware des gates : [../receiver](../receiver/README.md).

## Fonctions

- Course aux **tours**, au **temps** ou **libre**, mode **solo**, départ **normal** ou **lancé**,
  décompte de départ réglable de 5 à 30 s (à l'écran : chiffres ou **feux F1**, au choix par appareil ;
  feux F1 sur un ruban WS2812 en option), bips de départ
  et bip à chaque passage sur la ligne (plus aigu sur un record perso).
- Classement plein écran : écart au leader, tour en cours par secteur (gates), meilleur tour en violet
  et record perso en vert, tendance ▲▼ par rapport au tour précédent, **dépassements animés**,
  mode vidéoprojecteur (`F` ou `#display`).
- **Résultats** : podium, graphique des temps au tour, détail tour par tour. L'historique est gardé par
  le contrôleur, donc une page rechargée en pleine course le récupère.
- **Garage** (jusqu'à 50 voitures) : nom et code IR de chaque voiture, **lecture du code** (📡) en passant la
  voiture sur la ligne, record de la voiture. Les **pilotes** (nom, couleur) choisissent leur voiture.
- **🏆 Records** (onglet) : top 10 meilleur tour, meilleure moyenne, meilleur temps total par nombre de tours,
  gardés en mémoire permanente, avec pilote, voiture et date ; suppression d'un chrono.
- **Résultats** : affichés à l'arrivée, puis rouverts par « 📊 Derniers résultats » sur la page de course.
- Réglages de course et pilotes **conservés** entre deux redémarrages.
- Thème clair / sombre / auto.
- ESP32 : configuration Wi-Fi et nom d'hôte (`<nom>.local`), point d'accès de secours avec portail captif,
  mises à jour **OTA** (fichiers et firmware MicroPython), mode démo, mode secours,
  **bouton Start/Stop physique** et **feux de départ** sur ruban WS2812.

## Règles de comptage

- **Anti-rebond** : un passage ne compte qu'après **3 s** (réglable 1-10 s) **sans aucune détection** de la même
  voiture sur la même porte. Une voiture arrêtée sur la ligne ou une double détection ne font pas de tour.
- **Ordre des portes** : 1 → 2 → … → 1. Le chrono d'un pilote démarre à son premier passage sur la ligne ;
  les secteurs ne sont mesurés qu'entre deux portes consécutives.
- **Porte sautée** : **tolérée** (détection ratée probable) si le pilote n'a fait aucune erreur de porte sur ses
  **N derniers passages** (N = 10, réglable, 0 = jamais) ; sinon **le tour en cours n'est pas compté** et le
  suivant part de la ligne. Les deux cas sont annoncés et comptés (⚠ tolérées, 🚫 tours non comptés).

## Organisation

```
controller/
  main.py            point d'entrée : détecte PC ou ESP32 (+ mode secours sur ESP32)
  ricino/core.py     course : machine à états, chronos tours/secteurs, paquets I2C, protocole JSON
  ricino/web.py      serveur HTTP (fichiers statiques, .gz prioritaire) + WebSocket /ws
  ricino/sim.py      gates et voitures simulées (PC, et mode démo sur ESP32)
  ricino/pc.py       plateforme PC : options en ligne de commande, horloge, settings.json
  ricino/esp32.py    plateforme ESP32 : bus I2C ou RS485, Wi-Fi, nom d'hôte, API système, OTA
  ricino/lights.py   feux de départ F1 sur ruban WS2812 (ESP32)
  ricino/button.py   bouton poussoir ou interrupteur Start/Stop (ESP32)
  ricino/rs485.py    trames RS485 contrôleur <-> gates (référence pour le firmware des récepteurs)
  data/index.html    page de course (classement, résultats, réglages de course)
  data/system.html   page système : Wi-Fi, nom d'hôte, matériel, mises à jour (ESP32)
  tools/deploy.sh    installation sur ESP32 par USB
  test_controller.py test de bout en bout du simulateur (WebSocket brut)
  test_esp32.py      code ESP32 exécuté avec des modules MicroPython imités
```

Sur l'ESP32, les modules sont installés **précompilés en `.mpy`** (voir INSTALL.md) : sinon la compilation
au démarrage prive le Wi-Fi de mémoire.

`core.py`, `web.py` et `sim.py` n'utilisent que ce qui existe à la fois en CPython et en
MicroPython (pas de `:=`, pas de f-string, pas de `os.path`). La structure de `core.py` suit
celle du `.ino` (mêmes noms, même `loop()`) pour un éventuel portage C++. Les corrections par
rapport au firmware d'origine sont marquées `FIX:`.

## Sur PC

```
python3 main.py                        # http://localhost:8080/
python3 main.py --speed 5 --laptime 4  # course accélérée
python3 main.py --settings ""          # sans garder les réglages (settings.json)
python3 test_controller.py
python3 test_esp32.py
python3 test_rules.py                  # règles de comptage (anti-rebond, ordre des portes, tolérance)
python3 test_records.py                # records (top 10)
```

Aucune dépendance (bibliothèque standard uniquement).

## Protocole WebSocket (`/ws`)

Les valeurs `conf` envoyées par le client sont des **chaînes** (le firmware les lit avec `atoi`),
sauf `names` (tableau d'objets).

| Message | Champs |
|---|---|
| `conf` ↔ | laps, time (ms), players, gates, light, reset, style (0 tours, 1 temps, 2 libre), solo, warmup (s), start (0 normal, 1 lancé), read (lecture de tag 0/1), state, names[{id, name, color}] ; à la connexion en plus laps_m, time_m (min), players_m, gates_m |
| `race` → | state, lap, time, message?, countdown? (ms, pendant le décompte) |
| `live` → | rank, id, lap, best, last, mean, total (temps propre), cross (heure du dernier passage sur la ligne), sector (dernière gate passée), gate[] (temps des secteurs) |
| `history` → | {id: [temps des tours]} à la connexion, pendant ou après une course |
| `tag` → | id, gate, hit, strength (mode lecture de tag) |
| `debug` → | time, memory, … |

## Liaison avec les gates : I2C ou RS485

Au choix dans la page système. Dans les deux cas, chaque gate fournit le même paquet de 13 octets
(`0x82` état, `0x83` horodatage, `0x84` passage d'un émetteur) ; seul le transport change.

- **I2C** : SDA 21, SCL 22, 100 kHz (option SoftI2C). Câble court.
- **RS485** : UART2, TX 26, RX 25, 57600 bauds, 8N1, module à direction automatique (HW-519).
  Câble long (dizaines de mètres), paire torsadée A+/B-, 120 Ω aux deux extrémités.

Trames RS485 (`ricino/rs485.py`), CRC-8 Dallas/Maxim (le même que le paquet `0x84`) :

```
requête  contrôleur -> gate : A5 | adresse | commande | crc8(adresse, commande)
réponse  gate -> contrôleur : 5A | adresse | 0D | paquet[13] | crc8(adresse, 0D, paquet)
commandes : 01 poll, 02 répéter la dernière réponse, 8A départ, 8F arrêt ; adresse 0 = toutes les gates, sans réponse
```

Le contrôleur interroge une gate toutes les 20 ms (attente de réponse 10 ms au plus). Si une réponse
est perdue ou corrompue, l'interrogation suivante demande sa répétition (`02`), donc aucun passage n'est
perdu, et un doublon est écarté. Le départ
part en une seule trame à l'adresse 0 : toutes les gates démarrent leur horloge au même instant.

## API système (ESP32, `data/system.html`)

`GET /api/system`, `GET /api/scan`, `POST /api/config`, `POST /api/upload?path=…`,
`POST /api/firmware`, `POST /api/reboot`. Les routes d'écriture demandent l'en-tête `X-Password`
si un mot de passe d'administration est défini. Sur PC, `/api/system` répond `platform: "pc"` et
les routes d'écriture répondent 403.

## Fichiers sur l'ESP32

| Fichier | Contenu |
|---|---|
| `/config.json` | Wi-Fi, nom d'hôte, mots de passe, bus (I2C / RS485), mode démo |
| `/race.json` | réglages de course et pilotes (enregistrés 3 s après le dernier changement) |
| `/error.log` | dernière erreur de démarrage (mode secours) |
