# Rapport — Failles de sécurité et bugs (MicroDLNA)

**Date :** 2025  
**Périmètre :** Analyse statique du code source

Pour chaque problème : **Correctif (appliqué)** décrit le correctif en place ; **TODO** indique qu’il reste à faire ; **Aucun correctif nécessaire** précise que le comportement est accepté.

---

## 1. Failles de sécurité

### 1.1 Débordement de buffer dans le parseur SOAP/XML (`xmlregex.c`) — CRITIQUE

**Fichier :** `xmlregex.c`  
**Fonction :** `process_post_content`

- **`ele_name[20]`** : la boucle accepte jusqu’à 20 caractères (`while (i < 20 && ...)` avec `ele_name[i++] = c`). En sortie, `i` peut valoir 20, puis `ele_name[i] = '\0'` écrit en `ele_name[20]`, soit **hors du tableau** (indices valides 0..19).  
  → **Impact :** corruption mémoire, possible exécution de code à distance si exploitable.

- **`ele_value[BUF_SIZE]`** (BUF_SIZE = 1024) : la boucle lit jusqu’à 1024 caractères (`while (i < BUF_SIZE && ...)` avec `ele_value[i++] = c`). Si exactement 1024 caractères sont lus, on sort avec `i == BUF_SIZE` sans jamais placer de `'\0'`. La valeur n’est pas passée à `process_name_value_pair` dans ce tour (à cause du `break`), mais les boucles suivantes réutilisent `ele_value` sans garantie de chaîne terminée. De plus, si la logique changeait ou en cas de réutilisation, **`ele_value[BUF_SIZE]`** serait le seul emplacement possible pour le `'\0'`, ce qui est hors limites.  
  → **Impact :** lecture hors buffer (strlen/strcmp) ou débordement si on ajoute un null ; risque d’abus via requêtes SOAP malformées.

**Correctif (appliqué) :**  
- `ele_name` : buffer porté à `ELE_NAME_SIZE + 1` (21 octets), boucle limitée à `i < ELE_NAME_SIZE` ; le `'\0'` est écrit dans les limites.  
- `ele_value` : buffer `ELE_VALUE_SIZE` = `BUF_SIZE + 1` (1025 octets), lecture limitée à `ELE_VALUE_SIZE - 1` caractères ; `'\0'` posé systématiquement après la lecture ; trim réécrit pour ne pas dépasser le buffer.

---

### 1.2 Contournement possible du contrôle d’en-tête Host (`upnphttp.c`)

**Fichier :** `upnphttp.c`  
**Contexte :** Vérification du header `Host` pour limiter les attaques par rebinding DNS.

```c
char expected_host[30];
// ...
if (strncmp(expected_host, value, 30) == 0)
    h->reqflags |= FLAG_HOST;
```

La comparaison utilise **uniquement** les 30 premiers caractères. Un client peut envoyer un Host plus long dont le préfixe est identique à l’hôte attendu (ex. `192.168.1.1:2800.evil.com`).  
→ **Impact :** affaiblissement de la protection anti–DNS rebinding ; selon le réseau, possibilité d’usurpation ou d’abus.

**Correctif (appliqué) :**  
- Construction de `expected_host` avec `expected_len` (longueur réelle).  
- Acceptation du Host seulement si `(size_t)len >= expected_len`, les `expected_len` premiers caractères égaux à `expected_host`, et le caractère suivant (s’il existe) n’est pas un caractère d’hôte valide : il doit être `\0`, `\r`, `\n`, espace ou tab. Ainsi un Host du type `192.168.1.1:2800.evil.com` est rejeté.

---

### 1.3 Traversal via liens symboliques sous `media_dir`

**Fichiers :** `utils.c` (`sanitise_path`), `dirlist.c`, `upnphttp.c` (servir un fichier)

`sanitise_path` normalise uniquement les séquences `..` et `.` dans le chemin ; il **ne résout pas les liens symboliques**. Si un fichier ou répertoire sous `media_dir` est un symlink pointant en dehors (ex. `media_dir/private` → `/etc`), un ObjectID ou une URL du type `private` ou `private/passwd` peut permettre de lister ou de servir des fichiers hors de `media_dir`.  
→ **Impact :** fuite de contenu (fichiers sensibles) si l’administrateur a créé des symlinks sous `media_dir`.

