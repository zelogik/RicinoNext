# RicinoNext — récepteur (gate) · firmware v2

Un seul firmware pour toutes les gates. Il tourne sur l'**ATmega328PB** de la carte récepteur
(PCB « IRDA_I2C » 2021-09), entre le décodeur IR (ATmega16, protocole Robitronic, UART 38400)
et le contrôleur ESP32. La lumière, le son et les feux sont gérés par le contrôleur, pas par les gates.

## Liaison avec le contrôleur

| Option de compilation | Liaison | Broches du 328 |
|---|---|---|
| `LINK_RS485=1` (défaut, env `rs485`) | RS485 57600 8N1, module à direction automatique (HW-519) | **D8 = PB0 = RX** (J1 broche 6), **D9 = PB1 = TX** (J1 broche 5), imposées par AltSoftSerial (Timer1) |
| `LINK_RS485=0` (env `i2c`) | I2C esclave, comme avant | SDA/SCL du connecteur J1 |

Câblage RS485 d'une gate, **tout sur le connecteur J1** : module HW-519 alimenté en **5 V** (J1 1-2, ordre
VCC/GND à vérifier au multimètre), son **RXD/TXD** sur **J1 6 (PB0) / J1 5 (PB1)** (si la gate ne répond pas,
inverser les deux fils), **A+/B-** sur le bus, masse commune.

Connecteurs de la carte (lus sur le schéma `Schematic_IRDA_I2C_2021-09-16.pdf`, à confirmer au multimètre) :

| Connecteur | Broches |
|---|---|
| **J1** (principal) | 1-2 alimentation, 3 SCL (PC5), 4 SDA (PC4), 5 PB1 (D9), 6 PB0 (D8) |
| **P1** (1,27 mm) | **ISP de l'ATmega328** : 1 GND, 2 VCC, 3 MOSI, 4 MISO, 5 SCK, 6 RESET |
| **P2** (1,27 mm) | ISP de l'ATmega16 (décodeur) : **ne pas y toucher** |

## Adresse de la gate

Cavaliers à souder **SJ3 / SJ4 / SJ5** (PD5 / PD6 / PD7), lus avec pull-up : fermé = relié à la masse.

| SJ5 | SJ4 | SJ3 | Adresse | Rôle |
|---|---|---|---|---|
| - | - | - | 21 | **ligne d'arrivée** (gate 1) |
| - | - | ● | 22 | gate 2 |
| - | ● | - | 23 | gate 3 |
| - | ● | ● | 24 | … jusqu'à 28 |

⚠ Vérifier sur la carte que les cavaliers relient bien PD5-PD7 à la **masse**. S'ils vont au VCC,
l'adresse restera 21 quelle que soit la position : il faudrait alors inverser la lecture dans `readAddress()`.

## Compiler et flasher

La carte est dessinée pour un ATmega328P, mais elle porte un **ATmega328PB** (signature `1E 95 16`) : le projet
cible le 328PB. AltSoftSerial est inclus dans `lib/`, avec le 328PB ajouté (la version publiée ne le connaît pas).
Pour une carte avec un vrai 328P (« expected signature … 1E 95 0F »), mettre `board = ATmega328P` dans `platformio.ini`.

Programmateur **ISP** sur **P1** : **USBtinyISP** par défaut (ou USBasp, ou un Arduino avec « ArduinoISP ») :

```
cd receiver
uvx platformio run                             # compiler (RS485) ; -e i2c pour l'ancienne liaison
uvx platformio run -t fuses                    # OBLIGATOIRE 1re fois : 16 MHz externe (sinon 1 MHz)
uvx platformio run -t upload                   # flasher avec l'USBtinyISP
uvx platformio run -e rs485-usbasp -t upload   # ou avec un USBasp
uvx platformio run -e rs485-arduinoisp -t upload --upload-port /dev/ttyACM0   # ou un Arduino en ISP
```

⚠ **Fusibles** : les ATmega328PB des cartes sortent d'usine avec lfuse = **0x62** (oscillateur interne
8 MHz ÷ 8 = **1 MHz**). Tout tourne alors 16 fois trop lentement : 57600 bauds deviennent 3600, la gate
ne comprend rien et ses réponses arrivent en octets `00`. Il faut lfuse = **0xF7** (résonateur 16 MHz) :
`uvx platformio run -t fuses`, ou directement `avrdude -c usbtiny -p m328pb -U lfuse:w:0xF7:m`.
Vérifier : `avrdude -c usbtiny -p m328pb -U lfuse:r:-:h` doit afficher `0xf7`.

