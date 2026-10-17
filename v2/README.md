# RicinoNext v2 : compteur de tours pour Mini-Z

Un compteur de tours **libre et fait maison** pour voitures RC (Mini-Z) : chaque voiture porte un petit
émetteur infrarouge, des **portes** placées sur la piste les détectent, et un **ESP32** chronomètre la course
et l'affiche dans une **page web** (téléphone, tablette, PC, vidéoprojecteur). Rien à installer côté
spectateurs : un navigateur suffit.

Ce guide suit l'ordre à respecter quand on part de zéro :

1. [Comprendre le système](#1-comprendre-le-système)
2. [Rassembler et fabriquer le matériel](#2-rassembler-et-fabriquer-le-matériel)
3. [Câbler](#3-câbler)
4. [Programmer les portes](#4-programmer-les-portes)
5. [Programmer le contrôleur ESP32](#5-programmer-le-contrôleur-esp32)
6. [Configurer](#6-configurer)
7. [Première course](#7-première-course)
8. [Dépannage](#8-dépannage)
9. [Sans matériel : le simulateur sur PC](#9-sans-matériel--le-simulateur-sur-pc)

---

## 1. Comprendre le système

```
   voiture + émetteur IR                                         navigateurs (Wi-Fi)
        ((( 940 nm )))                                         téléphone · PC · vidéoprojecteur
              │                                                          ▲
              ▼                                                          │ page web + WebSocket
 ┌──────────── porte (gate) ────────────┐   bus RS485 (câble)   ┌──── contrôleur ────┐
 │ TFDU4100 → ATmega16  → ATmega328PB ──┼─ HW-519 ═══════════ HW-519 ─┤ ESP32 (MicroPython)│
 │ capteur    décodeur    firmware v2   │   A+/B- + alim.            │ chrono, page web,   │
 └──────────────────────────────────────┘                            │ Wi-Fi, records      │
     porte 1 = ligne d'arrivée (21), portes 2, 3… = temps intermédiaires └─────────────────┘
```

- **Émetteur** (sur chaque voiture) : envoie en boucle le code unique de la voiture.
- **Porte** : l'ATmega16 décode l'infrarouge (protocole Robitronic) et horodate chaque passage ;
  l'ATmega328PB transmet ces passages au contrôleur sur le **bus RS485**.
- **Contrôleur** : un ESP32 interroge les portes, compte les tours, applique les règles (anti-rebond,
  ordre des portes), et sert la page web. Il peut aussi piloter des feux de départ (ruban de LED) et un
  bouton Start/Stop physique.

Les temps sont mesurés **par les portes** : le contrôleur peut être lent ou occupé sans fausser un chrono.

## 2. Rassembler et fabriquer le matériel

### Liste

| Élément | Quantité | Remarques |
|---|---|---|
| **Carte de porte** « IRDA_I2C » | 1 par porte (1 minimum : la ligne d'arrivée) | fichiers de fabrication ci-dessous |
| **Émetteur IR** | 1 par voiture | ATtiny24/44/84 (ou 25/45/85) + LED IR 940 nm, voir ci-dessous |
| **ESP32 DevKit** (WROOM-32, 4 Mo de flash) | 1 | avec port USB ; l'antenne externe (WROOM-32U) porte plus loin |
| **Module RS485 HW-519** (MAX485 à direction automatique) | 1 par porte + 1 pour l'ESP32 | bornes VCC/TXD/RXD/GND et A+/B-/GND |
| **Câble** réseau (RJ45, catégorie 5) | de l'ESP32 à la dernière porte | 4 paires : données + alimentation |
| **Alimentation 5 V** | 1 | ≈ 100 mA par porte + l'ESP32 ; 9-12 V et un régulateur par porte pour un long câble |
| **Programmateur ISP** | 1 | USBtinyISP, USBasp, ou une carte Arduino avec le croquis « ArduinoISP » |
| Adaptateur ISP **pas de 1,27 mm** (ou fils fins) | 1 | les connecteurs de programmation de la porte sont petits |
| *Option* : ruban **WS2812** (8 LED) | 1 | feux de départ |
| *Option* : **bouton poussoir** | 1 | Start / Stop sans écran |

### Fabriquer les cartes

Les fichiers sont dans le dossier `Hardware/` du dépôt (Gerber, nomenclature, placement des composants,
compatibles avec une fabrication **et un montage** chez JLCPCB, composants LCSC) :

- **Porte** : [`Hardware/Receiver/irda_robi/PCB files`](../Hardware/Receiver/irda_robi/PCB%20files)
  (schéma `Schematic_IRDA_I2C_2021-09-16.pdf`).
- **Extension de porte** (capteur supplémentaire pour une piste large) :
  [`Hardware/Receiver/irda_robi_extension`](../Hardware/Receiver/irda_robi_extension).
- **Émetteur** (panneau de 6) : [`Hardware/Emitter/irda_robi/Schematics`](../Hardware/Emitter/irda_robi/Schematics) ;
  firmware et brochage : [`Software/Emitter/irda_robi`](../Software/Emitter/irda_robi/README.md). Chaque émetteur
  tire un code au hasard au premier démarrage et le garde en EEPROM.

⚠ La nomenclature indique un **ATmega328P**, mais les cartes montées portent souvent un **ATmega328PB**
(signature `1E 95 16`). Le firmware v2 cible le **328PB** ; voir [receiver/README.md](receiver/README.md)
pour un vrai 328P.

### Les connecteurs de la carte de porte (à connaître)

| Repère | Rôle | Broches |
|---|---|---|
| **J1** (2,54 mm, 6 broches) | connecteur principal : alimentation + RS485 | 1 GND · 2 VCC (à vérifier au multimètre) · 3 SCL · 4 SDA · **5 TX (PB1)** · **6 RX (PB0)** |
| **P1** (1,27 mm) | programmation de l'**ATmega328PB** | 1 GND · 2 VCC · 3 MOSI · 4 MISO · 5 SCK · 6 RESET |
| **P2** (1,27 mm) | programmation de l'**ATmega16** (décodeur) | même brochage que P1 |
| **SJ3 / SJ4 / SJ5** | cavaliers à souder : **adresse** de la porte | voir le tableau du §4 |

## 3. Câbler

### Vue d'ensemble

```
 ALIM 5 V
  +5V ──┬────────────────────────────── +V   (paire 2) ──────────────────┬─────────────► porte suivante
        │                                                                │
  GND ──┼──┬─────────────────────────── GND  (paire 3) ──────────────────┼──┬──────────► porte suivante
        │  │                                                             │  │
 ┌──────┴──┴──── CONTRÔLEUR ─────────────┐                        ┌──────┴──┴──── PORTE 21 ───────────────┐
 │  ESP32 DevKit         HW-519          │                        │  HW-519               carte de porte  │
 │  5V/VIN ◄── +5V                       │                        │  VCC ◄──────────────── J1-2  VCC      │
 │  3V3 ──────────────►  VCC             │                        │  GND ───────────────── J1-1  GND      │
 │  GND ──────────────── GND             │                        │  TXD ◄──────────────── J1-5  TX (PB1) │
 │  GPIO26 (TX) ──────►  TXD             │                        │  RXD ────────────────► J1-6  RX (PB0) │
 │  GPIO25 (RX) ◄─────── RXD             │                        │                                       │
 │                       A+ ═════════════╪══ A+ (paire 1,  ═══════╪ A+ ═══════════════════════════════════╪══► A+ porte suivante
 │                       B- ═════════════╪══ B-  torsadée)  ══════╪ B- ═══════════════════════════════════╪══► B- porte suivante
 └───────────────────────────────────────┘                        └───────────────────────────────────────┘
   TX de la carte → TXD du module · RX → RXD · toutes les masses reliées · pas de 120 Ω sur un câble court
```

### Contrôleur

```
ESP32 DevKit                         HW-519 n°1 (côté contrôleur)
  3V3 ──────────────────────────────── VCC        ← 3,3 V : ses signaux restent au niveau de l'ESP32
  GND ──────────────────────────────── GND
  GPIO 26 (TX) ─────────────────────── TXD
  GPIO 25 (RX) ─────────────────────── RXD
                                       A+ ──┐
                                       B- ──┼──► vers le câble du bus
                                       GND ─┘
Options :
  GPIO 27 ──[330 Ω]── DIN ruban WS2812 (ruban alimenté en 5 V à part, masses reliées)
  GPIO 33 ── bouton poussoir ── GND   (aucune résistance : pull-up interne)
```

### Chaque porte

```
Carte de porte, connecteur J1        HW-519 (côté porte)
  J1-2  VCC 5 V ────────────────────── VCC
  J1-1  GND ────────────────────────── GND
  J1-5  TX  (PB1) ──────────────────── TXD
  J1-6  RX  (PB0) ──────────────────── RXD
                                       A+ / B- / GND ──► bus
```

**Règle des étiquettes HW-519 : TX de la carte → TXD du module, RX → RXD** (vérifié sur les deux côtés).
Si une porte ne répond pas, inverser ces deux fils ne risque rien.

### Le bus et l'alimentation

```
[ESP32]─HW-519 ══════ câble ══════ HW-519─[porte 21] ══════ HW-519─[porte 22] ══════ HW-519─[porte 23]
              (A+, B-, GND, +V)          ligne d'arrivée            secteur 2              secteur 3
```

| Paire du câble RJ45 | Usage |
|---|---|
| paire 1 (torsadée) | **A+ / B-** |
| paire 2 | **+V** (les deux fils ensemble) |
| paire 3 | **GND** (les deux fils ensemble) : retour d'alimentation et masse commune du bus |
| paire 4 | réserve |

- Les portes se branchent **en guirlande**, pas en étoile. **Toutes les masses sont reliées.**
- **Câble court (quelques mètres) : aucune résistance de 120 Ω** avec ces modules. Elles écrasent le niveau
  qui code les « 1 » et les messages arrivent déformés. Retirer celles soudées sur les modules si besoin.
- **Câble long** : 120 Ω aux deux bouts **et** une polarisation forte à une extrémité (≈ 470 Ω de A vers +5 V,
  470 Ω de B vers la masse) ; envoyer 9-12 V dans le câble avec un régulateur 5 V par porte.

## 4. Programmer les portes

À faire **une fois par carte**, programmateur ISP branché sur la carte, carte alimentée.
Outils : `uvx` (paquet `uv`), qui lance PlatformIO et avrdude sans installation.

### 4.1 Le décodeur ATmega16 (connecteur P2) : en général rien à faire

Les cartes montées l'ont déjà. Pour le vérifier ou le réécrire, voir
[receiver/atmega16-decoder/README.md](receiver/atmega16-decoder/README.md)
(firmware d'origine, fusibles **lfuse 0x1F / hfuse 0xC8**).

### 4.2 L'ATmega328PB (connecteur P1)

**Fusibles : obligatoire sur une puce neuve.** D'usine elle tourne à **1 MHz** au lieu de 16 MHz : la porte
semble vivante mais rien ne communique (c'est *la* panne qui nous a coûté une soirée).

```bash
AD=$(ls -d ~/.platformio/packages/tool-avrdude* | head -1)   # présent après un premier « uvx platformio run »
$AD/avrdude -C $AD/avrdude.conf -c usbtiny -p m328pb -U lfuse:r:-:h      # 0x62 = d'usine (1 MHz)
$AD/avrdude -C $AD/avrdude.conf -c usbtiny -p m328pb -U lfuse:w:0xF7:m   # résonateur 16 MHz
```

(`0xFF` convient aussi : c'est également 16 MHz.) Puis le firmware :

```bash
cd v2/receiver
uvx platformio run -t upload          # USBtinyISP ; -e rs485-usbasp ou -e rs485-arduinoisp sinon
```

**Adresse de la porte** (cavaliers à souder, vers la masse) :

| SJ5 | SJ4 | SJ3 | Adresse | Porte | Clignotements au démarrage |
|---|---|---|---|---|---|
| | | | 21 | **ligne d'arrivée** | 1 |
| | | ● | 22 | porte 2 | 2 |
| | ● | | 23 | porte 3 | 3 |

Détails, LED et tests : [receiver/README.md](receiver/README.md).

## 5. Programmer le contrôleur ESP32

Une fois par USB, ensuite tout se met à jour **sans fil** depuis la page Système.

```bash
# 1. MicroPython, variante OTA (micropython.org/download/ESP32_GENERIC → « Support for OTA »)
uvx esptool --port /dev/ttyUSB0 flash-id                                   # ESP32, 4 Mo minimum
uvx esptool --chip esp32 --port /dev/ttyUSB0 erase-flash
uvx esptool --chip esp32 --port /dev/ttyUSB0 write-flash 0x1000 ESP32_GENERIC-OTA-xxxx.bin

# 2. RicinoNext (le script compile les modules en .mpy : indispensable, voir §8)
cd v2/controller
tools/deploy.sh /dev/ttyUSB0
```

⚠ Utiliser `esptool` de PyPI (`uvx esptool`), pas le paquet Debian (`stub_flasher_32.json` introuvable).

## 6. Configurer

1. Sans réseau configuré, l'ESP32 ouvre le point d'accès **RicinoNext-XXXX** : s'y connecter avec un
   téléphone, la page **Système** s'ouvre (sinon `http://192.168.4.1/system.html`).
2. **Wi-Fi** : choisir le réseau, le mot de passe et un **nom d'hôte** → la page devient `http://<nom>.local/`.
3. **Matériel** : liaison **RS485**, TX 26, RX 25, **57600** ; « Gates détectées » doit lister les portes (21…).
   Options : feux de départ (GPIO 27), bouton (GPIO 33). Définir un mot de passe d'administration.
4. Page de course, **Réglages > Matériel** : **Gates** = nombre de portes branchées.
5. **Garage** : « 📡 Lire une nouvelle voiture » puis passer chaque voiture devant la ligne d'arrivée,
   et la nommer (jusqu'à 50 voitures).
6. **Réglages > Pilotes** : nom, couleur, et la **voiture** de chaque pilote.

## 7. Première course

- **Réglages > Course** : tours / temps / libre, départ normal ou lancé, décompte, mode solo.
- **Start** (ou le bouton physique) : décompte, puis GO. La page passe en vue course (gros chrono).
- Un passage ne compte qu'après **3 s sans détection** de la voiture sur la porte, et les portes doivent être
  passées **dans l'ordre** (une porte ratée est tolérée de temps en temps, voir [controller/README.md](controller/README.md)).
- À l'arrivée : **résultats** (podium, graphique, détail), et l'onglet **🏆 Records** garde les top 10.
- Vidéoprojecteur : `http://<nom>.local/#display` (cliquer une fois pour autoriser les bips).

## 8. Dépannage

| Symptôme | Cause probable | Que faire |
|---|---|---|
| Porte : LED qui reste longtemps allumée, rien ne communique | ATmega328PB à **1 MHz** (fusibles d'usine) | §4.2 : lfuse = 0xF7 |
| Programmation : « expected signature … 1E 95 0F », reçu `1E 95 16` | la carte porte un **328PB** | le projet cible déjà le 328PB ; ne pas forcer avec `-F` |
| Programmation : « initialization failed » | programmateur mal branché, carte non alimentée | vérifier P1 (328PB) ou P2 (ATmega16), sens, alimentation |
| Les réponses arrivent en octets `00` | résistances de **120 Ω** sur un câble court | les retirer (§3) |
| Aucune porte ne répond | TXD/RXD inversés, A/B inversés, masse manquante | inverser TXD/RXD ; tester avec [receiver/README.md](receiver/README.md) « Tests de mise au point » |
| ESP32 ne répond plus, port série : `WiFi Out of Memory` | modules `.py` compilés au démarrage | réinstaller avec `tools/deploy.sh` (modules `.mpy`) |
| `stub_flasher_32.json` introuvable | esptool de Debian | `uvx esptool` |
| Une porte ratée annule des tours | détections manquées répétées | position du capteur, tolérance (Réglages > Matériel) |

Outils de diagnostic intégrés : page **Système > Diagnostic de la liaison** (compteurs par porte, écoute,
test), et **Réglages > Debug** (CPU, mémoire, flash). Le port série de l'ESP32 (`uvx mpremote connect
/dev/ttyUSB0 repl`) affiche les erreurs de démarrage.

## 9. Sans matériel : le simulateur sur PC

Le même code tourne sur PC avec des portes et des voitures simulées, pour découvrir l'interface ou
développer :

```bash
cd v2/controller
python3 main.py                      # puis http://localhost:8080/
python3 main.py --speed 5            # course accélérée
```

## Pour aller plus loin

| Document | Contenu |
|---|---|
| [controller/README.md](controller/README.md) | fonctions, règles de comptage, protocole WebSocket, API, organisation du code |
| [controller/INSTALL.md](controller/INSTALL.md) | checklist d'installation détaillée du contrôleur |
| [receiver/README.md](receiver/README.md) | firmware des portes, LED, protocole RS485, tests sur table |
| [receiver/atmega16-decoder/README.md](receiver/atmega16-decoder/README.md) | décodeur ATmega16 d'origine |

Tests automatiques (PC, sans matériel) : `controller/test_*.py` et `receiver/test/test_receiver.py`.