**TODO.**  
Recommandation : interdire de suivre les symlinks (`openat(..., O_NOFOLLOW)`, `fstatat(..., AT_SYMLINK_NOFOLLOW)`) ou résoudre avec `realpath()` et vérifier que le résultat reste sous `media_dir`.

---

### 1.4 Vérification Host absente pour certaines méthodes

Les requêtes **GET** (descriptions XML, icônes, médias) et **POST** (SOAP) exigent un header Host valide. Les méthodes **SUBSCRIBE** et **UNSUBSCRIBE** (GENA) passent par la même vérification. Aucun correctif à prévoir ; le flux est cohérent.

---

## 2. Bugs (logique, robustesse, types)

### 2.1 `dirlist.c` — Comportement quand `*file_count == 0`

**Fichier :** `dirlist.c`

Après la boucle `readdir`, on fait :

```c
safe_realloc((void **)&entries, *file_count * sizeof(content_entry *));
```

Quand `*file_count == 0`, cela appelle `realloc(entries, 0)`, ce qui peut **libérer** le bloc et mettre `entries` à `NULL`. Ensuite :

- `qsort(entries, 0, ...)` est valide (aucune opération).  
- Les boucles `for` ne s’exécutent pas.  
- Puis `safe_realloc((void **)&entries, h->requested_count * sizeof(content_entry *));` avec `requested_count == 0` rappelle `realloc(NULL, 0)`.

Le vrai risque est côté **appelant** : si le code qui utilise `get_directory_listing` suppose que le pointeur retourné est toujours non NULL lorsque `*file_count == 0`, il peut déréférencer `NULL`. À vérifier dans `upnpsoap.c` (gestion de la réponse Browse quand il n’y a aucun fichier).

**Correctif (appliqué) :**  
- Si `*file_count == 0` après `readdir` : on ne fait plus `realloc(..., 0)` ; on fait `safe_realloc(..., sizeof(content_entry *))`, on pose `h->requested_count = 0` et on retourne `entries` (non NULL). L’appelant reçoit un tableau « vide » valide et n’itère pas.

---

### 2.2 `dirlist.c` — Débordement / overflow sur `requested_count` et indices

**Fichier :** `dirlist.c`

- `h->requested_count` et `h->starting_index` viennent du SOAP (entiers). On fait :  
  `h->requested_count = *file_count - h->starting_index;`  
  Si `*file_count` et `h->starting_index` sont de type non signé et que le résultat est stocké dans un **int** (`requested_count`), un dépassement de capacité peut se produire (ex. `file_count` très grand, `starting_index` 0 → valeur négative ou tronquée).

- La boucle  
  `for (int i = 0; i < h->requested_count; i++)`  
  et l’accès `entries[i + h->starting_index]` supposent que `starting_index + requested_count <= file_count`. Si `requested_count` est négatif (overflow), le comportement est indéfini.

**Correctif (appliqué) :**  
- Recalcul de `requested_count` : si `requested_count == -1` ou si `starting_index + requested_count > *file_count`, on pose `requested_count = min(*file_count - starting_index, INT_MAX)` (avec `#include <limits.h>`).  
- Les boucles et `safe_realloc` utilisent des casts `(unsigned)h->requested_count` où nécessaire pour éviter des indices ou tailles incohérents.

---

### 2.3 `sendfile.c` / `sendfile.h` — Type de `end_offset` (off_t vs size_t)

**Fichiers :** `sendfile.c`, `sendfile.h`

La signature est :

```c
void send_file(int socketfd, int sendfd, off_t offset, size_t end_offset);
```

L’appel depuis `upnphttp.c` passe `h->req_range_end` (type **off_t**). Sur des plateformes 32 bits avec `off_t` 64 bits et `size_t` 32 bits, une valeur de `req_range_end` supérieure à `SIZE_MAX` peut être **tronquée** en `size_t`.  
→ **Impact :** transfert partiel ou comportement incorrect pour des fichiers très grands.

