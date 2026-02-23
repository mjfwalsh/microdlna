# Document d'architecture — MicroDLNA

**Version :** 1.0  
**Projet :** MicroDLNA  
**Date :** 2025

---

## 1. Vue d’ensemble

MicroDLNA est un serveur **DLNA/UPnP-AV** monolithique en C (C11), compilé en un unique binaire **microdlnad**. Le modèle d’exécution est **une boucle événementielle** (select) dans le processus principal pour SSDP et acceptation HTTP, avec **threads POSIX** pour traiter les requêtes HTTP (et en particulier le transfert de fichiers).

---

## 2. Principes architecturaux

- **Stateless** : aucune base de données ; le contenu est dérivé du système de fichiers à chaque requête.
- **Faible dépendance** : uniquement la libc, pthreads et les APIs socket/POSIX (pas de libxml, etc.).
- **Single process** : un processus, plusieurs threads pour les connexions HTTP simultanées.
- **Boucle select** : le thread principal gère SSDP, socket d’écoute HTTP, et descripteurs des abonnés GENA.

---

## 3. Structure du code source

```
microdlna/
├── microdlna.c        # Point d’entrée, boucle principale, options, signaux
├── microdlna.conf     # Exemple de fichier de configuration
├── globalvars.h       # Variables globales (port, UUID, media_dir, etc.)
├── configure.sh       # Génération du Makefile
├── Makefile           # Généré par configure.sh
├── gen_version.sh     # Génération version_info.h
├── version.c / version.h
│
├── Réseau et découverte
│   ├── minissdp.c/h   # SSDP : socket réception, NOTIFY, M-SEARCH, goodbyes
│   ├── getifaddr.c/h  # Interfaces réseau, UUID, validation, reload SIGHUP
│   └── utils.c/h      # inet_ntoa_ts, url_escape, sanitise_path, safe_* 
│
├── HTTP et UPnP
│   ├── upnphttp.c/h   # Parsing HTTP, routage des URLs, réponses, threads
│   ├── upnpdescgen.c/h# Génération XML (rootDesc, ContentDir, ConnectionMgr, etc.)
│   ├── upnpsoap.c/h   # Actions SOAP : Browse, Get*Capabilities, GetProtocolInfo
│   ├── upnpevents.c/h # GENA : SUBSCRIBE/UNSUBSCRIBE, file descriptors, timeouts
│   ├── microdlnapath.h# Constantes des chemins HTTP (ROOTDESC_PATH, etc.)
│   └── xmlregex.c/h   # Utilitaires regex/XML pour parsing SOAP
│
├── Contenu et médias
│   ├── mediadir.c/h   # chdir_to_media_dir, realpath(media_dir)
│   ├── dirlist.c/h    # Listing répertoire, content_entry, tri, MIME
│   ├── mime.c/h       # Table d’extensions → type MIME (audio/video/image/text)
│   ├── sendfile.c/h   # Envoi fichier (sendfile ou read/write)
│   └── icons.h        # Données des icônes (sm.png, lrg.png, etc.)
│
├── Entrées/sorties et concurrence
│   ├── stream.c/h     # Stream wrapper autour d’un fd (buffer, printf, chunk)
│   ├── threads.c/h    # create_thread, max_connections, pthread detach
│   └── log.c/h        # Niveaux de log, sortie fichier/console
│
└── Documentation / déploiement
    ├── microdlna.pod  # Source de la page man
    └── docs/
        ├── SPEC_FONCTIONNELLE.md
        └── ARCHITECTURE.md
```

---

## 4. Flux d’exécution

### 4.1 Démarrage (`microdlna.c`)

1. **Parse** des options (ligne de commande puis éventuel fichier `-f`).
2. **Initialisation** : UUID par défaut si absent, nom convivial (hostname), vérification de `media_dir` obligatoire.
3. **Sockets** :
   - `open_ssdp_receive_socket()` → socket UDP pour SSDP (réception sur 239.255.255.250:1900).
   - `open_and_conf_http_socket(port)` → socket TCP d’écoute HTTP.
4. **Interfaces** : `reload_ifaces(0, sssdp)` (création des sockets d’envoi SSDP par interface).
5. **Démon** : si pas `--foreground` / `--debug` / `--mode-systemd`, fork + setsid, écriture du PID, réduction des privilèges (`setuid`).
6. **Threads** : `init_threads()` (mutex, attributs pthread détachés).
7. **Signaux** : SIGTERM/SIGINT → arrêt, SIGHUP → rechargement interfaces, SIGPIPE ignoré.
8. **Boucle principale** (voir ci‑dessous).

### 4.2 Boucle principale (`microdlna.c`)

