# Documentation des échanges réseau — MicroDLNA

Ce document décrit les protocoles et formats des échanges réseau de MicroDLNA, avec suffisamment de détails pour permettre une réimplémentation compatible (client ou serveur).

**Références code :** SSDP `minissdp.c`, HTTP/streaming `upnphttp.c`, SOAP `upnpsoap.c` + `xmlregex.c`, GENA `upnpevents.c`, descriptions XML `upnpdescgen.c`, chemins `microdlnapath.h`.

---

## 1. Vue d’ensemble des protocoles

| Protocole | Transport | Rôle |
|-----------|------------|------|
| **SSDP** | UDP, multicast 239.255.255.250:1900 | Découverte : réception M-SEARCH, envoi réponses 200 et NOTIFY (alive/byebye) |
| **HTTP/1.1** | TCP | Descriptions (root + SCPD), contrôle SOAP, événements GENA, icônes, streaming média |
| **SOAP** | Corps de requêtes HTTP POST | Actions ContentDirectory (Browse, GetSearchCapabilities, GetSortCapabilities) et ConnectionManager (GetProtocolInfo) |
| **GENA** | HTTP SUBSCRIBE/UNSUBSCRIBE + NOTIFY TCP sortant | Abonnement aux événements et envoi des NOTIFY |
| **DLNA** | En-têtes HTTP sur GET média | Range, transferMode, contentFeatures, realTimeInfo, sous-titres |

**Port HTTP par défaut :** 2800 (configurable, ex. `-p` / `microdlna.conf`).  
**Port SSDP :** 1900 (fixe).  
**Adresse multicast SSDP :** 239.255.255.250.

---

## 2. SSDP (découverte)

### 2.1 Transport

- **Réception :** socket UDP liée sur chaque interface (ou sur l’adresse multicast selon l’OS), port 1900.
- **Envoi :** vers 239.255.255.250:1900 pour les NOTIFY ; en unicast vers l’adresse:port de l’émetteur pour les réponses M-SEARCH.

### 2.2 Types de services (ST / NT / USN)

Le serveur gère exactement les types suivants (ordre utilisé dans le code) :

| Index | Type (ST/NT) | USN |
|-------|---------------------|-----|
| 0 | `uuid:<device-uuid>` | `uuid:<device-uuid>` (sans suffixe) |
| 1 | `upnp:rootdevice` | `uuid:<device-uuid>::upnp:rootdevice` |
| 2 | `urn:schemas-upnp-org:device:MediaServer:` | `uuid:...::urn:schemas-upnp-org:device:MediaServer:1` |
| 3 | `urn:schemas-upnp-org:service:ContentDirectory:` | `...::urn:schemas-upnp-org:service:ContentDirectory:1` |
| 4 | `urn:schemas-upnp-org:service:ConnectionManager:` | `...::urn:schemas-upnp-org:service:ConnectionManager:1` |
| 5 | `urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:` | `...::urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:1` |

Pour les types 2–5, le préfixe `urn:...` est suivi de **`1`** dans ST/NT et USN.  
`<device-uuid>` est l’UUID du périphérique (ex. `uuid:4d696e69-444c-164e-9d41-...`).

### 2.3 Requête entrante : M-SEARCH

- **Ligne de requête (exacte) :** `M-SEARCH * HTTP/1.1\r\n`
- **En-têtes obligatoires (sinon la requête est ignorée) :**
  - **MAN:** valeur `"ssdp:discover"` (guillemets inclus dans la comparaison).
  - **MX:** entier (secondes d’attente max). Absent ou invalide → ignoré.
  - **ST:** une des cibles ci-dessus, ou `ssdp:all`.

Comportement :

- Si **ST** correspond à un type connu → une réponse 200 par type correspondant (pour `uuid:...` exact, une seule réponse ; pour `ssdp:all`, une réponse par type de la liste).
- Correspondance stricte : pas de caractère supplémentaire après le type (ex. pas d’espace avant `\r\n`).

### 2.4 Réponse M-SEARCH (sortante)

Envoyée en unicast vers l’adresse/port de l’émetteur du M-SEARCH. Format :

```http
HTTP/1.1 200 OK\r\n
CACHE-CONTROL: max-age=<v>\r\n
DATE: <date RFC 2822 GMT>\r\n
ST: <search target>\r\n
USN: <usn>\r\n
EXT:\r\n
SERVER: MicroDLNA/1.0\r\n
LOCATION: http://<host>:<port>/rootDesc.xml\r\n
Content-Length: 0\r\n
\r\n
```

- **max-age :** `(notify_interval * 2) + 10` (entier, ex. 895 → 1800).
- **DATE :** ex. `%a, %d %b %Y %H:%M:%S GMT`.
- **LOCATION :** URL de la description racine ; `<host>` = IP d’interface, `<port>` = port HTTP d’écoute (ex. 2800).

### 2.5 NOTIFY ssdp:alive

