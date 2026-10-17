# Installation sur le matériel : checklist

Le code n'a encore jamais tourné sur une vraie carte : les étapes **⚠ à vérifier** sont
celles où un réglage est le plus probable.

## 1. Matériel

- [ ] **ESP32** avec 4 Mo de flash (ex. ESP32 DevKit v1 / WROOM-32). La variante OTA de MicroPython demande 4 Mo.
- [ ] Câble USB **de données** (pas seulement de charge).
- [ ] Récepteurs RicinoNext (ATmega16 + ATmega328), décodeur ATmega16 fonctionnel.
- [ ] Programmateur **USBtinyISP** (ou USBasp, ou un Arduino en « ArduinoISP ») sur le connecteur **P1** (ISP de l'ATmega328, pas P2),
      relié **fil à fil par nom de signal** (l'ordre de P1 diffère du connecteur ISP standard, voir le README du récepteur).
- [ ] **Fusibles de chaque ATmega328PB** (une fois par puce, OBLIGATOIRE : d'usine elles tournent à 1 MHz) :
      `cd receiver && uvx platformio run -t fuses`, puis vérifier lfuse = `0xf7`.
- [ ] **Firmware récepteur** sur chaque gate : `cd receiver && uvx platformio run -t upload` (voir [../receiver/README.md](../receiver/README.md)).
- [ ] **Adresse de chaque gate par cavaliers SJ3-SJ5** (plus besoin de recompiler) :
  - [ ] aucun cavalier = 21 = **ligne d'arrivée** (gate 1)
  - [ ] SJ3 = 22 = gate 2, SJ4 = 23 = gate 3 (temps intermédiaires, dans l'ordre de passage)
  - [ ] ⚠ vérifier que les cavaliers vont à la masse (sinon l'adresse reste 21)
- [ ] Tester chaque gate seule sur la table : LED en clignotement lent dès que le contrôleur l'interroge.

## 2a. Câblage RS485 (recommandé pour une vraie piste) ⚠ à vérifier

Modules **HW-519** (MAX485 à direction automatique : VCC / TXD / RXD / GND côté carte, A+ / B- / GND côté bus).

- [ ] Côté ESP32 : module alimenté en **3,3 V** (VCC du module sur le 3V3 de l'ESP32, GND sur GND).
      Vérifier au multimètre que la broche RXD du module ne dépasse pas 3,3 V.
- [ ] ESP32 **GPIO 26 (TX) → TXD** du module, **GPIO 25 (RX) → RXD**. Si aucune gate ne répond,
      **inverser TXD/RXD** : selon les fabricants, l'étiquette est vue de la carte ou du module, et l'inversion n'abîme rien.
- [ ] Bus : **A+ avec A+**, **B- avec B-** sur tous les modules, en paire torsadée (ex. une paire d'un câble RJ45),
      plus la **masse commune**. Les gates se branchent **en guirlande** (pas en étoile).
- [ ] **Terminaison** : sur un câble court (quelques mètres), **aucune résistance de 120 Ω** avec ces modules à direction
      automatique (elle écrase la polarisation des bits à 1 : octets déformés en `00`). Retirer celles des modules
      si elles sont soudées. Câble long : 120 Ω aux deux bouts **et** polarisation forte à une extrémité
      (≈ 470 Ω de A vers +5 V, 470 Ω de B vers la masse).
- [ ] Étiquettes HW-519 **en droit** : TX de la carte → **TXD** du module, RX → **RXD** (vérifié sur les deux côtés).
- [ ] Côté gate, tout sur le connecteur **J1** : HW-519 alimenté en **5 V** (J1 1-2), RXD/TXD sur **J1 6 (PB0) / J1 5 (PB1)**, firmware récepteur `rs485`.
- [ ] Page système > Matériel > Liaison : **RS485**, TX 26, RX 25, **57600**, Enregistrer.

## 2b. Câblage I2C (gates proches, câble court) ⚠ à vérifier

- [ ] SDA → **GPIO 21**, SCL → **GPIO 22** (modifiable ensuite dans la page système), **masse commune**.
- [ ] **Niveaux logiques** : l'ESP32 est en **3,3 V** et ne tolère pas 5 V. Si les ATmega328 sont en 5 V,
      mettre un **convertisseur de niveaux bidirectionnel I2C** (module BSS138). Des pull-ups tirés à 5 V
      directement sur l'ESP32 l'abîment, et des pull-ups à 3,3 V peuvent être trop bas pour un ATmega en 5 V.
- [ ] **Pull-ups** de 4,7 kΩ sur SDA et SCL, côté 3,3 V et côté 5 V (souvent déjà présents sur le module).
- [ ] Longueur de câble : I2C n'aime pas les longues distances. Au-delà de 1 à 2 m, baisser la fréquence
      (50 kHz) ou passer sur un bus adapté plus tard.

## 2c. Feux de départ (ruban WS2812, optionnel)

- [ ] Données du ruban sur **GPIO 27** (modifiable), via une résistance de 330 Ω en série.
- [ ] Ruban alimenté en **5 V à part** (8 LED ≈ 0,5 A au maximum), **masses reliées** à l'ESP32.
- [ ] ⚠ Les WS2812 en 5 V lisent en général bien le 3,3 V de l'ESP32 sur un fil court. Si les couleurs sont
      erratiques : raccourcir le fil, ou ajouter un adaptateur de niveau (74AHCT125) sur la ligne de données.
- [ ] Page système > Matériel : cocher **Feux de départ**, GPIO, nombre de LED, luminosité, puis Enregistrer.

## 2d. Bouton ou interrupteur Start/Stop (optionnel)

- [ ] Contact entre **GPIO 33** (modifiable) et la **masse**. Aucune résistance : le pull-up interne est utilisé.
      Ne pas prendre les GPIO 34 à 39 (pas de pull-up interne).
- [ ] Pour essayer sans rien câbler : GPIO **0** = bouton **BOOT** de la carte de développement.
- [ ] Page système > Matériel : cocher **Bouton Start/Stop**, choisir le GPIO et le type :
  - **Bouton poussoir** : appui = départ, **appui long 1 s = arrêt** (un coup involontaire n'arrête pas la course) ;
  - **Interrupteur** : ON = départ, OFF = arrêt (un interrupteur déjà sur ON au démarrage ne lance rien).
- [ ] Câble long jusqu'au bouton : fil torsadé avec la masse, et éventuellement un condensateur de 100 nF entre le GPIO et la masse.

## 3. Outils sur le PC

- [ ] esptool et mpremote **depuis PyPI**, via `uvx` (rien à installer) : `uvx esptool version`, `uvx mpremote version`.
      ⚠ Ne pas utiliser le paquet Debian `esptool` (4.7.0+dfsg) : il lui manque le flasheur de l'ESP32 classique
      (`stub_flasher_32.json` introuvable).
- [ ] Accès au port série : `sudo usermod -aG dialout $USER`, puis se reconnecter.
- [ ] Repérer le port : brancher l'ESP32, puis `ls /dev/ttyUSB* /dev/ttyACM*`.

## 4. Flasher MicroPython (une seule fois, par USB)

- [ ] Sur https://micropython.org/download/ESP32_GENERIC/ prendre la **variante OTA** (« Support for OTA »),
      dernière version stable. Télécharger :
  - [ ] le `.bin` complet, pour l'USB ;
  - [ ] le `.app-bin`, pour les futures mises à jour OTA.
- [ ] Identifier la carte (lecture seule) : `uvx esptool --port /dev/ttyUSB0 flash-id` → ESP32, 4 Mo minimum.
- [ ] `uvx esptool --chip esp32 --port /dev/ttyUSB0 erase-flash`
- [ ] `uvx esptool --chip esp32 --port /dev/ttyUSB0 write-flash 0x1000 ESP32_GENERIC-OTA-xxxx.bin`
- [ ] Vérifier : `uvx mpremote connect /dev/ttyUSB0 exec "import sys; print(sys.implementation)"`

## 5. Installer RicinoNext

- [ ] `cd controller && tools/deploy.sh /dev/ttyUSB0`
- [ ] Suivre le démarrage : `uvx mpremote connect /dev/ttyUSB0 repl`, puis Ctrl-D (redémarrage logiciel).
      Attendu : `Wi-Fi: access point RicinoNext-XXXX`, `I2C gates found: [...]`, `RicinoNext 1.0.0 ready`.
- [ ] ⚠ Si une erreur Python s'affiche (incompatibilité MicroPython) : noter la trace et me l'envoyer.
      Le mode secours démarre alors tout seul (point d'accès `RicinoNext-recovery`, page d'envoi de fichier).

## 6. Configuration

- [ ] Sur le téléphone, rejoindre le Wi-Fi **RicinoNext-XXXX**. La page système s'ouvre seule,
      sinon aller sur http://192.168.4.1/system.html.
- [ ] Wi-Fi : **Scanner**, choisir le réseau, saisir le mot de passe, choisir le **nom d'hôte**, puis Enregistrer.
- [ ] Rejoindre ce réseau, ouvrir `http://<nom>.local/`.
      Si `.local` ne marche pas (certains Android), utiliser l'IP affichée dans le REPL ou dans la box.
- [ ] Définir un **mot de passe d'administration** (Matériel), et un mot de passe pour le point d'accès de secours.

## 7. Tests sur la carte

- [ ] **Mode démo** (Matériel > Mode démo) : lancer une course simulée, vérifier l'affichage et la fluidité.
      Regarder la **mémoire libre** dans Appareil : au moins 30 Ko pendant une course.
- [ ] Désactiver le mode démo, puis vérifier **Gates détectées** : 21 (et 22, 23).
  - [ ] ⚠ Gate manquante ou détection intermittente : vérifier niveaux et pull-ups, baisser la fréquence
        à 50 kHz, puis essayer **I2C logiciel (SoftI2C)**.
- [ ] Réglages > Pilotes > 📡 : passer chaque voiture sur la ligne, attribuer les tags, **Enregistrer**.
- [ ] Redémarrer l'ESP32 : les pilotes et réglages sont toujours là (`/race.json`).
- [ ] Course réelle de 3 tours : comparer quelques temps au tour avec un chronomètre.
- [ ] ⚠ Avec plusieurs gates : vérifier que les temps des secteurs (S1, S2, S3) sont cohérents.
      Les horloges des récepteurs démarrent à quelques ms d'écart (commande `0x8A` envoyée gate par gate).
- [ ] Recharger la page en pleine course : l'historique des tours est récupéré.
- [ ] Vidéoprojecteur : `http://<nom>.local/#display`, cliquer une fois pour activer les bips.
- [ ] Feux : lancer une course, vérifier orange → 5 feux rouges → vert au GO → damier à l'arrivée.
- [ ] Bouton : appui = départ (les pages passent en vue course), appui long = arrêt.

## 8. Tester l'OTA (avant d'en avoir besoin)

- [ ] Système > Mise à jour : renvoyer `data/index.html` (compressé automatiquement), puis recharger la page.
- [ ] Renvoyer un `.py` (ex. `ricino/core.py`), puis redémarrer.
- [ ] Firmware : envoyer le `.app-bin` de la même version, et vérifier que la **partition active** a changé (ota_0 ↔ ota_1).
- [ ] Optionnel : envoyer un `.py` volontairement cassé, vérifier que le **mode secours** permet de le corriger.

## Modules précompilés (.mpy) : obligatoire

Le code est envoyé sur la carte **précompilé en `.mpy`** (`tools/deploy.sh` le fait avec `uvx mpy-cross`).
Compiler les `.py` sur la carte prend tant de mémoire que le Wi-Fi ne trouve plus la sienne au démarrage
(« OSError: WiFi Out of Memory », la carte ne répond plus). `mpy-cross` doit avoir la **même version que le
MicroPython de la carte** (1.29 ici : `uvx --from "mpy-cross==1.29.*" mpy-cross -o core.mpy ricino/core.py`).

- [ ] Mise à jour OTA d'un module : envoyer le **`.mpy`** (la page système retire alors l'ancien `.py`).
- [ ] Si la carte ne répond plus après une mise à jour : `uvx mpremote connect /dev/ttyUSB0 repl` montre l'erreur ;
      `tools/deploy.sh /dev/ttyUSB0` réinstalle tout par USB.