**Correctif (appliqué) :**  
- Signature modifiée en `void send_file(..., off_t offset, off_t end_offset)`.  
- Dans `send_file`, `send_size` est en `off_t` ; à l’appel système Linux `sendfile(..., (size_t)send_size)` pour respecter le type du paramètre `count`.

---

### 2.4 `getifaddr.c` — `set_interfaces_from_string` et `strsep`

**Fichier :** `getifaddr.c`

`strsep(&p, ",")` modifie la chaîne en remplaçant la virgule par `'\0'`. Les pointeurs stockés dans `ifaces[]` pointent tous dans le **même** bloc alloué (`ifaces[0]` au départ). `free_ifaces()` ne fait que `free(ifaces[0])` puis met tous les éléments à `NULL`, ce qui est correct (un seul bloc). En revanche, après `strsep`, si l’entrée est par exemple `"eth0,,eth1"`, on peut avoir des tokens vides ; il faudrait les ignorer pour éviter des noms d’interfaces vides.

**Correctif (appliqué) :**  
- Utilisation de la **valeur de retour** de `strsep(&p, ",")` pour obtenir chaque token (au lieu d’utiliser `p` après coup).  
- Nouvelle fonction **`trim_token`** : suppression des espaces en tête et en queue (et null-termination en place).  
- Tokens **vides** (après trim) **ignorés** : `if (*token == '\0') continue`.  
- Un seul bloc alloué pour tous les noms : **`ifaces_alloc`** ; `free_ifaces()` libère `ifaces_alloc` et remet tous les `ifaces[i]` à NULL.

---

### 2.5 `url_escape` (`utils.c`) — Risque d’overflow en calcul de taille

**Fichier :** `utils.c`

```c
int normal = 0;
int to_escape = 0;
// ...
char *escaped_string = safe_malloc(normal + 3 * to_escape + 1);
```

Si la chaîne d’entrée est très longue, `normal + 3 * to_escape + 1` peut dépasser la capacité d’un **int** (overflow).

**Correctif (appliqué) :**  
- Compteurs et taille en `size_t` (`normal`, `to_escape`, `escaped_size`).  
- Avant allocation : test d’overflow `normal > SIZE_MAX - 1 || to_escape > (SIZE_MAX - 1 - normal) / 3` ; en cas de dépassement, `EXIT_ERROR`.  
- Premier passage sur la chaîne en pointeur `const char *s` au lieu d’un indice `int`.  
- `#include <stdint.h>` pour `SIZE_MAX`.

---

### 2.6 `readline` (`upnphttp.c`) — Comportement en cas de ligne trop longue

**Fichier :** `upnphttp.c`

Quand la ligne dépasse `limit` (1024) sans rencontrer `\r\n`, la fonction retourne `-1` et le client reçoit une erreur. Il n’y a pas d’écriture au-delà du buffer, donc pas de débordement.

**Aucun correctif nécessaire.** Comportement voulu ; la limite de 1024 octets par ligne est acceptable.

---

## 3. Synthèse et priorisation

| Priorité | Type        | Fichier(s)   | Problème | Statut |
|----------|-------------|-------------|----------|--------|
| Critique | Sécurité    | xmlregex.c  | Débordement buffer `ele_name` et `ele_value` | Corrigé |
| Haute    | Sécurité    | utils.c / accès fichiers | Traversal via symlinks sous `media_dir` | TODO |
| Haute    | Sécurité    | upnphttp.c  | Contrôle Host insuffisant (strncmp 30) | Corrigé |
| Moyenne  | Bug         | dirlist.c   | Retour NULL quand file_count==0 ; overflow requested_count | Corrigé |
| Moyenne  | Bug         | sendfile.*  | Incompatibilité off_t / size_t pour gros fichiers | Corrigé |
| Basse    | Robustesse  | utils.c     | Overflow possible dans `url_escape` | Corrigé |
| Basse    | Comportement| getifaddr.c | Tokens vides dans la liste d’interfaces | Corrigé |

Reste à traiter : **1.3** (symlinks sous `media_dir`).
