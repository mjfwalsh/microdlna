# Spécification fonctionnelle — MicroDLNA

**Version :** 1.0  
**Projet :** MicroDLNA (fork de minidlna)  
**Date :** 2025

---

## 1. Objet du document

Ce document décrit les fonctionnalités du serveur **MicroDLNA** : objectifs, utilisateurs, cas d’usage et limites fonctionnelles.

---

## 2. Contexte et objectifs

### 2.1 Contexte

MicroDLNA est un **serveur DLNA/UPnP-AV** léger et **sans état** (stateless), dérivé de minidlna et du projet microdlna (SourceForge). Il vise un usage sur équipements à ressources limitées (NAS, box, Raspberry Pi, etc.).

### 2.2 Objectifs

- **Servir des médias** (audio, vidéo, images) depuis un répertoire local vers des **renderers** DLNA (téléviseurs, lecteurs réseau, applications mobiles).
- **Faible empreinte mémoire** : pas de base de données, pas d’indexation au démarrage.
- **Indépendance** : pas de dépendances externes (bibliothèques optionnelles).
- **Robustesse au démarrage** : le répertoire média peut ne pas exister au lancement ; le serveur démarre quand même.

### 2.3 Non-objectifs (hors périmètre)

- Pas de **base de données** ni de **recherche** (Search).
- Pas de **tri** côté serveur (GetSortCapabilities renvoie une liste vide).
- Pas de **miniatures** (thumbnails) ni **conversion de formats**.
- Pas de **multi-répertoires** : un seul `media_dir` par instance.

---

## 3. Utilisateurs et acteurs

| Acteur | Description |
|--------|-------------|
| **Administrateur** | Installe et configure le serveur (fichier de config, options en ligne de commande). |
| **Client DLNA / Renderer** | Télévision, lecteur réseau, application mobile qui découvrent le serveur via SSDP et parcourent / lisent les médias. |

---

## 4. Fonctionnalités principales

### 4.1 Découverte UPnP (SSDP)

- **Annonces périodiques** : le serveur envoie des NOTIFY SSDP sur le multicast `239.255.255.250:1900` à intervalle configurable (`notify_interval`, défaut 895 s).
- **Réponse aux M-SEARCH** : les requêtes SSDP des clients sont traitées (recherche de dispositifs et de services).
- **Bye-bye** : à l’arrêt, envoi de messages SSDP « bye » pour retirer le serveur de la liste des dispositifs visibles.

### 4.2 Description du dispositif et des services (UPnP)

Le serveur expose les documents XML requis pour un Media Server DLNA :

- **Root Device** : `/rootDesc.xml`
- **Content Directory** : `/ContentDir.xml`, contrôle `/ctl/ContentDir`, événements `/evt/ContentDir`
- **Connection Manager** : `/ConnectionMgr.xml`, contrôle `/ctl/ConnectionMgr`, événements `/evt/ConnectionMgr`
- **X_MS_MediaReceiverRegistrar** (optionnel) : `/X_MS_MediaReceiverRegistrar.xml` et URLs associées

Les descriptions sont générées dynamiquement (URLs, UUID, nom convivial, interfaces).

### 4.3 Répertoire de contenu (Content Directory)

- **Browse** : parcours du répertoire média par ObjectID.  
  - ObjectID = chemin relatif sous `media_dir` (chaîne vide = racine).  
  - Réponse DIDL-Lite avec dossiers (`container`) et fichiers reconnus (`item`), avec pagination (StartingIndex, RequestedCount).
- **GetSearchCapabilities** : renvoie une chaîne vide (recherche non supportée).
- **GetSortCapabilities** : renvoie une chaîne vide (tri non supporté).
- **GetProtocolInfo** : renvoie la liste des profils MIME supportés (dérivée des types dans `mime.c`).
- **Search** : non supporté (réponse d’erreur appropriée).

### 4.4 Diffusion des médias (streaming)

- **URL de lecture** : les items sont accessibles via `/MediaItems/<chemin_relatif>`.
- **HTTP** : GET avec prise en charge des **Range** pour la reprise et le time-seek.
- **En-têtes DLNA** : `Content-Type`, `Transfer Mode`, `TimeSeekRange.dlna.org` le cas échéant.
- **Sous-titres** : recherche automatique d’un fichier `.srt` homonyme pour un média et envoi de l’URL en en-tête (ex. `CaptionInfo.sec`).
- **Envoi des fichiers** : utilisation de `sendfile()` (Linux, etc.) lorsque disponible pour de bonnes performances.

### 4.5 Icônes du dispositif

- **URL** : `/icons/<nom>` (ex. `sm.png`, `lrg.png`, `sm.jpg`, `lrg.jpg`).
- Icônes embarquées (données dans `icons.h`).

