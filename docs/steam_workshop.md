# Steam Workshop

Settings > Network Setup > **STEAM WORKSHOP** signs in with Steam and downloads
MCC content into `mcc_maps` on Android, the Switch and the PS5. The Steam client
comes from [Halo Content Evolved](https://github.com/Watanoge/halo-content-evolved)
(AGPL-3.0-or-later, like this port). Here it runs in the host rather than the guest:
`port/android/host/steam_*.c`, built into all three hosts.

## What it does

- **Sign in**, in one of two ways:
  - **With a QR code**, scanned with the Steam mobile app.
  - **With the account name and password**, typed on the game's keyboard. The
    password shows as stars, is RSA-encrypted under Steam's key for the account,
    and is never stored. Steam then asks for approval in the Steam app (which
    works with the app on the same phone) or for a Steam Guard code.

  This is first on Android, because a phone can't scan its own screen. Handing
  the QR code's link (`https://s.team/q/...`) to the Steam app on the phone
  doesn't work instead: the app (3.10.11) claims those links but only opens its
  camera, to scan the code, and approves nothing from a link.

  The account must own Halo: The Master Chief Collection and Halo: Combat
  Evolved Anniversary, because Steam only gives the depots' keys to owners.
- **MCC FILE STATUS:**
  - Downloads the three shared resource maps from Halo: CE Anniversary's depot
    (976733) into `mcc_maps/_shared/singleplayer` and `mcc_maps/_shared/multiplayer`.
  - Lists the Workshop packages in `mcc_maps`, and updates or removes them.
- **BROWSE WORKSHOP:** the Halo 1 Workshop, 20 items at a time. You can search,
  sort and filter by type, and each item shows its preview picture.
  - Installing an item builds it in `mcc_staging/steam_<id>`, then moves it into
    `mcc_maps/<id>/` whole, replacing an older copy.
  - The package has the same layout the map lists' IMPORT writes
    (`port/android/host/host_mcc_import.c`).
  - `steam/downloads.txt` records which version each item came from, which is
    how an update is told.
- **MANAGE ACCOUNT > LOG OUT:** revokes the sign-in with Steam
  (`Authentication.RevokeToken`), then forgets it on the device. If Steam can't
  be reached, the sign-in is forgotten on the device only. The screen then says
  that Steam's own settings can deauthorize it.

## How it reaches Steam

- **Connection:** a TCP session with one of Steam's connection servers, found
  through `ISteamDirectory` over the host's verified HTTPS.
  - The channel key is sent encrypted under Steam's public universe RSA key,
    then every message is AES-encrypted with HMAC-derived IVs, as SteamKit does.
  - A server lookup that was tampered with can't read or change the session.
- **Downloads:** depot chunks come from the content servers over plain HTTP on
  kept-alive connections, as Steam's own client fetches them.
  - Each chunk is encrypted with the depot key and checked against its SHA-1
    after unpacking (LZMA, Zstandard or zip: `steam_unpack.c`).

## The sign-in on the device

A refresh token is as good as the account to whoever holds it until it's
revoked, so the session file (`steam_seal.h`) is protected as far as each
device allows:

| | Where | Sealed with |
| --- | --- | --- |
| Android | The app's private storage, not the data folder | An AES-GCM key in the Android Keystore that never leaves it (`HaloActivity.steamSeal`) |
| Switch | `<data root>/steam/session.txt` on the SD card | A key derived from the console's serial number and device ID |
| PS5 | `<data root>/steam/session.txt` | A key derived from the console's OpenPsId |

- **A copied file opens nowhere else:** a copy of the file doesn't open on
  another phone or console. On Android it also stops opening once the app is
  uninstalled or the player logs out, because the key is deleted.
- **On the consoles, this binds the token to the console rather than hiding
  it:** someone who has the console itself can still read it.
- **Without a key:** if a device has no key to give, the token is stored
  unsealed. The log says so, and the token is sealed at the next load once a key
  is available.
- **How the file is written:** to a `.part` file with mode 0600 (where the file
  system has modes), then renamed over the old one.
- **The token stays in the host:**
  - The guest only gets rows, texts, the QR code's modules and preview pictures.
  - What the player types passes through the guest once, and the guest wipes its
    copy.
  - The host wipes the password, the Steam Guard code, the token, the channel's
    keys and the message buffers that held any of them before freeing them.

## Tests

`tools/test_steam_client.py` builds the client natively with the PS5 host's
flags and checks everything that works offline:

- the crypto against known answers;
- the session file and its seal (with a test secret standing in for a
  console's);
- the chunk packings;
- the paths an item's files may take.

The sign-in, the downloads and the revoke need Steam and an account that owns
MCC, so they're only tested on devices.