Câblage du programmateur : son connecteur ISP standard (2,54 mm) n'a **pas le même ordre** que P1, il faut
relier fil à fil **par nom de signal** :

| Signal | ISP standard 6 broches | P1 de la carte |
|---|---|---|
| MISO | 1 | 4 |
| VCC | 2 | 2 |
| SCK | 3 | 5 |
| MOSI | 4 | 3 |
| RESET | 5 | 6 |
| GND | 6 | 1 |

« initialization failed » ou « target doesn't answer » : vérifier les fils et l'alimentation, puis ralentir
l'horloge de programmation : `uvx platformio run -t upload --upload-flags="-e -B 32"`.

⚠ Brancher le programmateur sur **P1** (ATmega328) et jamais sur **P2** : `-t upload` efface la puce,
et sur l'ATmega16 cela détruirait le firmware du décodeur Robitronic (une copie existe dans
`Software/Receiver/irda_robi/atmega16_dump/` du dépôt d'origine). Arduino IDE : ouvrir
`RicinoNext-receiver/RicinoNext-receiver.ino`, carte « ATmega328 » de **MiniCore**, variante **328PB**, 16 MHz externe,
bibliothèque **AltSoftSerial** du dossier `lib/` (copier dans le dossier libraries de l'IDE).

Taille : 5,4 Ko de flash, 573 octets de RAM (RS485).

## LED d'état

| LED | Signification |
|---|---|
| clignotement rapide | aucun contrôleur |
| clignotement lent (1 s) | contrôleur présent, au repos |
| allumée, brèves coupures | course en cours |
| double clignotement rapide | course, mais décodeur muet ou contrôleur injoignable |
| brève inversion | une voiture vient d'être détectée |

## Gestion des erreurs (nouveau)

- La boucle ne bloque jamais (la v1 attendait jusqu'à 5 ms par octet).
- Trames du décodeur vérifiées : octet de longueur plausible, délai entre octets, resynchronisation
  (la v1 pouvait déborder de son tableau de 13 octets sur une longueur aberrante).
- File de 8 passages : plusieurs voitures entre deux interrogations ne sont plus perdues.
- **Bus coupé en pleine course : le décodeur continue** (la v1 le redémarrait au retour du bus,
  et tous les temps suivants étaient faux).
- RS485 : CRC sur chaque trame, et réponse renvoyée à l'identique si le contrôleur l'a perdue (commande `02`).
- Compteurs remontés au contrôleur dans le paquet d'état `0x82` : erreurs décodeur, passages perdus
  (file pleine), erreurs de liaison, file, version, drapeaux décodeur muet / liaison perdue.
  Ils apparaissent dans le message `debug` de la page de course.

## Protocole

Paquets de 13 octets identiques en I2C et RS485, trames RS485 et commandes : voir l'en-tête de
`RicinoNext-receiver.ino` et `controller/ricino/rs485.py`. CRC-8 Dallas/Maxim partout.

## Tests de mise au point sur table

- `bringup/` : test RS485 minimal par registres (`T21` → la LED bascule, réponse `G21`, 9600 bauds) ;
  env `pintest` : PB1 bas 1 s / haut 1 s, pour vérifier la broche TX au multimètre.
- `bringup-lib/` : le même test avec SoftwareSerial ou AltSoftSerial (env `altsoft`).
- Côté ESP32 : `uvx mpremote connect /dev/ttyUSB0 run receiver/bringup/rs485_bringup_controller.py`
  (aller-retour) ou `.../rs485_listen.py` (écoute seule, redémarrer la gate pendant l'écoute : `H21`).

Câblage HW-519 vérifié : étiquettes **en droit** (TX de la carte → TXD du module, RX → RXD), sur les deux modules.
Avec ces modules à direction automatique, **pas de résistance de 120 Ω** sur un câble court : elle écrase la
polarisation qui tient les bits à 1, et les octets arrivent déformés. Pour un long câble avec terminaisons,
ajouter une polarisation forte à une extrémité (≈ 470 Ω de A vers +5 V, 470 Ω de B vers la masse).

## Test sur PC (sans carte)

```
python3 receiver/test/test_receiver.py
```

Compile le vrai `.ino` pour le PC (bouchon Arduino dans `test/stub/`) et le fait dialoguer avec le
code Python du contrôleur : adresse, poll, CRC, départ en diffusion, passages, répétition, erreurs
décodeur, file pleine, bus coupé en course, décodeur muet, arrêt.
