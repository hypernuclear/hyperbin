# Distribution — signing, updates, analytics

hyperbin ships as a direct download: a notarized DMG for macOS and a
signed EXE installer (plus an MSIX) for Windows. There is no store, no
license check, and no account. Updates come from a Sparkle appcast
served out of Cloudflare R2.

Everything below happens on a `v*` tag. Pull requests build and test on
all three targets and stop there — see `.github/workflows/release.yml`.

## The two version numbers

`CMakeLists.txt` carries the marketing version (`project(hyperbin
VERSION x.y.z)`). The updaters do not compare it. They compare
`APP_VERSION_BUILD`, a commit count, which lands in `CFBundleVersion`
on macOS and goes to `win_sparkle_set_app_build_version` on Windows.

That split exists because `CFBundleVersion` must be numeric and two
builds of the same `0.1.0` are indistinguishable to Sparkle. A commit
count only ever increases.

**The CI checkout must be full depth.** `git rev-list --count HEAD` on a
shallow clone returns 1, so every release would report build 1 and no
update would ever be offered. Both jobs set `fetch-depth: 0`.

## Releasing

1. Bump `project(hyperbin VERSION ...)` and land it on `main`.
2. Tag it: `git tag v0.2.0 && git push origin v0.2.0`.
3. Watch the run. On success R2 holds:

```
hyperbin-builds/
├── releases/0.2.0/
│   ├── hyperbin-0.2.0-macos-arm64.dmg     the download
│   ├── hyperbin-0.2.0-macos-arm64.zip     what Sparkle installs
│   ├── hyperbin-0.2.0-macos-x64.dmg
│   ├── hyperbin-0.2.0-macos-x64.zip
│   ├── hyperbin-0.2.0-windows-x64.exe
│   └── hyperbin-0.2.0-windows-x64.msix
├── latest/mac/appcast.xml
└── latest/win/appcast.xml
```

Artifacts upload before the feeds do. A feed naming a file that is not
there yet sends every running copy to a 404 — after it has already told
the user an update is waiting.

`latest-beta/` is deliberately unused. Stable is the only channel; the
layout leaves room to add a second one without moving the first.

## The update signing key

Sparkle and WinSparkle share one EdDSA keypair. The public half is baked
into every binary (`SUPublicEDKey` in `Info.plist`, and a compile
definition on Windows). The private half signs each release and exists
only as a GitHub secret.

**Generate it yourself — do not paste the private key into a chat, a
ticket, or a file in this repo.** On a Mac:

```sh
curl -L https://github.com/sparkle-project/Sparkle/releases/download/2.9.0/Sparkle-2.9.0.tar.xz | tar xJ ./bin/generate_keys
./bin/generate_keys --account hyperbin                # creates it, prints the public key
./bin/generate_keys --account hyperbin -x private.key # export, to paste into the secret
```

**`--account hyperbin` is not optional on a machine that already has a
Sparkle key.** Without it, `generate_keys` uses the global `ed25519`
account, finds the existing key, and prints *that* — so on the machine
that already signs hypershot you would silently end up with hypershot's
key in hyperbin's secrets, and one compromise would then reach both
products. The flag is Sparkle's own answer to this; its help says to use
different accounts for different organizations, and the same reasoning
applies per product.

Put `private.key`'s contents in the `SPARKLE_EDDSA_PRIVATE_KEY` secret
and the printed public key in `HYPERBIN_EDDSA_PUBKEY` in
`CMakeLists.txt` — the public half is not a secret and belongs in the
tree — then delete `private.key`. The Keychain copy is the backup;
losing it means no
existing install can ever be updated again, because they only accept
signatures from the key they shipped with.

That also makes this a decide-once choice. The public key is baked into
every binary, so it can be changed freely today — hyperbin has shipped
nothing — and never again after the first release.

### The failure this is guarded against

`generate_appcast` compares the signing key against the `SUPublicEDKey`
it finds inside the app. When they disagree it prints a warning, **omits
the signature, and exits 0**. The release then looks entirely
successful and publishes a feed no client will install. The publish job
counts `sparkle:edSignature` in the finished appcast and fails if it is
not exactly two — so a mismatched pair stops the release instead of
quietly breaking updates.

## Required GitHub secrets