Envoyé au démarrage (deux fois, 200 ms d’écart) puis périodiquement selon `notify_interval` (défaut 895 s). Destination : 239.255.255.250:1900.

```http
NOTIFY * HTTP/1.1\r\n
HOST:<ssdp_mcast>:<1900>\r\n
CACHE-CONTROL:max-age=<(notify_interval*2)+10>\r\n
LOCATION:http://<host>:<port>/rootDesc.xml\r\n
SERVER: MicroDLNA/1.0\r\n
NT:<service type>\r\n
USN:<usn>\r\n
NTS:ssdp:alive\r\n
\r\n
```

Un NOTIFY par type de service (même liste que ci-dessus).

### 2.6 NOTIFY ssdp:byebye

Envoyé à l’arrêt (deux séries complètes). Destination : 239.255.255.250:1900. Pas de CACHE-CONTROL ni LOCATION.

```http
NOTIFY * HTTP/1.1\r\n
HOST: 239.255.255.250:1900\r\n
NT: <service type>\r\n
USN: <usn>\r\n
NTS: ssdp:byebye\r\n
\r\n
```

---

## 3. HTTP — Base et chemins

### 3.1 Base URL

- **Schéma :** `http://`
- **Host :** doit être exactement l’IP de l’interface sur laquelle le client s’est connecté (vérification stricte anti rebinding). Si le port n’est pas 80, le Host doit être `IP:port` (ex. `192.168.1.10:2800`).
- **Port par défaut :** 2800.

Toute requête sans en-tête Host valide pour l’interface reçoit **400 Bad Request**.

### 3.2 Chemins (définis dans microdlnapath.h)

| Méthode(s) | Chemin | Rôle |
|------------|--------|------|
| GET, HEAD | `/rootDesc.xml` | Description racine (device + services + URLs) |
| GET, HEAD | `/ContentDir.xml` | SCPD ContentDirectory |
| GET, HEAD | `/ConnectionMgr.xml` | SCPD ConnectionManager |
| GET, HEAD | `/X_MS_MediaReceiverRegistrar.xml` | SCPD X_MS_MediaReceiverRegistrar |
| GET, HEAD | `/MediaItems/<chemin_relatif>` | Fichier média (streaming) ou répertoire (traité comme chemin) |
| GET, HEAD | `/icons/sm.png`, `lrg.png`, `sm.jpg`, `lrg.jpg` | Icônes embarquées (PNG/JPEG) |
| POST | (voir SOAP) | Contrôle SOAP ; l’action est déterminée par l’en-tête SOAPAction, pas par le chemin |
| SUBSCRIBE | `/evt/ContentDir`, `/evt/ConnectionMgr`, `/evt/X_MS_MediaReceiverRegistrar` | GENA : abonnement |
| UNSUBSCRIBE | (mêmes chemins) | GENA : résiliation avec SID |

Tout autre chemin → **404 Not Found**.

Pour `/MediaItems`, le préfixe est retiré en interne ; le reste est le chemin relatif sous le répertoire média, normalisé et validé (pas de `..`, etc.) via `sanitise_path`.

---

## 4. Descriptions XML (GET)

- **Content-Type :** `text/xml; charset=utf-8`
- **Connection:** close
- **Transfer-Encoding:** chunked (pour les réponses XML/HTML générées)
- **Server:** MicroDLNA/1.0

Le contenu de `/rootDesc.xml` inclut notamment :

- Device : friendlyName, deviceType (MediaServer), UDN (uuid), manufacturer, modelName, etc.
- **serviceList** avec pour chaque service : serviceType, serviceId, **controlURL**, **eventSubURL**, **SCPDURL** (chemins absolus comme dans microdlnapath.h).

Les SCPD décrivent les actions et variables d’état (ContentDirectory, ConnectionManager, X_MS_MediaReceiverRegistrar). Les URLs de contrôle et d’événements utilisées par les clients sont celles de cette description.

---

## 5. SOAP (contrôle)

### 5.1 Méthode et URL

- **Méthode :** POST
- **Corps :** XML SOAP, **taille max 2048 octets** (au-delà → 400 Bad Request). Transfer-Encoding chunked accepté ; taille de chunk lue limitée à 2048.
- **Dispatch :** uniquement via l’en-tête **SOAPAction**. Le chemin de la requête n’est pas utilisé pour choisir l’action.

Format SOAPAction : `"urn:...service:...:1#<ActionName>"`. Le serveur prend la partie après `#` et tronque au premier `'` ou `"`.

### 5.2 Actions reconnues