### 4.6 Événements UPnP (GENA)

- **SUBSCRIBE** : enregistrement des abonnés avec Callback et Timeout.
- **UNSUBSCRIBE** : désabonnement via SID.
- Gestion des timeouts et envoi des notifications aux abonnés (intégration dans la boucle principale avec `select`).

---

## 5. Configuration et exécution

### 5.1 Options obligatoires

- **`-D` / `--media-dir`** : répertoire à publier (obligatoire).

### 5.2 Options de configuration

| Option | Description | Défaut |
|--------|-------------|--------|
| `-f` / `--config-file` | Fichier de configuration (format `cle=valeur`) | — |
| `-p` / `--port` | Port HTTP (1–65535) | 2800 |
| `-i` / `--network-interface` | Interfaces (liste séparée par des virgules) | Toutes |
| `-c` / `--max-connections` | Nombre max de connexions simultanées | 10 |
| `-t` / `--notify-interval` | Intervalle des annonces SSDP (secondes) | 895 |
| `-U` / `--uuid` | UUID du dispositif | Généré si non fourni |
| `-F` / `--friendly-name` | Nom affiché sur les clients | Hostname (sans domaine) |
| `-u` / `--user` | Utilisateur (uid ou nom) sous lequel tourner | Utilisateur courant |
| `-L` / `--log-file` | Fichier de log | — |
| `-l` / `--log-level` | Niveau : off, error, info, debug | — |
| `-P` / `--pid-file` | Fichier PID | — |
| `-d` / `--debug` | Mode debug (pas de démonisation, log debug) | — |
| `-v` / `--verbose` | Messages verbeux | — |
| `-S` / `--mode-systemd` | Mode compatible systemd (premier plan, pas de timestamps) | — |
| `-g` / `--foreground` | Rester au premier plan | — |

### 5.3 Fichier de configuration

Format : une option par ligne, `nom=valeur`. Les tirets en ligne de commande s’écrivent avec des underscores (ex. `media_dir`, `log_file`). Exemple :

```ini
media_dir=/var/media
user=nobody
pid_file=/var/run/microdlna.pid
log_file=/var/log/microdlna.log
```

### 5.4 Comportement au démarrage

- Résolution de `media_dir` en chemin absolu (`realpath`) au premier accès ; le répertoire peut être absent au démarrage.
- Ouverture du socket SSDP (réception UDP 1900) et du socket HTTP (TCP, port configuré).
- Chargement des interfaces réseau (optionnel : restriction à une liste).
- Au premier accès au répertoire média, `chdir(media_dir)` ; si le répertoire n’existe pas, les requêtes Browse ou lecture échouent (503 / 404 selon le cas).

### 5.5 Signaux

- **SIGTERM / SIGINT** : arrêt propre (goodbye SSDP, fermeture des sockets, suppression du fichier PID).
- **SIGHUP** : rechargement des interfaces réseau (sockets SSDP notify).
- **SIGPIPE** : ignoré.

---

## 6. Types de médias reconnus

Les types sont définis dans `mime.c` par extension. Principaux exemples :

- **Vidéo** : avi, mkva, mkv, mp4, m4v, mov, webm, wmv, etc.
- **Audio** : aac, flac, mp3, oga, ogg, wav, wma, etc.
- **Image** : bmp, gif, jpeg, jpg, png, webp, etc.

Les fichiers dont l’extension n’est pas reconnue sont **ignorés** dans le listing (Browse) et ne sont pas servis comme items média.

---

## 7. Sécurité et contraintes

- **Confinement du chemin** : tout chemin (ObjectID, URL de fichier) est validé pour rester sous `media_dir` (pas de sortie par `..` ou liens symboliques non résolus).
- **Filtrage par interface** : les connexions HTTP peuvent être limitées aux interfaces configurées ; les connexions depuis une IP non autorisée sont rejetées (sauf localhost).
- **Droits** : le serveur peut passer sous un utilisateur donné (`-u`) après avoir ouvert les sockets (droits réduits pour la suite de l’exécution).

---

## 8. Limites et dépendances techniques

- **Un seul répertoire média** par instance.
- **Pas de cache** : chaque Browse lit le système de fichiers à la demande.
- **Pas de transcodage** : les clients doivent accepter les formats natifs.
- **Threads** : une connexion HTTP par requête (thread dédié pour le transfert de fichier), plafonnée par `max_connections`.
- **Build** : C11, pthreads, compilation via `configure.sh` + `make` (pas de dépendances externes au-delà de la libc et des headers POSIX/Linux).

---

## 9. Références

- README du projet
- Page de manuel `microdlnad.8` (générée depuis `microdlna.pod`)
- Standards : UPnP Device Architecture, UPnP AV ContentDirectory, DLNA Guidelines