Repository → Settings → Secrets and variables → Actions. The repo is
public, so nothing here can live in the tree.

### Shared with the other products, or not

Most of these describe *hypernuclear* rather than *hyperbin*, and are
copied verbatim from any other repo that already releases. Three must
not be.

| Reuse | Why |
|---|---|
| `APPLE_DEVELOPER_ID_CERT_BASE64`, `APPLE_DEVELOPER_ID_CERT_PASSWORD` | One Developer ID certificate signs everything the org ships |
| `APPLE_NOTARIZATION_APPLE_ID`, `APPLE_NOTARIZATION_TEAM_ID` | Same Apple account, same team |
| `AZURE_TENANT_ID`, `AZURE_SIGNING_ACCOUNT`, `AZURE_CERT_PROFILE` | Same tenant, same Trusted Signing account and profile |
| `CLOUDFLARE_R2_ENDPOINT` | Per Cloudflare **account**, not per bucket |

| Make a new one | Why |
|---|---|
| `COUNTLY_APP_KEY` | A Countly app key *is* the dashboard. Reusing another product's key merges two products' metrics into one app, with no way to separate them afterwards |
| `SPARKLE_EDDSA_PRIVATE_KEY` | See above — `--account hyperbin`. Sparkle permits one key across apps; a separate one means a leaked key can only push a malicious update to one product |
| `CLOUDFLARE_R2_ACCESS_KEY_ID` / `_SECRET_ACCESS_KEY` | Scope a fresh R2 token to `hyperbin-builds` alone. **This repo is public** — a token that also reaches another bucket turns any leak here into a compromise of that product's releases too |

Two that could go either way, where the safer answer costs one extra
step: `APPLE_NOTARIZATION_PASSWORD` (an app-specific password — Apple
allows 25, and a per-repo one can be revoked without breaking the
others) and `AZURE_APPLICATION_ID` / `AZURE_CLIENT_SECRET` (a second app
registration against the same signing account, revocable on its own).

### Apple — signing and notarization

| Secret | What it is |
|---|---|
| `APPLE_DEVELOPER_ID_CERT_BASE64` | The "Developer ID Application" certificate exported as `.p12`, then `base64 -i cert.p12 \| pbcopy` |
| `APPLE_DEVELOPER_ID_CERT_PASSWORD` | The password set when exporting that `.p12` |
| `APPLE_NOTARIZATION_APPLE_ID` | Apple ID used for notarization |
| `APPLE_NOTARIZATION_PASSWORD` | An **app-specific password**, not the account password — appleid.apple.com → Sign-In and Security → App-Specific Passwords |
| `APPLE_NOTARIZATION_TEAM_ID` | `67J6LGVJDP` — same team the local build already signs with |

### Update signing

| Secret | What it is |
|---|---|
| `SPARKLE_EDDSA_PRIVATE_KEY` | From `generate_keys -x` above |

The **public** key is not a secret and is not stored as one. It lives in
`CMakeLists.txt` as the default for `HYPERBIN_EDDSA_PUBKEY`: it ships
inside every binary regardless, so hiding it buys nothing, while making
it a secret turns a reviewable constant into an invisible input that can
silently disagree with the private half — the exact failure the publish
job's signature count guards against.

### Windows — Azure Trusted Signing

| Secret | What it is |
|---|---|
| `AZURE_TENANT_ID` | Entra tenant |
| `AZURE_APPLICATION_ID` | App registration (client) ID |
| `AZURE_CLIENT_SECRET` | That registration's client secret |
| `AZURE_SIGNING_ACCOUNT` | Trusted Signing account name |
| `AZURE_CERT_PROFILE` | Certificate profile name |

The `Publisher` in `packaging/windows/AppxManifest.xml` must match the
certificate's Subject character for character. `makeappx` will happily
pack a mismatched one and Windows will refuse to install it.

### Cloudflare R2

| Secret | What it is |
|---|---|
| `CLOUDFLARE_R2_ACCESS_KEY_ID` | R2 API token, Object Read/Write on `hyperbin-builds` |
| `CLOUDFLARE_R2_SECRET_ACCESS_KEY` | Its secret |
| `CLOUDFLARE_R2_ENDPOINT` | `https://<account-id>.r2.cloudflarestorage.com` — **not** the custom domain |

There are two URLs for the same bucket and they are not
interchangeable:

- **`CLOUDFLARE_R2_ENDPOINT`** is the S3 API endpoint, from R2 →
  Overview. It takes authenticated `PUT`s, and it is the only one
  `aws s3 cp` can talk to. Naming the custom domain here fails the
  upload — that host does not speak the S3 API.
- **`CDN_URL`** is the public custom domain,
  `https://hyperbin-builds.hypernuclear.com`. Anonymous reads only.
  It is **not a secret**: it is in `release.yml`'s `env:` block and in
  `HYPERBIN_CDN_URL` in `CMakeLists.txt`, because it gets baked into
  every binary as the appcast URL. There is nothing to protect in a
  hostname that ships inside the app.

So the bucket needs the custom domain attached and public, but you enter
it in neither secret. The workflow writes through the endpoint and users
read through the domain.

### Analytics

| Secret | What it is |
|---|---|
| `COUNTLY_APP_KEY` | The app key for hyperbin's Countly app |

Not a credential in the usual sense — it identifies which Countly app a
request belongs to and ships inside every binary, so anyone can read it
out of a download. It is a secret here only because this repo is public
and a key sitting in a public tree is an invitation to write junk into
the dashboard. Create a separate Countly app for hyperbin rather than
reusing another product's key.

A build with no key compiled in has no analytics backend at all: the
menu entry is hidden and `Analytics::create()` returns null. That is
what every local build does.

## Analytics, and what is actually collected

Off by default, and off means the SDK is never initialised, no device id
is computed, and no request is made. Verified by running a build with a
real app key against a local HTTP sink: zero requests while opted out.

The switch is "Share Usage Data" in the menu bar, next to the other
things the app can be told to stop doing.

When on, what goes out is: which effect is running, density and
threshold changes, whether the bin got emptied, the app version, and the
OS and its version. The device id is a SHA-256 of a hardware
identifier — one-way, so it distinguishes machines without identifying
one. There is nothing about file names, paths, or bin contents, and no
code path that could send them.

## Building a release locally

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DHYPERBIN_EDDSA_PUBKEY="<public key>" \
      -DHYPERBIN_COUNTLY_APP_KEY="<app key>"
cmake --build build -j
./scripts/package-macos.sh build 0.2.0 arm64
```

That signs with the first Developer ID in the Keychain (override with
`CODESIGN_IDENTITY`) and produces a DMG. Notarization is not included —
it needs credentials and takes minutes; CI does it.

Configuring with no `HYPERBIN_EDDSA_PUBKEY` warns loudly and builds an
app that accepts **unsigned** update feeds. Fine for local work, never
for anything anyone else runs.

## Icons

Two icons, from two sources, because they do two different jobs.

**The app icon** is the alley illustration. `resources/app_icon.png` is
the 1024px RGBA master; `resources/hyperbin.icns`,
`resources/hyperbin.ico` and `packaging/windows/Assets/*` are all
downscales of it.

```sh
python3 scripts/gen_icons.py                      # regenerate from the master
python3 scripts/gen_icons.py path/to/new_art.png  # rebuild the master first
```

The second form cuts the squircle out of a flat-backed render by
flood-filling inward from the four corners rather than drawing a rounded
rectangle over it: the corner is a squircle, and any radius picked by eye
leaves either a sliver of the dark backing or a bite out of the border.
Needs Pillow.

**The menu-bar icon** is not generated here at all. `TrayMenu::trayIcon()`
draws it at runtime from `resources/hyperbin.svg`, because a menu-bar icon
has to be a flat monochrome mask that the shell recolours — the
illustration would come out as a dark smudge.

Everything is committed, so an ordinary build generates nothing.

Two things worth knowing. The illustration is a whole scene and stops
being legible below about 32px; that covers everywhere this app's icon
actually appears — the DMG, the Applications folder, Sparkle's update
dialog — but Finder's 16px list view is mud. The fix would be a tighter
crop on the bin for the 16 and 32 variants, which is a design decision
rather than something the script should invent.

And the `.icns` is ~3.5 MB, nearly all of it the 1024px variant — about
6% of a DMG that Qt otherwise dominates. Dropping `icon_512x512@2x` from
`make_icns()` halves it, at the cost of a soft icon in Finder's largest
view. Worth doing only if download size becomes a concern.