```
┌─────────────────────────────────────────────────────────────────┐
│  Boucle while (!quitting)                                        │
├─────────────────────────────────────────────────────────────────┤
│  1. Calcul du prochain timeout (prochain NOTIFY SSDP).            │
│  2. FD_ZERO(readset/writeset).                                   │
│  3. Ajout à readset : sssdp, shttpl.                             │
│  4. upnpevents_selectfds(&readset, &writeset, &max_fd).           │
│  5. select(max_fd+1, readset, writeset, NULL, timeout).         │
│  6. upnpevents_processfds(); upnpevents_removed_timedout_subs(); │
│  7. Si sssdp actif → process_ssdp_request(sssdp).                │
│  8. Si shttpl actif → accept() → process_upnphttp_http_query().  │
└─────────────────────────────────────────────────────────────────┘
```

- Les **NOTIFY SSDP** sont envoyés lorsque le temps écoulé depuis le dernier envoi dépasse `notify_interval`.
- Chaque **connexion HTTP acceptée** est traitée dans **process_upnphttp_http_query** (dans le thread principal jusqu’à la fin du parsing) ; pour les requêtes de **fichier** (`/MediaItems/...`), un **thread dédié** est créé (`serve_file`) et la boucle principale reprend tout de suite.

### 4.3 Traitement d’une requête HTTP (`upnphttp.c`)

1. **init_upnphttp_struct** : allocation `struct upnphttp`, `sdopen(s)` pour un `stream` sur le socket.
2. **Lecture** de la première ligne (méthode + chemin), vérification HTTP/1.1.
3. **Parsing des en-têtes** : Content-Length, SOAPAction, Callback, SID, NT, Timeout, Range, etc.
4. **Routage par path** :
   - `GET /rootDesc.xml` → `gen_root_desc(st)`.
   - `GET /ContentDir.xml` → `send_content_directory(st)`.
   - `GET /ConnectionMgr.xml` → `send_connection_manager(st)`.
   - `GET /X_MS_MediaReceiverRegistrar.xml` → `send_x_ms_media_receiver_registrar(st)`.
   - `GET /MediaItems/...` → **send_resp_dlnafile** : création d’un thread `serve_file(h)` qui fera `send_file()` et fermera la structure.
   - `GET /icons/...` → **send_resp_icon** (données en mémoire).
   - `POST /ctl/ContentDir` (ou autre control URL) → lecture du body SOAP, dispatch par `req_soap_action` (Browse, GetSearchCapabilities, etc.).
   - SUBSCRIBE / UNSUBSCRIBE → `process_http_subscribe_upnphttp` / `process_http_un_subscribe_upnphttp` (délégué à `upnpevents`).
5. Pour les réponses **synchrones** (descriptions, SOAP, icônes), envoi des en-têtes et du corps puis **delete_upnphttp_struct** dans le même thread.
6. Pour le **streaming de fichier**, le thread principal retourne après avoir créé le thread ; le thread **serve_file** lit les éventuels en-têtes restants, ouvre le fichier, envoie les en-têtes HTTP + DLNA puis **send_file(fd, sendfh, start, end)** et nettoie.

---

## 5. Modules clés

### 5.1 SSDP (`minissdp.c`, `getifaddr.c`)

- **Réception** : un socket UDP lié au groupe multicast 239.255.255.250:1900, avec possibilité de restreindre les interfaces.
- **Envoi** : un socket d’envoi par interface (`lan_addr_s.snotify`) pour les NOTIFY et les réponses M-SEARCH (adresse de destination déduite de l’interface).
- **getifaddr** : liste des interfaces (`lan_addr_s`), validation UUID, `get_interface(client_addr)` pour accepter ou rejeter une connexion HTTP, `reload_ifaces` pour SIGHUP.

### 5.2 HTTP / Stream (`upnphttp.c`, `stream.c`)

- **stream** : encapsule un fd dans une structure avec buffer (BUFFER_SIZE 1024), `stream_printf`, `stream_write`, `chunk_printf` pour HTTP chunked.
- **upnphttp** : garde le `stream`, le fd, l’interface, la méthode, le path, les champs SOAP (ObjectID → remote_dirpath, StartingIndex, RequestedCount), Range, Callback/SID/NT/Timeout pour GENA.

### 5.3 SOAP et répertoire (`upnpsoap.c`, `dirlist.c`, `mediadir.c`)

- **Browse** : extraction des paramètres SOAP (ObjectID, BrowseFlag, Filter, StartingIndex, RequestedCount). ObjectID = chemin relatif sous `media_dir`. Appel à **get_directory_listing(h, &file_count)** qui fait `chdir_to_media_dir()`, `opendir(rel_dir)`, `readdir` + filtrage (fichiers cachés ignorés, MIME inconnu ignoré), tri par type puis nom, pagination. Construction de la réponse DIDL-Lite (containers + items avec res, protocolInfo, size, etc.).
- **mediadir** : `chdir_to_media_dir()` résout une fois `media_dir` en `realpath` et fait `chdir(media_dir)`.

