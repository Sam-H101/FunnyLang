# suno_grab — a share link, and the file behind it

Every other example in this tree is the thing being *called*: a web server, a chat room, a
database, three games. This one dials **out**. It takes a publicly shared Suno link, finds the
audio the song actually publishes, downloads it with a progress bar, writes the title, artist,
year, style and cover art into it, and gives it a filename that will still be legal when the file
is copied to a phone.

```console
$ funny run extensive_examples/suno_grab/grab.funny -- https://suno.com/s/<code>
resolving   suno.com/s/<code>
  title     <the song's title>
  artist    <the creator's display name>
  audio     cdn1.suno.ai/57c1ce7d-....mp4  (mp4)
downloading  ##############################  15,608,030 / 15,608,030 B
  tagged    MP4 ilst, cover 84932 bytes
wrote     ./<the song's title>.mp4  (15,693,534 B, MP4 ilst, via api)
```

**It writes an `.mp4`, not an `.mp3`, and that is not a shortcut.** See
[why there is no mp3](#why-there-is-no-mp3) — it is the most interesting thing this example found
out, and it is the reason the example is shaped the way it is.

```console
$ funny run extensive_examples/suno_grab/grab.funny -- <url> <url> --workers 3 --out ./songs
$ funny run extensive_examples/suno_grab/grab.funny -- --from-file links.txt --manifest run.json
$ funny run extensive_examples/suno_grab/grab.funny -- <url> --print-only
$ funny run extensive_examples/suno_grab/grab.funny -- <url> --name "{artist} - {title}"
$ funny run extensive_examples/suno_grab/grab.funny -- <url> --format mp4    # the video
$ funny run extensive_examples/suno_grab/tag.funny  -- "./The Hacker Went Down to Prod.m4a"
$ funny run extensive_examples/suno_grab/measure.funny
$ funny test extensive_examples/suno_grab
```

Exit status is `0` when everything arrived, `1` when nothing did, and `2` when some did and some
did not — so a script can tell "the network is down" from "one of these forty links was deleted".

## What is here

| file | what it is |
| --- | --- |
| `fetch.funny` | HTTP/1.1 **client**: TLS, redirects, chunked, `Range`, retry, streamed to disk |
| `suno.funny` | a share link → what it publishes, by a ladder of four rungs |
| `htmlbits.funny` | the four shapes of somebody else's HTML this needs, and nothing more |
| `id3.funny` | ID3v2.3, written and read, for when the file really is an mp3 |
| `mp4tags.funny` | iTunes-style tags written into an MP4 box tree — what usually runs |
| `namer.funny` | a title → a filename that survives being copied to another computer |
| `pipeline.funny` | resolve, fetch, tag, name: the one place the steps exist |
| `grab.funny` | the command line |
| `worker.funny` | one intern, for `--workers` |
| `tag.funny` | read the tags back out of a file |
| `measure.funny` | how long each step takes, against the fixture |
| `fixture.funny`, `fixtures/` | the golden's other end: ten response shapes and a Suno-shaped API |
| `test_grab.funny`, `.expected` | the golden — seven parts, no internet |

## Why there is no mp3

The plan for this example assumed the share page carries the audio, the way a page usually does.
It does not. Probing one real share link on 2026-09-18 — every probe and its answer is in
[PLAN.md](PLAN.md) §10 D1 — found:

- **no `og:audio` meta tag.** The page has `og:title`, `og:image`, `og:type: music.song`, and
  nothing at all that names the audio.
- the clip record embedded in the page hands a signed-out reader
  `"audio_url": "https://studio-api.prod.suno.com/api/forbidden"`. That is not a bug being
  worked around; it is the site deliberately not telling you.
- `https://cdn1.suno.ai/<id>.mp3` answers **403**, `<Code>MissingKey</Code>` — the mp3 is served
  from CloudFront behind **signed URLs**, and only a signed-in owner is handed the signature. A
  browser `User-Agent`, `Referer` and `Origin` change nothing.
- but the **clip API answers unauthenticated**: `https://studio-api.prod.suno.com/api/clip/<id>`
  returns the title, the style tags, the creator's handle, the cover, the video, and `media_urls`.
- and `media_urls` lists exactly one entry: a CloudFront `.m4a`, `"content_type": "m4a-opus"`.

So the one *audio* rendition Suno publishes for a share link is Opus in an MP4 container. Turning
that into an mp3 would need an Opus decoder and an MP3 encoder written in FunnyLang — tens of
thousands of lines of signal processing — and shelling out to `ffmpeg` is not available either:
`computer` makes no subprocess calls, by design, and that is not being relaxed for one example.

**And then running the finished program found the rest of it.** That `.m4a` downloads exactly —
6,331,654 bytes, matching its own `Content-Length` — and its first bytes are `41 5f ac a9 2f 62
29 59`. There is no `ftyp`. There is no box structure at all. The runtime's own C client fetches
byte-identical bytes, so it is not this program mangling them: **Suno serves that rendition
encrypted.** It is not a playable file and no tagger can touch it.

What *is* served in the clear is the video, `cdn1.suno.ai/<id>.mp4`, which begins
`00 00 00 20 66 74 79 70 69 73 6f 6d` — a plain `ftyp isom` MP4 — and which carries the audio.

So **`--format` defaults to `mp4`**, because it is the only rendition of a shared song that
actually plays. `--format m4a` still fetches the encrypted bytes if you want them, keeps them
exactly as they arrived, and says so instead of failing somewhere confusing:

```console
$ funny run extensive_examples/suno_grab/grab.funny -- <url> --format m4a
warning   the bytes served for this m4a are not an MP4 container -- Suno serves this rendition
encrypted. The file was kept as it arrived and left untagged; --format mp4 is the rendition that
plays.
```

A program that wrote encrypted Opus frames into a file called `.mp3` would be lying, and every
player would catch it. So this one writes what it was given, calls it what it is, and says so here.

`--format mp3` still exists, and fails with the sentence that explains itself:

```console
$ funny run extensive_examples/suno_grab/grab.funny -- <url> --format mp3
failed    suno.com/s/<code> -- Suno publishes no mp3 for a shared link: the mp3 on its CDN is
behind a signed URL only a signed-in owner is given. What it does publish here is m4a, mp4 --
try --format m4a, or pass your own session with --header "Cookie: ..." if the song is yours.
```

That last clause is the real escape hatch and it is a general one: `--header` is repeatable and
goes straight onto every request. Nothing about credentials is implemented specially, stored, or
read from a file. If you are the owner and you hand the program your own session, the resolver
gets a real mp3 URL and `id3.funny` tags it as ID3v2.3 — which is why that module is built and
tested even though the default path never reaches it.

## How a share link becomes a file

Four rungs, and `via` in the output says which one answered:

1. **the URL** — `/song/<uuid>` carries the id. `/s/<code>` does not, so the page is fetched and
   the first UUID-shaped run in it is the id. Matching the *shape* rather than a key path is
   deliberate: the path moves between deploys, the shape does not.
2. **the clip API** — public, and the source of everything. `via: "api"` is the normal answer.
3. **the page's meta tags** — `og:title` and `og:image` when the API cannot be reached. Enough for
   a title and a cover, never enough for the audio.
4. **the pattern** — `cdn1.suno.ai/<id>.mp4`, confirmed with a **`GET` of one byte via `Range`,
   never a `HEAD`**, because that CDN answers `403` to a `HEAD` and `200` to the `GET` of the very
   same URL.

**`via: "guess"` in your output means rungs two and three both failed and should be looked at.**
That is what the field is for.

## Writing HTTP the other way round

`internet.go_brrrr` exists and is written in C, so why is there a client here in FunnyLang? CI
decides it: `funny test extensive_examples` runs with `FUNNY_NO_NET=1`, which makes `go_brrrr`
raise before it sends a packet, while `slide_into` to loopback stays deliberately allowed — a test
runner still has to be able to test a server. A client built on `slide_into` is one CI can
actually run, so the code that runs against the real internet is the code the golden proved.

It also buys three things `go_brrrr` cannot: a progress callback, a resumable `Range`, and
**relative** `Location` handling (`go_brrrr` follows only absolute ones, as `native/platform.c`
says in its own note).

The three places a hand-written client usually breaks, each of which is a test:

- **a read does not come back whole.** A chunk-size line split across two reads is not rare, it is
  Tuesday — and the first request of every real run is chunked, because that is how Cloudflare
  serves the share page. So the reader is a buffer with `want(n)`, never a read whose result is
  assumed complete. The fixture's `/chunked` route really does split `11\r\n` across two writes.
- **a `200` answering a `Range`.** The server ignored it and is sending the whole body. Appending
  that to the partial file on disk makes a file of very nearly the right length and entirely the
  wrong bytes — and nothing downstream notices, because the welded-on bytes are real bytes from
  the same file. The rewind happens between the head and the body, the only place it can. The
  golden caught this one.
- **a worker that only stops when told** hangs the whole process the moment its boss raises
  instead of telling it. The fixture also gives up after thirty seconds of silence. The golden
  caught this one too, by hanging.

## Two taggers, because a container is not a file extension

`id3.funny` writes ID3v2.3: UTF-16 text frames, so an accented title or an emoji survives where
ISO-8859-1 could not hold it; `APIC` cover art; `TXXX`; and a **syncsafe** tag size — 28 bits over
four bytes with the top bit of each clear, so the header cannot look like an MPEG frame sync. Get
that wrong and the file plays perfectly and its tag is invisible, which is the worst kind of wrong
because nothing errors. The golden checks those four bytes against their own expected values
rather than against the reader in the same file.

`mp4tags.funny` is what runs on the default download, and is the harder of the two. Tags live at
`moov/udta/meta/ilst`, four levels down, and every box on the way carries its own byte count — so
changing one string at the bottom means every ancestor's size field has to be rebuilt on the way
back up. And then the part that actually breaks files:

> A `stco` box holds the **absolute file offset** of every chunk of audio. `moov` sits before
> `mdat` in anything meant to be streamed, so growing `moov` by the length of a title pushes the
> audio down the file and every one of those offsets now points at the wrong byte. The file still
> parses. Players seek to the wrong place, or refuse it.

A tagger that skips that passes every round-trip test and corrupts every real file it touches. So
the fixture's MP4 carries real chunk offsets, and the golden asserts each one moved by exactly the
number of bytes `moov` grew, and that offset zero still points at the same byte it did before.

Also here: `©nam` is **four** bytes beginning `0xA9`, not the five UTF-8 spells `©` with. Building
one from a `yapstring` makes a five-byte box type and every box after it is read from the wrong
offset. That bug was written, and the golden caught it — the cover simply stopped coming back.

## What it measured

`measure.funny`, against the fixture on loopback, on one developer machine:

| step | ms |
| --- | --- |
| resolve a full song link | 1.0 |
| resolve a short share link (a page fetch first) | 30.5 |
| fetch the audio | 14.6 |
| fetch the cover | 14.9 |
| write the MP4 tags | 0.17 |
| read them back | 0.24 |
| the whole pipeline, one song | 30.4 |

The two fetches are of a 1.5 KB fixture, so they are latency and not bandwidth; against the real
site they are entirely somebody else's network. **Every other row would be the same for a 6 MB
song** — tagging costs a fifth of a millisecond because it is a handful of `blob` concatenations,
not a parse of the audio.

**There is no speed-up table, on purpose.** Eight songs take 0.42 s with one worker, 0.14 s with
two and 0.05 s with four — which falls faster than the worker count rises, and no pool can do
that. It is `fixture.funny` accepting one connection at a time on a 20 ms poll: with one worker
every request waits for the next poll, and with several one is always already pending. So that
table measures the fixture as much as the pool. The honest claim for `--workers` is the design
one, not a multiple: on the real internet it hides the two round-trips of resolving one song
behind the download of another. The default is **2**, not one per core, because the other end is
somebody else's server.

Getting even this far took throwing one measurement away: the first run reported two workers as
27× faster than one, which was the first intern in the process compiling the whole module graph
and the later ones finding it warm. `measure.funny` now hires and dismisses a worker before the
clock starts.

## What it is not

- **It downloads publicly shared songs**, from links their owners chose to publish, using the
  audio URL the song's own public API advertises. It does not sign in, it carries no key, and
  there is nothing in it that reaches a song that is not already reachable by anyone holding the
  link. Where Suno withholds something from a signed-out reader — the mp3 — this program does not
  get it either, and says so rather than working around it.
- **The song belongs to whoever made it.** What this does is save a copy of a file that is already
  being served; what anybody then does with that copy is between them, the person who made it, and
  Suno's terms, and it is not this example's to answer.
- **It is not an mp3 downloader**, for the reasons above. It writes `.mp4` by default, because that
  is the only rendition of a shared song that is served unencrypted. The `.m4a` that the API
  advertises is encrypted and is fetched only if you ask for it by name.
- **It does not decrypt anything, and there is no code here that tries.** Where Suno withholds
  something — the mp3 behind a signed URL, the encrypted `m4a` — this program reports that and
  stops. "Suno serves this rendition encrypted" is the end of the sentence, not the start of a
  workaround.
- **It does not transcode.** No format conversion of any kind. The bytes written are the bytes
  served, plus a tag.
- **`suno.funny` scrapes somebody else's site and will break.** The ladder makes that a sentence
  rather than a crash; it does not make it not happen. PLAN.md §10 D1 is dated for exactly this
  reason, and `via` tells you when the rungs have started to slip.
- **The golden never reaches the network.** CI proves the client, the taggers, the namer and the
  pipeline against a fixture on loopback. **It cannot prove that Suno still answers the way it did
  on the day §10 D1 was written.**
- **The CLI's argument parsing is the one part not covered by the golden.** `grab.funny` ends in
  `dip(main(...))` and this language has no subprocesses, so the golden drives `pipeline.funny` —
  the module `grab.funny` and `worker.funny` both call — rather than the command line itself.
- **No gzip** (the client sends `Accept-Encoding: identity` and means it), **no keep-alive** (one
  request per connection, which costs a handshake and saves a state machine), no HTTP/2, no
  cookies of its own, no proxy support.
- **No ID3 unsynchronisation, compression, encryption or extended header; no ID3v2.4, no Vorbis
  comments, no FLAC.** Two tag formats onto two containers, and `htmlbits.funny` is a scraper for
  four specific shapes rather than an HTML parser.
- **No playlists or profiles.** `--from-file` reads a list of links, which covers the same need
  without reverse-engineering a paged API.
- **It is polite by construction and easy to make impolite.** Two workers by default, a hard cap
  of eight, exponential backoff with `Retry-After` honoured, and a `User-Agent` that says what it
  is. Turning it into a scraper of a whole site is a few edits away and is not what it is for.