| Action (après #) | Service (implicite) | Comportement |
|------------------|---------------------|--------------|
| Browse | ContentDirectory | Parcourt le répertoire ; voir 5.4 |
| GetSearchCapabilities | ContentDirectory | Retourne SearchCaps |
| GetSortCapabilities | ContentDirectory | Retourne SortCaps |
| GetProtocolInfo | ConnectionManager | Retourne Source/Sink |
| Search | ContentDirectory | **708** Unsupported Action |
| (autre) | — | **401** Invalid Action |

Aucune action spécifique pour X_MS_MediaReceiverRegistrar (SCPD exposé, contrôle non implémenté).

### 5.3 Parsing du corps SOAP (requête)

Le serveur ne parse pas un vrai SOAP complet : il cherche des éléments XML de premier niveau et récupère la **première** valeur texte des balises suivantes :

- **ObjectID** ou **ContainerID** → `remote_dirpath` (répertoire à lister ; `"0"` = racine).
- **StartingIndex** → entier → `starting_index` (0 si absent ou invalide).
- **RequestedCount** → entier → `requested_count` (nombre d’entrées à retourner ; si &lt; 1, traité comme illimité côté logique métier).

Les noms de balises sont reconnus en ASCII (lettres uniquement pour le nom, pas les préfixes de namespace). Pas d’échappement XML exigé côté client au-delà du standard (le serveur décode `&amp;`, `&lt;`, etc. pour les valeurs utilisées).

### 5.4 Réponse Browse (ContentDirectory)

- **Enveloppe SOAP 1.1** avec `<s:Body>` contenant :
  - **Result** : un seul document DIDL-Lite **échappé en XML** (contenu placé dans une seule chaîne, les `<` et `>` échappés en `&lt;` / `&gt;`, etc.).
  - **NumberReturned** : nombre d’entrées dans ce fragment.
  - **TotalMatches** : nombre total d’entrées dans le conteneur.
  - **UpdateID** : 0.

**DIDL-Lite (conceptuel) :**

- **Namespaces utilisés :**  
  `xmlns:dc="http://purl.org/dc/elements/1.1/"`  
  `xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/"`  
  `xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/"`

- **Conteneur (dossier) :**  
  `<container id="..." parentID="..." restricted="1" searchable="0">`  
  `<dc:title>...</dc:title>`  
  `<upnp:class>object.container.storageFolder</upnp:class>`  
  `<upnp:storageUsed>-1</upnp:storageUsed>`  
  `</container>`

- **Item (fichier) :**  
  `<item id="..." parentID="..." restricted="1">`  
  `<dc:title>...</dc:title>`  
  `<upnp:class>object.item.<audio|image|video>Item</upnp:class>`  
  `<res size="..." protocolInfo="http-get:*:<mime>:DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000">http://<host>:<port>/MediaItems/<path_échappé_URL></res>`  
  `</item>`

L’`id` est le chemin relatif (ex. `/dossier/fichier.mp3` ou vide pour la racine). Le **protocolInfo** et l’URL dans `<res>` sont générés à partir du type MIME et de l’adresse du serveur.

### 5.5 Réponses GetSearchCapabilities / GetSortCapabilities

- **GetSearchCapabilitiesResponse :**  
  `<SearchCaps>@id, @parentID, @refID </SearchCaps>`
- **GetSortCapabilitiesResponse :**  
  `<SortCaps>dc:title,</SortCaps>`

### 5.6 GetProtocolInfo (ConnectionManager)

- **Source :** liste séparée par des virgules de profils `http-get:*:<type>/<sous-type>:*`, dérivée de la liste interne `supported_mime_types` (audio, image, text, video, etc.).
- **Sink :** chaîne vide `<Sink></Sink>`.

### 5.7 Erreurs SOAP (Fault)

- **Code HTTP :** égal au code d’erreur UPnP (401, 402, 708, etc.).
- **Corps :** SOAP 1.1 Fault :
  - `faultcode`: `s:Client`
  - `faultstring`: `UPnPError`
  - **detail** :  
    `<UPnPError xmlns="urn:schemas-upnp-org:control-1-0">`  
    `<errorCode><code></errorCode>`  
    `<errorDescription><description></errorDescription>`  
    `</UPnPError>`

Exemples : 401 Invalid Action, 402 Invalid Args (ex. RemoteDirpath), 708 Unsupported Action (Search).

---

## 6. GENA (événements)

### 6.1 SUBSCRIBE

- **Méthode :** SUBSCRIBE
- **Chemin :** `/evt/ContentDir`, `/evt/ConnectionMgr` ou `/evt/X_MS_MediaReceiverRegistrar`
- **En-têtes requis :**
  - **Callback :** `<http://host:port/path>` (URL du client pour recevoir les NOTIFY).
  - **NT :** `upnp:event`
  - **Timeout :** optionnel, format `Second-<n>` ; sinon renouvellement imposé à 300 s.

Pour une **nouvelle** souscription, ne pas envoyer **SID**.  
Réponse réussie : **200 OK** avec :

- **Timeout:** `Second-<n>` (n utilisé pour l’expiration ; si 0, 300).
- **SID :** identifiant de souscription (UUID généré à partir de l’UUID device + suffixe).

Le serveur envoie immédiatement un premier NOTIFY vers l’URL Callback.

### 6.2 RENEW (SUBSCRIBE avec SID)

- Même méthode SUBSCRIBE, même chemin, avec **SID** au lieu de Callback/NT.  
Timeout renouvelé ; pas de nouveau NOTIFY immédiat obligatoire (comportement actuel).

### 6.3 UNSUBSCRIBE

- **Méthode :** UNSUBSCRIBE
- **En-tête :** **SID:** `<subscription-uuid>`
- Réponse : **200 OK** ou **412 Precondition Failed** (SID inconnu).

### 6.4 NOTIFY (serveur → client)

Le serveur ouvre une connexion TCP vers l’URL Callback (host:port), envoie :

```http
NOTIFY <path> HTTP/1.1\r\n
Host: <host><:port>\r\n
Content-Type: text/xml; charset="utf-8"\r\n
Transfer-Encoding: chunked\r\n
NT: upnp:event\r\n
NTS: upnp:propchange\r\n
SID: <subscription-uuid>\r\n
SEQ: <sequence>\r\n
Connection: close\r\n
Cache-Control: no-cache\r\n
\r\n
<body chunked>
```

- **path :** partie chemin de l’URL Callback (ou `/` si vide).
- **SEQ :** entier incrémenté à chaque envoi pour cette souscription.

**Corps (ContentDirectory) :**  
Document XML de type **e:propertyset** (namespace `urn:schemas-upnp-org:event-1-0`), avec namespace service `urn:schemas-upnp-org:service:ContentDirectory:1` :

- **e:property** → **TransferIDs** (vide)
- **e:property** → **SystemUpdateID** = 0

**Corps (ConnectionManager) :**  
Même principe, namespace `urn:schemas-upnp-org:service:ConnectionManager:1` :

- **e:property** → **SourceProtocolInfo** (même liste que GetProtocolInfo, chaîne comma-separated)
- **e:property** → **SinkProtocolInfo** (vide)
- **e:property** → **CurrentConnectionIDs** = 0

**X_MS_MediaReceiverRegistrar :** souscription acceptée mais aucun corps d’événement envoyé (pas de branche dans le code).

---

## 7. Streaming (GET /MediaItems/...)

### 7.1 Méthodes et chemin

- **GET** ou **HEAD**
- Chemin : `/MediaItems/<chemin_relatif>` (le chemin relatif est normalisé et restreint au répertoire média ; pas de `..`).

### 7.2 En-têtes de requête (DLNA/HTTP)

| En-tête | Rôle |
|---------|------|
| **Range** | Optionnel. `bytes=start-end` ou `bytes=start-`. Si seul start → fin = fin du fichier. Invalide ou hors limites → 400 ou 416. |
| **transferMode.dlna.org** | `Streaming` \| `Interactive` \| `Background`. Règles : pas de Streaming pour une image ; pas d’Interactive pour non-image sans realTimeInfo ; pas d’Interactive pour une image (le serveur force Interactive pour les images). |
| **realTimeInfo.dlna.org** | Présent → FLAG ; si utilisé avec Interactive sur non-image → 400. |
| **TimeSeekRange.dlna.org** / **PlaySpeed.dlna.org** | Exigent **Range** ; sinon 406. |
| **getcontentFeatures.dlna.org** | Doit être `1` si présent ; autre valeur → 400. |
| **getAvailableSeekRange.dlna.org** | Doit être `1` si présent ; autre valeur → 400. |
| **getCaptionInfo.sec** | Demande d’info sous-titres → si vidéo et fichier .srt homonyme existe, en-tête **CaptionInfo.sec** dans la réponse. |

### 7.3 Réponse (200 ou 206)

- **realTimeInfo.dlna.org:** `DLNA.ORG_TLAG=*`
- **transferMode.dlna.org:** `Streaming` | `Interactive` | `Background` (selon type MIME et requête)
- **Content-Type:** `<type>/<sous-type>` (déduit de l’extension)
- **Content-Length** (et **Content-Range** en 206)
- **Accept-Ranges:** bytes
- **contentFeatures.dlna.org:**  
  `DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=<32 bits hex>000000000000000000000000`

**DLNA.ORG_FLAGS (exemples) :**  
- Toujours : `0x00100000` (DLNA v1.5), `0x00200000` (connection stalling), `0x00400000` (Background).  
- Image : en plus `0x00800000` (Interactive).  
- Audio/Vidéo : en plus `0x01000000` (Streaming).  
Ex. image : `01700000` puis 24 zéros ; audio/vidéo : `01F00000` puis 24 zéros.

**CaptionInfo.sec (optionnel) :**  
Si sous-titre demandé et .srt trouvé :  
`CaptionInfo.sec: http://<host>:<port>/MediaItems/<chemin_échappé>.srt`

Corps : octets du fichier (ou plage). Pour HEAD, pas de corps. La connexion est fermée après envoi.

---

## 8. Icônes

- **GET** `/icons/sm.png`, `lrg.png`, `sm.jpg`, `lrg.jpg`
- **Content-Type:** image/png ou image/jpeg
- **transferMode.dlna.org:** Interactive
- Réponse 200 avec corps binaire (icônes embarquées). Autre nom sous `/icons/` → 404.

---

## 9. Résumé des codes HTTP utilisés

| Code | Usage |
|------|--------|
| 200 | OK (descriptions, SOAP OK, GENA SUBSCRIBE/UNSUBSCRIBE, média full, icônes) |
| 206 | Partial Content (Range sur média) |
| 400 | Bad Request (Host invalide, body SOAP > 2048, getcontentFeatures/getAvailableSeekRange ≠ 1, Range invalide, realTimeInfo+Interactive incohérent, etc.) |
| 401 | Invalid Action (SOAP) |
| 402 | Invalid Args (SOAP) |
| 403 | Forbidden (fichier non régulier) |
| 404 | Not Found (chemin inconnu, fichier absent) |
| 406 | Not Acceptable (TimeSeek/PlaySpeed sans Range ; transferMode vs type MIME) |
| 412 | Precondition Failed (GENA UNSUBSCRIBE, SID inconnu) |
| 416 | Range Not Satisfiable |
| 500 | Internal Server Error |
| 501 | Not Implemented |
| 503 | Service Unavailable (ex. media_dir inaccessible) |
| 708 | Unsupported Action (SOAP, ex. Search) |

---

## 10. Ordre typique des échanges (réimplémentation)

1. **Découverte**
   - Client envoie M-SEARCH (ST au moins un des types ou ssdp:all), MAN `"ssdp:discover"`, MX présent.
   - Serveur répond en 200 avec LOCATION vers `/rootDesc.xml`.
   - Optionnel : client écoute les NOTIFY ssdp:alive sur 239.255.255.250:1900.

2. **Description**
   - GET `http://<host>:<port>/rootDesc.xml` (Host = IP:port exact).
   - Puis GET des SCPD : `/ContentDir.xml`, `/ConnectionMgr.xml`, `/X_MS_MediaReceiverRegistrar.xml` si besoin.

3. **Contrôle**
   - POST vers une controlURL (ex. `/ctl/ContentDir`) avec SOAPAction `...:#Browse`, corps SOAP avec ObjectID/ContainerID, BrowseFlag, StartingIndex, RequestedCount, etc.
   - Réponse SOAP avec Result (DIDL-Lite), NumberReturned, TotalMatches, UpdateID.
   - GetProtocolInfo sur `/ctl/ConnectionMgr` pour la liste des MIME supportés.

4. **Streaming**
   - GET `http://<host>:<port>/MediaItems/<path>` avec Host correct, optionnellement Range et en-têtes DLNA (transferMode, getcontentFeatures.dlna.org: 1, etc.).
   - Réponse 200/206 avec en-têtes DLNA et corps binaire.

5. **Événements (optionnel)**
   - SUBSCRIBE vers eventSubURL avec Callback et NT: upnp:event.
   - Réception des NOTIFY sur l’URL Callback (serveur ouvre TCP vers le client).
   - UNSUBSCRIBE avec SID pour résilier.

Cette documentation couvre les formats et règles nécessaires pour réimplémenter un client ou un serveur compatible avec MicroDLNA.

---

## Annexe A — Exemples concrets (reçu / envoyé)

Les exemples ci‑dessous utilisent des valeurs types : IP `192.168.1.10`, port HTTP `2800`, UUID `uuid:4d696e69-444c-164e-9d41-554e4b4e4f57`, répertoire média avec un dossier `Musique` et un fichier `titre.mp3`. Les retours à la ligne sont indiqués par `\r\n`.

---

### A.1 SSDP

**Reçu (client → serveur) — M-SEARCH :**

```
M-SEARCH * HTTP/1.1\r\n
HOST: 239.255.255.250:1900\r\n
MAN: "ssdp:discover"\r\n
MX: 3\r\n
ST: urn:schemas-upnp-org:device:MediaServer:1\r\n
\r\n
```

**Reçu — M-SEARCH avec ssdp:all :**

```
M-SEARCH * HTTP/1.1\r\n
HOST: 239.255.255.250:1900\r\n
MAN: "ssdp:discover"\r\n
MX: 5\r\n
ST: ssdp:all\r\n
\r\n
```

**Envoyé (serveur → client) — Réponse M-SEARCH (une des six, ici ContentDirectory) :**

```
HTTP/1.1 200 OK\r\n
CACHE-CONTROL: max-age=1800\r\n
DATE: Thu, 19 Feb 2025 14:30:00 GMT\r\n
ST: urn:schemas-upnp-org:service:ContentDirectory:1\r\n
USN: uuid:4d696e69-444c-164e-9d41-554e4b4e4f57::urn:schemas-upnp-org:service:ContentDirectory:1\r\n
EXT:\r\n
SERVER: MicroDLNA/1.0\r\n
LOCATION: http://192.168.1.10:2800/rootDesc.xml\r\n
Content-Length: 0\r\n
\r\n
```

**Envoyé — NOTIFY ssdp:alive (exemple pour ContentDirectory) :**

```
NOTIFY * HTTP/1.1\r\n
HOST:239.255.255.250:1900\r\n
CACHE-CONTROL:max-age=1800\r\n
LOCATION:http://192.168.1.10:2800/rootDesc.xml\r\n
SERVER: MicroDLNA/1.0\r\n
NT:urn:schemas-upnp-org:service:ContentDirectory:1\r\n
USN:uuid:4d696e69-444c-164e-9d41-554e4b4e4f57::urn:schemas-upnp-org:service:ContentDirectory:1\r\n
NTS:ssdp:alive\r\n
\r\n
```

**Envoyé — NOTIFY ssdp:byebye :**

```
NOTIFY * HTTP/1.1\r\n
HOST: 239.255.255.250:1900\r\n
NT: urn:schemas-upnp-org:service:ContentDirectory:1\r\n
USN: uuid:4d696e69-444c-164e-9d41-554e4b4e4f57::urn:schemas-upnp-org:service:ContentDirectory:1\r\n
NTS: ssdp:byebye\r\n
\r\n
```

---

### A.2 HTTP — Description racine

**Reçu — GET /rootDesc.xml :**

```
GET /rootDesc.xml HTTP/1.1\r\n
Host: 192.168.1.10:2800\r\n
\r\n
```

**Envoyé — Réponse 200 (extrait du corps XML) :**

```
HTTP/1.1 200 OK\r\n
Content-Type: text/xml; charset=utf-8\r\n
Connection: close\r\n
Transfer-Encoding: chunked\r\n
Server: MicroDLNA/1.0\r\n
Date: Thu, 19 Feb 2025 14:30:00 GMT\r\n
EXT:\r\n
\r\n
<corps chunké, ex. :>
<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0">
  <specVersion><major>1</major><minor>0</minor></specVersion>
  <device>
    <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
    <friendlyName>MicroDLNA</friendlyName>
    <manufacturer>Michael J. Walsh</manufacturer>
    <manufacturerURL>https://github.com/mjfwalsh/microdlna</manufacturerURL>
    <modelDescription>MicroDLNA</modelDescription>
    <modelName>MicroDLNA Media Server</modelName>
    <serialNumber>00000000</serialNumber>
    <UDN>uuid:4d696e69-444c-164e-9d41-554e4b4e4f57</UDN>
    <presentationURL>/</presentationURL>
    <iconList>...</iconList>
    <serviceList>
      <service>
        <serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType>
        <serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId>
        <controlURL>/ctl/ContentDir</controlURL>
        <eventSubURL>/evt/ContentDir</eventSubURL>
        <SCPDURL>/ContentDir.xml</SCPDURL>
      </service>
      ...
    </serviceList>
  </device>
</root>
```

---

### A.3 SOAP — Browse

**Reçu — POST Browse (racine, 50 entrées) :**

```
POST /ctl/ContentDir HTTP/1.1\r\n
Host: 192.168.1.10:2800\r\n
Content-Type: text/xml; charset="utf-8"\r\n
SOAPAction: "urn:schemas-upnp-org:service:ContentDirectory:1#Browse"\r\n
Content-Length: 285\r\n
\r\n
<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:Browse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1">
      <ObjectID>0</ObjectID>
      <BrowseFlag>BrowseDirectChildren</BrowseFlag>
      <Filter>*</Filter>
      <StartingIndex>0</StartingIndex>
      <RequestedCount>50</RequestedCount>
      <SortCriteria></SortCriteria>
    </u:Browse>
  </s:Body>
</s:Envelope>
```

**Envoyé — Réponse Browse (un dossier + un fichier) :**

```
HTTP/1.1 200 OK\r\n
Content-Type: text/xml; charset=utf-8\r\n
Connection: close\r\n
Transfer-Encoding: chunked\r\n
Server: MicroDLNA/1.0\r\n
Date: Thu, 19 Feb 2025 14:30:00 GMT\r\n
EXT:\r\n
\r\n
<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body>
<u:BrowseResponse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1">
<Result>&lt;DIDL-Lite xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/" xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/"&gt;
&lt;container id="Musique" parentID="" restricted="1" searchable="0"&gt;&lt;dc:title&gt;Musique&lt;/dc:title&gt;&lt;upnp:class&gt;object.container.storageFolder&lt;/upnp:class&gt;&lt;upnp:storageUsed&gt; -1 &lt;/upnp:storageUsed&gt;&lt;/container&gt;
&lt;item id="Musique/titre.mp3" parentID="Musique" restricted="1"&gt;&lt;dc:title&gt;titre.mp3&lt;/dc:title&gt;&lt;upnp:class&gt;object.item.audioItem&lt;/upnp:class&gt;&lt;res size="1234567" protocolInfo="http-get:*:audio/mpeg:DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000"&gt;http://192.168.1.10:2800/MediaItems/Musique/titre.mp3&lt;/res&gt;&lt;/item&gt;
&lt;/DIDL-Lite&gt;</Result>
<NumberReturned>2</NumberReturned>
<TotalMatches>2</TotalMatches>
<UpdateID>0</UpdateID>
</u:BrowseResponse></s:Body></s:Envelope>
```

---

### A.4 SOAP — GetProtocolInfo, GetSearchCapabilities, GetSortCapabilities

**Reçu — GetProtocolInfo :**

```
POST /ctl/ConnectionMgr HTTP/1.1\r\n
Host: 192.168.1.10:2800\r\n
Content-Type: text/xml; charset="utf-8"\r\n
SOAPAction: "urn:schemas-upnp-org:service:ConnectionManager:1#GetProtocolInfo"\r\n
Content-Length: 2\r\n
\r\n

```

**Envoyé — Réponse GetProtocolInfo (extrait de la liste Source) :**

```
HTTP/1.1 200 OK\r\n
...
<u:GetProtocolInfoResponse xmlns:u="urn:schemas-upnp-org:service:ConnectionManager:1">
  <Source>http-get:*:audio:adpcm:*,http-get:*:audio:basic:*,http-get:*:audio:mpeg:*,http-get:*:audio:ogg:*,http-get:*:audio:x-flac:*,http-get:*:image:jpeg:*,http-get:*:image:png:*,http-get:*:video:mpeg:*,...</Source>
  <Sink></Sink>
</u:GetProtocolInfoResponse>
```

**Reçu — GetSearchCapabilities :**

```
POST /ctl/ContentDir HTTP/1.1\r\n
Host: 192.168.1.10:2800\r\n
SOAPAction: "urn:schemas-upnp-org:service:ContentDirectory:1#GetSearchCapabilities"\r\n
Content-Length: 0\r\n
\r\n
```

**Envoyé — Réponse GetSearchCapabilities :**

```
...<SearchCaps>@id, @parentID, @refID </SearchCaps>...
```

**Envoyé — Réponse GetSortCapabilities :**

```
...<SortCaps>dc:title,</SortCaps>...
```

---

### A.5 SOAP — Erreur (401 Invalid Action)

**Reçu — POST avec action inconnue :**

```
POST /ctl/ContentDir HTTP/1.1\r\n
Host: 192.168.1.10:2800\r\n
SOAPAction: "urn:schemas-upnp-org:service:ContentDirectory:1#CreateObject"\r\n
Content-Length: 0\r\n
\r\n
```

**Envoyé — 401 + Fault :**

```
HTTP/1.1 401 Invalid Action\r\n
Content-Type: text/xml; charset=utf-8\r\n
...
\r\n
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" ...>
  <s:Body>
    <s:Fault>
      <faultcode>s:Client</faultcode>
      <faultstring>UPnPError</faultstring>
      <detail>
        <UPnPError xmlns="urn:schemas-upnp-org:control-1-0">
          <errorCode>401</errorCode>
          <errorDescription>Invalid Action</errorDescription>
        </UPnPError>
      </detail>
    </s:Fault>
  </s:Body>
</s:Envelope>
```

**Envoyé — 708 Unsupported Action (ex. Search) :** même structure avec `errorCode` 708 et `errorDescription` « Unsupported Action ».

---

### A.6 GENA — SUBSCRIBE / UNSUBSCRIBE

**Reçu — SUBSCRIBE :**

```
SUBSCRIBE /evt/ContentDir HTTP/1.1\r\n
Host: 192.168.1.10:2800\r\n
Callback: <http://192.168.1.100:49152/upnp/event>\r\n
NT: upnp:event\r\n
Timeout: Second-300\r\n
\r\n
```

**Envoyé — Réponse 200 SUBSCRIBE :**

```
HTTP/1.1 200 OK\r\n
Content-Type: text/xml; charset=utf-8\r\n
Connection: close\r\n
Transfer-Encoding: chunked\r\n
Server: MicroDLNA/1.0\r\n
Timeout: Second-300\r\n
SID: uuid:4d696e69-444c-164e-9d41-554e4b4e4f57-0001\r\n
Date: Thu, 19 Feb 2025 14:30:00 GMT\r\n
EXT:\r\n
\r\n
```

**Reçu — UNSUBSCRIBE :**

```
UNSUBSCRIBE /evt/ContentDir HTTP/1.1\r\n
Host: 192.168.1.10:2800\r\n
SID: uuid:4d696e69-444c-164e-9d41-554e4b4e4f57-0001\r\n
\r\n
```

**Envoyé — Réponse 200 UNSUBSCRIBE :** `HTTP/1.1 200 OK\r\n` + en-têtes habituels, corps optionnel.

---

### A.7 GENA — NOTIFY (serveur → client)

Le serveur ouvre une connexion TCP vers `192.168.1.100:49152` et envoie :

**ContentDirectory :**

```
NOTIFY /upnp/event HTTP/1.1\r\n
Host: 192.168.1.100:49152\r\n
Content-Type: text/xml; charset="utf-8"\r\n
Transfer-Encoding: chunked\r\n
NT: upnp:event\r\n
NTS: upnp:propchange\r\n
SID: uuid:4d696e69-444c-164e-9d41-554e4b4e4f57-0001\r\n
SEQ: 0\r\n
Connection: close\r\n
Cache-Control: no-cache\r\n
\r\n
<e:propertyset xmlns:e="urn:schemas-upnp-org:event-1-0" xmlns:s="urn:schemas-upnp-org:service:ContentDirectory:1"><e:property><TransferIDs></TransferIDs></e:property><e:property><SystemUpdateID>0</SystemUpdateID></e:property></e:propertyset>
```

**ConnectionManager (extrait du corps) :**

```
<e:propertyset xmlns:e="urn:schemas-upnp-org:event-1-0" xmlns:s="urn:schemas-upnp-org:service:ConnectionManager:1">
  <e:property><SourceProtocolInfo>http-get:*:audio:adpcm:*,http-get:*:audio:mpeg:*,...</SourceProtocolInfo></e:property>
  <e:property><SinkProtocolInfo></SinkProtocolInfo></e:property>
  <e:property><CurrentConnectionIDs>0</CurrentConnectionIDs></e:property>
</e:propertyset>
```

---

### A.8 Streaming — GET média

**Reçu — GET fichier audio (sans Range) :**

```
GET /MediaItems/Musique/titre.mp3 HTTP/1.1\r\n
Host: 192.168.1.10:2800\r\n
getcontentFeatures.dlna.org: 1\r\n
transferMode.dlna.org: Streaming\r\n
\r\n
```

**Reçu — GET avec Range et sous-titre (vidéo) :**

```
GET /MediaItems/Films/film.mkv HTTP/1.1\r\n
Host: 192.168.1.10:2800\r\n
Range: bytes=0-1048575\r\n
getcontentFeatures.dlna.org: 1\r\n
getCaptionInfo.sec: 1\r\n
transferMode.dlna.org: Streaming\r\n
\r\n
```

**Envoyé — Réponse 200 (audio complet) :**

```
HTTP/1.1 200 OK\r\n
Connection: close\r\n
Date: Thu, 19 Feb 2025 14:30:00 GMT\r\n
Server: MicroDLNA/1.0\r\n
EXT:\r\n
realTimeInfo.dlna.org: DLNA.ORG_TLAG=*\r\n
transferMode.dlna.org: Streaming\r\n
Content-Type: audio/mpeg\r\n
Content-Length: 1234567\r\n
Accept-Ranges: bytes\r\n
contentFeatures.dlna.org: DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01F00000000000000000000000000000\r\n
\r\n
<octets du fichier>
```

**Envoyé — Réponse 206 + sous-titre (vidéo) :**

```
HTTP/1.1 206 Partial Content\r\n
...
transferMode.dlna.org: Streaming\r\n
Content-Type: video/x-matroska\r\n
Content-Length: 1048576\r\n
Content-Range: bytes 0-1048575/50000000\r\n
CaptionInfo.sec: http://192.168.1.10:2800/MediaItems/Films/film.srt\r\n
Accept-Ranges: bytes\r\n
contentFeatures.dlna.org: DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01F00000000000000000000000000000\r\n
\r\n
<octets de la plage>
```

---

### A.9 Icônes

**Reçu :**

```
GET /icons/sm.png HTTP/1.1\r\n
Host: 192.168.1.10:2800\r\n
\r\n
```

**Envoyé :**

```
HTTP/1.1 200 OK\r\n
Connection: close\r\n
Date: ...\r\n
Server: MicroDLNA/1.0\r\n
EXT:\r\n
realTimeInfo.dlna.org: DLNA.ORG_TLAG=*\r\n
transferMode.dlna.org: Interactive\r\n
Content-Type: image/png\r\n
Content-Length: <taille>\r\n
\r\n
<données binaires PNG>
```

---

### A.10 Erreurs HTTP (exemples)

**Reçu avec Host invalide :**

```
GET /rootDesc.xml HTTP/1.1\r\n
Host: autre.domaine.local:2800\r\n
\r\n
```

**Envoyé — 400 Bad Request :**

```
HTTP/1.1 400 Bad Request\r\n
Content-Type: text/html; charset=utf-8\r\n
...
<body><h1>Bad Request</h1></body>
```

**Reçu — chemin inconnu :**

```
GET /autre/chemin HTTP/1.1\r\n
Host: 192.168.1.10:2800\r\n
\r\n
```

**Envoyé — 404 Not Found :** `HTTP/1.1 404 Not Found` + page HTML.