### 5.4 Génération des descriptions (`upnpdescgen.c`)

- Descriptions XML générées **à la volée** dans le `stream` : rootDesc (device, services, URLBase selon l’interface), ContentDir.xml, ConnectionMgr.xml, X_MS_MediaReceiverRegistrar.xml. Les URLs de contrôle et d’événements pointent vers l’IP de l’interface du client.

### 5.5 Transfert de fichiers (`sendfile.c`, `upnphttp.c`)

- **send_file(socketfd, sendfd, offset, end_offset)** : sur Linux utilise `sendfile()` ; sinon boucle read/write. Gère les plages (Range) et les gros fichiers (FILESIZE).
- **Sécurité** : le chemin demandé (`/MediaItems/...`) est **sanitise_path** pour éviter toute sortie hors de `media_dir`. Ouverture du fichier après `chdir_to_media_dir()` avec chemin relatif.

### 5.6 Événements GENA (`upnpevents.c`)

- Abonnés stockés avec callback, SID, timeout. Le thread principal inclut leurs fd dans **select** via `upnpevents_selectfds` et traite les écritures/expirations dans `upnpevents_processfds` et `upnpevents_removed_timedout_subs`. Pas de thread dédié aux événements : tout est piloté par la boucle select.

### 5.7 Threads (`threads.c`)

- **create_thread(start_routine, arg)** : si `active_threads >= max_connections`, retourne -1 ; sinon `pthread_create` avec attribut DETACHED et incrément du compteur. **decrement_thread_count** appelé à la fin de `serve_file` (et équivalents si d’autres traitements en thread existent).

---

## 6. Modèle de données (concepts)

- **media_dir** : chemin absolu du répertoire publié (résolu au premier `chdir_to_media_dir`).
- **ObjectID** : identifiant de contenu = chemin relatif sous `media_dir` (chaîne vide = racine).
- **content_entry** : un élément de listing (dossier ou fichier) avec type (T_DIR / T_FILE), nom, taille, `ext_info` MIME.
- **lan_addr_s** : une interface (adresse, masque, socket notify SSDP, ifindex).
- **upnphttp** : une requête/réponse HTTP en cours (fd, stream, path, paramètres SOAP/GENA, callbacks d’action).

Il n’y a **pas de modèle persistant** : pas de base SQLite ni de cache en mémoire pour le catalogue.

---

## 7. Dépendances de compilation et d’exécution

- **Langage** : C11 (`-std=c11`).
- **Définitions** : `_GNU_SOURCE`, `_LARGEFILE_SOURCE`, `_FILE_OFFSET_BITS=64`.
- **Bibliothèques** : pthread (link `-pthread`).
- **Système** : sockets BSD, `select()`, `sendfile()` (Linux ; sinon émulation read/write), `realpath`, `getpwnam`, `getpwuid`, `dirent`, `stat`/`fstatat`, etc.
- **Génération du Makefile** : `configure.sh` (liste des `.c`, génération des dépendances via `$(CC) -MM`).
- **Page man** : `pod2man` pour générer `microdlnad.8` à partir de `microdlna.pod`.

---

## 8. Diagramme de déploiement (logique)

```
                    ┌──────────────────────────────────────┐
                    │           Processus microdlnad        │
                    │  ┌────────────────────────────────┐  │
                    │  │  Thread principal               │  │
                    │  │  - select(ssdp, http_listen,    │  │
                    │  │    event fds)                   │  │
                    │  │  - SSDP recv / NOTIFY          │  │
                    │  │  - accept() HTTP                │  │
                    │  │  - dispatch requête (ou thread) │  │
                    │  └────────────────────────────────┘  │
                    │  ┌────────────────────────────────┐  │
                    │  │  Threads workers (détachés)      │  │
                    │  │  - serve_file() par connexion   │  │
                    │  │  - max_connections              │  │
                    │  └────────────────────────────────┘  │
                    └──────────────────────────────────────┘
                      │                    │
                      ▼                    ▼
              UDP 239.255.255.250:1900   TCP port (défaut 2800)
              (SSDP)                     (HTTP / SOAP / streaming)
                      │                    │
                      ▼                    ▼
              Clients DLNA (M-SEARCH)    Clients (GET/POST, GET /MediaItems/...)
```

---

## 9. Évolutions et contraintes d’évolution

- **Scalabilité** : limitée par le nombre de threads (`max_connections`) et par l’absence de cache (chaque Browse refait un `opendir`/`readdir`).
- **Sécurité** : renforcement possible (HTTPS, contrôle d’accès par client) nécessiterait des couches supplémentaires (proxy, ou intégration TLS).
- **Fonctionnalités** : ajout de recherche ou de tri impliquerait un index ou un cache (en contradiction avec le choix « stateless » actuel).

Ce document décrit l’état actuel du code ; il peut être mis à jour en cas de refactor ou d’ajout de modules.
