# FunnyLang — `suno_grab` Plan (`extensive_examples/suno_grab/`)

> **Status:** planned, nothing built. Branch `feature/suno-grab`, to be cut from `master`.
> **Prerequisite:** nothing new in the runtime is expected (§6.1 says what would change that).
> Every example in the tree so far is the thing being *called*: a server, a game, a database, a
> builder. This one dials **out**, and it is the first that does — which is the reason to write it
> and the reason most of §4 is about an HTTP client rather than about Suno.
> **Deliverable:** `funny run extensive_examples/suno_grab/grab.funny -- <share-url>` takes a
> publicly shared Suno song link and leaves a tagged `.mp3` on disk — the audio the page itself
> advertises, the cover art embedded, the title, artist and year written as ID3v2.3, the filename
> safe on every platform. Around it: many links at once across `interns`, a resumable download, a
> JSON manifest, and a golden that `funny test extensive_examples` runs **with the network turned
> off**, against a fixture server on loopback that is built in the test.

---

## 0. Rules for the executing agent

1. **An example is FunnyLang.** If it needs something the runtime does not have, that is a
   `RUNTIME_PLAN.md` §9 entry and a small, general primitive — never a special case for one
   example. §6.1 lists the two candidates and why neither is expected to be needed.
2. **The golden never touches the internet.** CI runs `funny test extensive_examples` with
   `FUNNY_NO_NET=1`, which makes `internet.go_brrrr`, `download` and `is_it_up` raise before they
   send a packet. It does *not* block `slide_into` to loopback. That is not an inconvenience to
   work around — it is the constraint that decides §4.1: **the HTTP client is written in
   FunnyLang over `slide_into`**, so the same code path the golden drives is the one a person
   uses against `suno.com`.
3. **No real song bytes are committed.** Fixtures are generated: a valid, silent MPEG-1 Layer III
   file built byte by byte in the test, a 1×1 JPEG likewise, and an HTML page written to match the
   *shape* of a real one (§2.2), not a copy of one. `word_count/`'s generated corpus is the model:
   an expected value that is arithmetic rather than something somebody once measured.
4. **Every example has a README** that says what it is, how to run it, what it measured, and — in
   a section titled exactly that — what it is not. An example that claims more than it does is
   worse than one that does less. Here "what it is not" carries more weight than usual (§8).
5. **`the_script()` for every path.** The example runs from any working directory, and an
   `interns` worker is hired by an absolute path built from it.
6. **A worker cannot import through a parent directory** (`RUNTIME_PLAN.md` §9, the E5 entry), so
   everything `worker.funny` imports lives in this directory.
7. **The resolver is allowed to be wrong, and must say so.** Suno's page is somebody else's and
   can change on any deploy. Every failure to find an audio URL ends in one message naming which
   rung of the ladder (§4.2) failed and what the page looked like, never a stack trace and never
   a silent zero-byte file.
8. **Stage by explicit path; commit per milestone on `feature/suno-grab`; push; never merge to
   master.** Other sessions work on this repository at the same time.
9. **Every deliberate deviation gets a §10 entry**, including the ones that turn out to be wrong.

---

## 1. What "done" looks like

```console
$ funny run extensive_examples/suno_grab/grab.funny -- https://suno.com/song/<id>
resolving  suno.com/song/<id>
  title     Night Bus Home
  artist    @someone
  audio     cdn1.suno.ai/<id>.mp3   3.4 MB
downloading ################################   3,512,064 B in 1.9 s (1.8 MB/s)
tagging     ID3v2.3 — title, artist, year, cover 41 KB
wrote       ./Night Bus Home.mp3
```

```console
$ funny run extensive_examples/suno_grab/grab.funny -- <url> <url> <url> --workers 3 --out ./songs
$ funny run extensive_examples/suno_grab/grab.funny -- --from-file links.txt --out ./songs --manifest songs.json
$ funny run extensive_examples/suno_grab/grab.funny -- <url> --no-tags --name "{artist} - {title}"
$ funny run extensive_examples/suno_grab/grab.funny -- <url> --print-only          # resolve, print JSON, download nothing
$ funny run extensive_examples/suno_grab/tag.funny  -- ./Night\ Bus\ Home.mp3      # read the tags back out
$ funny test extensive_examples/suno_grab
```

Exit codes: `0` everything downloaded, `1` nothing did, `2` some did and some did not (and the
manifest says which). A link that resolves but whose audio 404s is a failure of that link, not of
the run.

---

## 2. What a Suno share link actually is

### 2.1 The shape of the problem

A share link is an **HTML page**, not a file. The mp3 lives on a CDN host under a URL the page
carries. So the program is three steps, and each is a module:

1. **Resolve** — GET the share page, find the audio URL and whatever metadata is lying next to it.
2. **Fetch** — GET that URL, following redirects, over TLS, to a file on disk.
3. **Tag** — write ID3v2.3 over the bytes that arrived, cover art included.

Step 2 is the one with real engineering in it and step 3 is the one with real bit-twiddling in it.
Step 1 is the fragile one, and §4.2 is built around admitting that.

### 2.2 What S0 must confirm before anything else is written

The resolver is written against what a real page holds **today**, and this plan does not pretend
to know that. Milestone S0 (§7) is a single session with a browser-less `funny` script that fetches
one public share link with `go_brrrr` — outside CI, where the network is allowed — and records, in
this file's §10, the answers to:

- Which URL forms exist (`/song/<uuid>`, a short `/s/<id>` form, an `/embed/` form), which redirect
  to which, and whether any of them needs a cookie or a JS-rendered page to carry the audio URL. If
  the audio URL is *only* reachable by running JavaScript, this example stops and becomes a
  different one — §6.3.
- Whether an OpenGraph `og:audio` / `twitter:player:stream` meta tag is present, and whether it
  points at the mp3 or at a player page.
- Whether a JSON island (Next.js `__NEXT_DATA__`, or a `self.__next_f.push` stream) carries the
  clip record, and which keys hold audio URL, title, display name, image URL, tags/style, and
  creation date.
- The CDN host and the path pattern, and whether it answers `Range` requests (it must, for
  `--resume`) and what it sends for `Content-Length` and `Content-Type`.
- What an unlisted or deleted song answers with.

S0's output is a fixture: the page saved to `fixtures/song_page.html` **with the audio URL rewritten
to the loopback fixture server and everything not needed for parsing deleted** — a few hundred
lines, structurally the real thing, containing no song. That file is what the golden parses, and
§10 records the date it was captured, because a fixture whose provenance is unrecorded is a
fixture nobody can refresh.

---

## 3. Why this shape

**The interesting half of this example is not Suno.** It is that FunnyLang has never had to be a
client. `web_server/`, `web_server_https/`, `chat/`, `kv/`, `chess/`, `battleship/` all sit and
wait; `internet.go_brrrr` exists, but it is C, it buffers the whole body in memory, and CI cannot
run a line of it. Writing HTTP/1.1 the other way round — request framing, redirect chains,
`Content-Length` versus chunked, `Range`, retry with backoff, and a TLS connection that verifies
a chain — is a body of work the tree does not contain, and it is all ordinary FunnyLang.

Three consequences for the code:

- **`fetch.funny` knows nothing about Suno**, and `suno.funny` knows nothing about sockets. The
  split is what lets the golden exercise the client against nine fixture responses that have
  nothing to do with music, and lets the resolver be tested against saved HTML with no server at
  all.
- **Streaming, not slurping.** The client hands the caller a body a chunk at a time and the
  downloader appends each chunk to a `.part` file. A 6-minute song is only a few MB and `go_brrrr`
  would survive it, but a downloader that holds the whole file in memory is a downloader that
  cannot show a progress bar or resume, and both of those are the point.
- **Threads where they help and nowhere else.** One song is one connection and there is nothing to
  parallelise inside it. Many songs are embarrassingly parallel, so `--workers N` is a pool over
  `interns` — a job per free worker, `word_count/`'s pool, because songs differ in size the way
  its files differed in length. The default is **2**, not the core count: the other end is
  somebody else's server (§8, politeness).

---

## 4. Each file

| file | what it is | est. lines |
| --- | --- | --- |
| `fetch.funny` | HTTP/1.1 client over `slide_into`: TLS, redirects, chunked, `Range`, retry | 420 |
| `suno.funny` | share URL → `{audio_url, title, artist, ...}`, the ladder of §4.2 | 260 |
| `htmlbits.funny` | the small amount of HTML poking the resolver needs, and nothing more | 140 |
| `id3.funny` | ID3v2.3 writer and reader over `blob`, cover art included | 380 |
| `namer.funny` | a title and a template → a filename that is legal on every platform | 120 |
| `grab.funny` | the CLI: arguments, the pool, progress, the manifest, exit codes | 400 |
| `worker.funny` | one intern: resolve, fetch, tag, `dm` the result back | 90 |
| `tag.funny` | read the tags back out of an mp3 — the reader, as a command | 70 |
| `fixture.funny` | the golden's server: nine canned responses over loopback | 260 |
| `test_grab.funny`, `.expected` | the golden, §5 | 420 + 180 |
| `fixtures/song_page.html` | the captured, rewritten, song-free share page (§2.2) | ~200 |
| `certs/` | the test CA and identity, copied from `battleship/` | copied |
| `README.md` | §8 | 400 |

### 4.1 `fetch.funny` — HTTP/1.1, the other way round

```funny
flex bet parse_url(url)          // {"scheme","host","port","path","query"} or ghost
flex bet get(url, opts)          // one request, redirects followed -> a response
flex bet get_to_file(url, path, opts)   // the same, streamed to disk; returns {"bytes","status","resumed"}
flex bet head(url, opts)
```

`opts` is a `groupchat`: `headers`, `timeout_ms` (per read, default 15000), `max_redirects`
(default 5), `retries` (default 3), `ca` (a PEM, for the golden's own CA), `range_from`,
`on_progress` (a function called with bytes-so-far and total, at most ten times a second), and
`max_bytes` (default 256 MB — a client with no ceiling is a client that can be handed a stream
that never ends).

What it has to get right, each of which is a test in §5:

- **Request framing.** `GET <path> HTTP/1.1`, `Host`, `User-Agent: suno_grab/1.0 (FunnyLang
  example)`, `Accept-Encoding: identity` — because a client that cannot inflate gzip must say so
  rather than receive it and guess — and `Connection: close`, which costs a handshake per request
  and buys not having to write connection reuse. §6.2 says why that trade is taken.
- **Reading a response without a length.** Status line, headers until the blank line, then a body
  delimited by `Content-Length`, by chunked framing, or by the connection closing — three cases,
  and the third is only legal when neither of the others is present. Header names fold to
  lowercase; a header may repeat; a line may be folded (obsolete, and rejected rather than
  half-supported).
- **Chunked decoding** over `blob`: size line in hex, extensions after a `;` ignored, the chunk,
  `CRLF`, a zero chunk and the trailers. The partial-read case matters and is where a hand-written
  client usually breaks: `hear_them_out` may return a chunk header split across two reads, and on
  TLS it may return `ghost` immediately after `hold_up` said readable (`STDLIB.md`'s own warning).
  The reader is therefore a **buffer with a `want(n)` and a `want_line()`**, never a read whose
  result is assumed whole.
- **Redirects.** 301/302/303/307/308, absolute and relative `Location` (the runtime's own
  `go_brrrr` only handles absolute — `native/platform.c` §9 says so — and this one handling
  relative is a fair thing for the README to point at), cross-host allowed, http→https allowed,
  https→http **refused**, a chain no longer than `max_redirects`, and the `Authorization` header
  dropped on a host change.
- **`Range`.** `bytes=<n>-`, a 206 with `Content-Range` validated against what is already on disk
  (start offset and total size both), a 200 answer to a `Range` request meaning "start over" and
  being honoured as such rather than appended to a partial file.
- **Retry.** Connection failure, a timeout, 429 and 5xx: up to `retries` attempts, exponential
  backoff starting at 500 ms with jitter, `Retry-After` honoured when present and sane (capped at
  60 s). 4xx other than 429 is not retried. Retrying a partial download resumes it.
- **TLS.** `slide_into(host, 443, {"tls": fax, "server_name": host, "timeout_ms": …})`, and
  `{"ca": path}` when the caller passed one. There is no way to turn verification off and the
  example does not add one.

It raises nothing that is not a `SkillIssue` with a sentence a person can act on, and a failed
`get_to_file` leaves the `.part` file alone, because that file is what `--resume` needs.

### 4.2 `suno.funny` — the ladder

```funny
flex bet resolve(url, opts)   // -> {"audio_url","title","artist","image_url","tags","created","id","source","via"}
```

`via` is which rung answered, and it is printed under `--print-only` and stored in the manifest,
so a run that starts behaving oddly six months from now says which assumption expired.

1. **The URL itself.** Recognise the share forms S0 found, pull the id out, normalise a short link
   by following it (`fetch.head`) to the canonical one.
2. **The page's meta tags.** `og:audio` (or the stream tag S0 confirms) for the audio, `og:title`,
   `og:image`, `og:description`. Cheapest, most stable, and usually enough.
3. **The JSON island.** The embedded state blob, parsed with the native `json` module, walked for
   the first object that has both an audio-URL-shaped key and an id matching the URL's. Walking for
   a *shape* rather than a fixed key path is deliberate: the path moves between deploys, the shape
   does not.
4. **The last resort:** the CDN URL pattern built from the id, confirmed with a `HEAD` that must
   answer 200 and an audio content type. If that is what answered, `via` says so loudly and the
   README says a `via: "guess"` result is a sign the rungs above it need refreshing.

Then, whatever the rung: the audio URL is checked to be `https://`, to be on a host whose name ends
in one of the CDN suffixes S0 recorded, and to be under `max_bytes`. A resolver that will follow a
page to any URL at all is an SSRF gadget, and this one is a program a person runs on their own
machine, but the check costs four lines.

A page that is a 404, a private song, or a login wall produces one sentence saying which, from a
`{status, marker}` table, not a parse error.

### 4.3 `htmlbits.funny` — deliberately not an HTML parser

`meta_content(html, property)`, `json_island(html, script_id)`, `text_between(html, a, b)`,
`unescape_entities(text)` (the five named ones plus numeric). Case-insensitive tag and attribute
matching, single or double quotes, attributes in either order. It is a scraper for four specific
shapes and the README says so; writing a conformant HTML5 tokenizer is `site_gen/`'s kind of job
and is not this example's.

### 4.4 `id3.funny` — the part that is pure FunnyLang

```funny
flex bet write_tags(mp3, tags)     // a blob in, a blob out, the old tag replaced
flex bet read_tags(mp3)            // -> a groupchat, or ghost if there is no tag
flex bet strip_tags(mp3)
```

ID3v2.3 because it is what every player reads, at the front of the file, with:

- A **syncsafe** size — a 28-bit integer in four bytes with the high bit of each clear, so the
  header cannot look like an MPEG frame sync. Getting this wrong makes a file that plays but whose
  tag is invisible, which is exactly the kind of bug a golden should catch and a person should not
  have to.
- **Text frames** `TIT2` title, `TPE1` artist, `TALB` album (`"Suno"` unless given), `TYER` year,
  `TCON` genre from the style tags, `COMM` comment, `WOAF` the share URL, and `TXXX` frames for
  `SUNO_ID`, `SUNO_STYLE` and `SUNO_CREATED`. Encoding byte `1` — UTF-16 with a BOM — because a
  title with an accent in it is the common case, not the exotic one, and ISO-8859-1 cannot hold it.
- **`APIC`**, the cover: the jpeg fetched from the page's image URL, MIME type from what actually
  arrived rather than from the extension, picture type 3 (front cover), empty description.
- **An existing tag replaced, not stacked.** If the file already begins `ID3`, its header is read,
  its declared length skipped, and the new tag written in front of what is left. `strip_tags` is
  that half on its own.
- **Padding** of 1024 bytes, so a later edit need not rewrite the file. No unsynchronisation, no
  compression, no encryption, no extended header — and the README lists those four as "not".

The reader exists so the golden can round-trip every frame the writer emits, and so `tag.funny` is
a real command rather than a test helper with delusions.

### 4.5 `namer.funny` — a filename that survives being copied to a different computer

Template `{artist} - {title}` and friends; then: path separators and the rest of Windows's
forbidden set replaced, control characters dropped, the reserved device names (`CON`, `PRN`, `AUX`,
`NUL`, `COM1`–`COM9`, `LPT1`–`LPT9`) suffixed, trailing dots and spaces trimmed, the whole thing
capped at 150 codepoints **measured in UTF-8 bytes and cut on a codepoint boundary**, an empty
result falling back to the song id, and a collision resolved as ` (2)`, ` (3)` — checked and
created in one step so two workers cannot both win it.

### 4.6 `grab.funny` — the CLI

Arguments per §1. One link runs on the main thread with a progress bar; several with `--workers N`
hire N interns from `worker.funny`, which get a URL by `dm` and send back
`{"ok", "path", "bytes", "title", "via", "error"}`. Progress with several workers is one line per
finished song rather than N bars fighting over the terminal — the same choice `word_count/` made.
`--manifest PATH` writes the whole run with `filez.write_blob_atomic`. `--resume` keeps `.part`
files; without it they are cleaned up on failure. `computer.until_ctrl_c()` is awaited alongside
the pool so Ctrl-C stops the run, winds every worker down, and leaves the `.part` files intact
(rule 5 of `EXAMPLES_PLAN.md` §0: a program that hires interns winds every one of them down before
it raises).

---

## 5. The golden

`test_grab.funny`, run by `funny test extensive_examples` on every platform, **with
`FUNNY_NO_NET=1`**, and nothing in it reaches further than `127.0.0.1`. Seven parts, each able to
fail on its own:

1. **`parse_url`** — every form, including a port, an empty path, a query, an IPv6 host, and four
   malformed ones that must come back `ghost`.
2. **The client against `fixture.funny`**, a plain-HTTP listener on port 0 hired as an intern:
   a `Content-Length` body; a chunked body delivered in **three writes that split a chunk header
   across two of them**; a body delimited by close; a 302 to a relative `Location`; a chain of
   three redirects; a chain of six, which must fail as "too many"; a 503 with `Retry-After: 0`
   that succeeds on the second attempt; a 206 answering a `Range`; a 200 answering a `Range`,
   which must restart rather than append; and a 404. Byte counts and the decoded body are asserted
   exactly.
3. **TLS**, the same client against a second fixture listener using `certs/site.p12`, verified
   against `certs/ca.pem` passed as `ca` — the one part that proves the outbound path a person
   actually uses.
4. **The resolver** against `fixtures/song_page.html`, with the audio URL rewritten to the fixture
   server: rung 2 asserted on the full page, then rung 3 by deleting the meta tags, then rung 4 by
   deleting the island too, then a private-song page and a 404 page, each asserted to produce its
   own sentence. `via` is asserted every time.
5. **ID3**, with no server at all: a tag written onto a generated mp3 and read back frame for
   frame; a non-ASCII title and a non-ASCII artist round-tripping; an APIC round-tripping byte for
   byte; the syncsafe size asserted **against its own four bytes**, not against the reader that
   produced it; a second `write_tags` replacing rather than stacking; `strip_tags` returning the
   original bytes exactly; and the tagged file's audio bytes asserted unchanged, which is the
   property that makes it still a song.
6. **Naming** — a title with a slash, a title that is `NUL`, an empty title, a 400-character title
   cut on a boundary and still valid UTF-8, an emoji title, and two collisions in a row.
7. **End to end, twice**: one URL through `grab.funny`'s own path, then four URLs through the pool
   with 3 workers, asserting the same four files with the same four byte counts and the same four
   tags — **printed sorted**, so the output does not depend on which worker finished first. Then
   an interrupted download (the fixture closes the connection at 40%) resumed with `--resume`,
   asserting the final file is byte-identical to the uninterrupted one.

The mp3 the fixture serves is generated: a valid MPEG-1 Layer III frame header (`FF FB 90 64`),
its 417-byte frame filled with zeroes, repeated to a size the test states — so the expected byte
counts are arithmetic, the file is a real mp3 by its own header, and no audio anybody made is in
this repository. The cover is a minimal valid JPEG built the same way.

---

## 6. Decisions already made

### 6.1 Nothing new in the runtime is expected

Two candidates were considered and both are refused:

- **gzip/deflate.** The client sends `Accept-Encoding: identity`, so it never needs to inflate.
  An mp3 is already compressed and a share page is small. If S0 finds a host that compresses
  anyway, in defiance of that header, the answer is a `RUNTIME_PLAN.md` §9 entry for a general
  `blob.inflate` — not a special case here. Recorded in §10 if it happens.
- **A progress bar primitive.** `\r` and `yap` are enough.

`vault.sha256` already exists and is what `--verify` would use if S0 finds the CDN sends a digest
header. If it does not, there is no `--verify`, because a checksum of bytes against themselves is
theatre.

### 6.2 `Connection: close`, one request per connection

Keep-alive would save a TLS handshake between the page fetch and the audio fetch. It also means
tracking whether the server honoured it, whether the body was framed such that the next response
can be found, and what to do when it closes anyway. Two extra handshakes per song is a few tens of
milliseconds against a download of several seconds. The README says this outright rather than
letting a reader assume the client is cleverer than it is.

### 6.3 If the audio URL needs JavaScript to appear

Then rungs 2 and 3 are both dead, rung 4 is a guess with no confirmation, and this example cannot
be written honestly — a headless browser is not going in this repository and will not be. S0 finds
this out in the first hour rather than S4 finding it out in the fifth. The fallback, recorded here
so the decision is not made under pressure: the example becomes **a generic share-link grabber**
with the same three modules, whose bundled backend is a service whose page does carry the URL, and
Suno becomes a documented backend that works when it works. `fetch.funny`, `id3.funny`,
`namer.funny` and the whole golden survive that change unaltered; only `suno.funny` and the README
change. That is the reason for the split in §3.

### 6.4 Scope

Not in scope: the video a Suno page may also carry, stems, a playlist or profile page (a paged API
that S0 is not chartered to reverse-engineer — `--from-file` covers the same need with a text file
of links), anything requiring a login, anything requiring an API key, and any form of concurrency
against one host beyond `--workers` (default 2, hard cap 8).

---

## 7. Milestones

One commit each, on `feature/suno-grab`, staged by explicit path.

### S0 — find out what is true
`tools/`-style throwaway script, run **outside CI** with the network on, against one public share
link. Produces: §10 entries answering every question in §2.2, and
`fixtures/song_page.html` captured and rewritten per rule 3. If §6.3's condition is met, this
milestone ends the plan as written and reopens it. **Nothing else starts until this lands.**

### S1 — `fetch.funny` and its half of the golden
The client, `fixture.funny`, and parts 1–3 of §5 — which is most of the risk, tested before there
is anything to point it at. Passing means the client is correct against ten response shapes and a
real TLS chain.

### S2 — `id3.funny`, `namer.funny`, and parts 5–6
No network at all. Independent of S1 and could be done first; it is second because S1 is the
milestone that can fail.

### S3 — `suno.funny`, `htmlbits.funny`, and part 4
Against the S0 fixture. Ends with `--print-only` working against the real site, by hand, outside
CI — and the result pasted into §10, because that is the only evidence the resolver ever worked.

### S4 — `grab.funny`, `worker.funny`, `tag.funny`, and part 7
One song, then many, then resume. Ends with the whole golden green on this machine.

### S5 — the README and the changelog
§8. Plus: `README.md`'s example count (ten → eleven) and its `extensive_examples` bullet,
`CHANGELOG.md` under Unreleased, and this file's §7 statuses and §10.

### S6 — the honest measurement
The numbers the README quotes: time to resolve, time to download a 3 MB file, the tagging cost in
milliseconds, and 1 versus 2 versus 4 workers over eight links — measured against the **fixture
server**, so the number is reproducible and the README says plainly that it measures this program
and not anybody's network.

---

## 8. The README, in outline

1. What it is, and the console session from §1.
2. What is here — the file table.
3. **How a share link becomes an mp3** — the three steps, and the ladder, with the point that
   rung 4 answering means the rungs above it need attention.
4. **Writing HTTP the other way round** — the interesting half: framing, chunked, redirects,
   `Range`, retry, and the three places a hand-written client usually breaks (a split chunk
   header, a `ghost` read on TLS, a 200 answering a `Range`).
5. **ID3v2.3, by hand** — syncsafe sizes, UTF-16 text frames, APIC, and replacing rather than
   stacking.
6. **What it measured** — S6's table, with its caveat.
7. **What it is not.** In one place, in plain words:
   - It downloads **publicly shared** songs, from links their owners chose to publish, using the
     audio URL the page itself advertises. It does not sign in, does not carry a key, does not
     defeat any access control, and there is nothing in it that reaches a song that is not
     already public.
   - The song is still the work of whoever made it. What this program does is save a copy of a
     public file; what a person may then do with it is between them, the person who made it, and
     Suno's terms — and it is not this example's to answer.
   - Default two workers, backoff on 429, a `User-Agent` that says what it is. Turning it into a
     scraper of a whole site is a few edits away and is not what it is for.
   - The resolver scrapes somebody else's HTML and **will** break. §3's ladder makes that a
     message rather than a crash; it does not make it not happen.
   - No gzip, no keep-alive, no HTTP/2, no cookies, no proxy support, no gapless/stem/video.
   - No ID3 unsynchronisation, compression, encryption or extended header; no ID3v2.4, no Vorbis
     comments, no FLAC — it writes one tag format onto one container.
   - The golden never reaches the network, so **CI proves the client and the parser, and cannot
     prove that Suno still answers the way it did on the day §10 was written.**

---

## 9. Cost sheet

| milestone | new lines | notes |
| --- | --- | --- |
| S0 | ~80 throwaway + a fixture | the one with unknown answers |
| S1 | 420 + 260 + 200 golden | the bulk of the engineering |
| S2 | 500 + 140 golden | pure FunnyLang, no network |
| S3 | 400 + 60 golden | the fragile one |
| S4 | 560 + 160 golden | the CLI and the pool |
| S5 | 400 | README, CHANGELOG, this file |
| S6 | 60 | measurement, into the README |

**~3,200 lines**, of which about 600 are the golden — in the range of `kv/` and `battleship/`,
below `chat/`.

---

## 10. Deviations log

### D1 — S0's findings, measured 2026-09-18

Against one real share link, supplied by the repository's owner, who owns the song. Every line
below was produced by a probe script run with the network on, and the raw responses are in the
session's scratchpad. **This is the dated evidence rule 7 demands; everything in §2.2 that
contradicts it is wrong and the sections below supersede it.**

| probe | answer |
| --- | --- |
| `GET https://suno.com/s/<short>` | `200`, 185 KB of HTML, `X-Matched-Path: /song/[slug]` — the short form is rewritten server-side, not redirected |
| `og:audio` / `twitter:player:stream` | **absent.** The page carries `og:title`, `og:description`, `og:image`, `og:type: music.song` and nothing that names the audio |
| the page's embedded JSON | carries `"audio_url":"https://studio-api.prod.suno.com/api/forbidden"` — the audio URL is **deliberately withheld** from a signed-out reader |
| `GET https://studio-api.prod.suno.com/api/clip/<id>` | **`200`, 8.8 KB of JSON, unauthenticated** — title, tags, `created_at`, `display_name`, `handle`, `image_url`, `image_large_url`, `video_url`, `media_urls`, model name, duration |
| `GET https://cdn1.suno.ai/<id>.mp3` | `403` — `<Code>MissingKey</Code>`, "Missing Key-Pair-Id query parameter or cookie value". A browser `User-Agent`, `Referer` and `Origin` change nothing |
| `media_urls` | exactly one entry: `https://d2lwuy8qc234o3.cloudfront.net/1/clip/<id>.m4a`, `"content_type": "m4a-opus"` |
| `GET` that `.m4a` | **`200`, 6,331,654 bytes, `audio/mp4`, `Accept-Ranges: bytes`** |
| `GET https://cdn1.suno.ai/<id>.mp4` | `200`, 15,608,030 bytes, `video/mp4`, `Accept-Ranges: bytes` |
| `GET https://cdn2.suno.ai/image_large_<id>.jpeg` | `200`, 84,932 bytes, `image/jpeg` — **but `HEAD` on the same URL is `403`** |
| `is_public` on a song reached by a share link | `cap`. A share link is an unlisted capability, not a public listing |

### D2 — there is no public MP3, and this changes the deliverable

`.mp3` on the CDN is served from CloudFront behind **signed URLs**, and the signature is only
handed to a signed-in owner. The one audio rendition Suno publishes for a share link is
**Opus in an MP4 container** (`m4a-opus`). Transcoding it to MP3 would need an Opus decoder and an
MP3 encoder written in FunnyLang — tens of thousands of lines of DSP — and shelling out to `ffmpeg`
is not available either: `computer` makes no subprocess calls, by design, and that is not getting
relaxed for one example.

So the honest deliverable changes, and §1's console session with it:

- **Default output is `.m4a`** — the bytes Suno actually serves, unmodified, correctly named and
  correctly tagged. A file this program calls an mp3 while holding Opus frames would be a lie that
  every player would catch.
- **`id3.funny` stays and is still built and still tested.** The moment the resolver is handed a
  URL that really is an mp3 — an authenticated fetch, a future rendition, any other backend — the
  pipeline tags it as ID3v2.3 exactly as §4.4 says. The golden exercises it against a synthetic
  mp3, so it is real, working, tested code rather than a promise.
- **`mp4tags.funny` is new**, and is what tags the default output: the iTunes-style `moov/udta/
  meta/ilst` atoms (`©nam`, `©ART`, `©alb`, `©day`, `©gen`, `©cmt`, `covr`) written into the MP4
  box tree. This is the same bit-twiddling §4.4 wanted from ID3 and arguably better, because the
  box tree has to be rebuilt with corrected parent sizes rather than prepended to.
- **`--format` picks the rendition**: `m4a` (default, the audio), `mp4` (the video Suno also
  publishes), `mp3` (only reachable when something actually serves one — it fails with a sentence
  saying why, and pointing at `--header`).
- **`--header "Name: value"`, repeatable**, is the general escape hatch: an owner who wants their
  own mp3 passes their own session cookie and the same pipeline fetches and ID3-tags it. No
  credential is stored, read from a file, or implemented specially — it is a request header the
  caller supplies, and the README says exactly that.

### D3 — the ladder inverts

§4.2's rungs were ordered on the assumption that the page carries the audio. It does not, so the
order is now:

1. **The URL** — `/s/<short>` and `/song/<uuid>` both yield the clip id (the short form's page is
   fetched once to read the canonical id out of it).
2. **The clip API** — `studio-api.prod.suno.com/api/clip/<id>`, public, and the source of every
   field including the audio URL. This is now the rung that normally answers, and `via` says
   `"api"`.
3. **The page's meta tags** — `og:title` and `og:image` when the API is unreachable, which gets a
   cover and a title but no audio, so it can only complete a `--print-only`.
4. **The pattern** — `cdn1.suno.ai/<id>.mp4` for the video, confirmed with a **`GET` of one byte
   via `Range`, never a `HEAD`**, because D1's last row proves `HEAD` lies on this CDN.

### D5 — the `.m4a` is encrypted, so the default is the video (measured 2026-09-18)

Running the finished program against the same real link, which is the evidence §7's S3 demands:

- `--print-only` resolved it correctly: short link → page → id → clip API → `m4a` and `mp4`
  renditions, `via: "api"`, `id_via: "page"`.
- the `.m4a` downloaded exactly, 6,331,654 bytes, matching its `Content-Length` — **and its first
  bytes are `41 5f ac a9 2f 62 29 59`. There is no `ftyp`, and no box structure at all.** The
  runtime's own C client (`internet.download`) fetches byte-identical bytes, so this is the
  content and not this program: **Suno serves that rendition encrypted.**
- `https://cdn1.suno.ai/<id>.mp4` begins `00 00 00 20 66 74 79 70 69 73 6f 6d` — a plain
  `ftyp isom` MP4, 15,608,030 bytes, and it contains the audio.

So D2's conclusion needs one more turn of the screw. There is no public mp3 **and** the one thing
`media_urls` advertises cannot be played either. The only rendition a share link publishes in the
clear is the video.

- **`--format` now defaults to `mp4`.** It is the only thing that works.
- **The pipeline sniffs what actually arrived** before tagging it, and a rendition whose bytes are
  not the container its kind implies is kept as it arrived, left untagged, and reported with a
  sentence saying it is encrypted and which format does play. Tagging it would otherwise fail with
  "that file is not an MP4: it has no boxes at all", which tells nobody anything.
- `best` now prefers `mp3`, then `mp4`, then `m4a`.

The end-to-end run on the real song: 15,608,030 bytes down, tagged to 15,693,534, title, artist,
year, style, source URL and an 84,932-byte cover read back out by `tag.funny`.

**And the real file vindicated one design choice.** Its layout is `ftyp, free, mdat, moov` — with
`moov` *last*. So `mdat` does not move when the tag grows, the 7,654 chunk offsets correctly stay
exactly where they were (48 → 48, 15,423,186 → 15,423,186), and every one still points at the same
audio bytes. `mp4tags.funny` compares box *positions* rather than assuming the usual
`moov`-before-`mdat` order, which is why it did the right thing on a layout the fixture does not
have. A tagger that had hard-coded "shift the offsets" would have corrupted this file.

### D6 — the CLI was the one thing the golden could not reach, and it was broken

The golden drives `pipeline.funny`, because `grab.funny` ends in `dip(main(...))` and this
language has no subprocesses. The first time `grab.funny` was actually run — against the real link
— it did not parse, twice: a ternary whose `?` and `:` were on their own lines, and a
`lowkey (...) => yap ...` where `yap` is a statement and cannot be an expression. Both were in
code the golden had never executed a line of. The README's "what it is not" already named this
gap; it turns out to have been worth naming.

### D7 — a page, and then the video taken out of the file (2026-09-18)

Two things the owner asked for after S7, neither of which was in this plan.

**S8, the page.** `serve.funny` and `web/`: paste a link, see the song, pick a rendition, watch a
real progress bar, save the file. One thread for the server and an intern for the download — the
one place in this tree that needs both at once, because `fetch.funny` blocks by design and a page
polling four times a second must not queue behind six megabytes. Three of its four bugs were ones
only a running browser could find, which is the argument for the golden covering the HTTP surface
too; it does not yet, and the README says so.

**S9, the remux.** The ask was "convert the mp4 to mp3 server-side". It cannot be done: the video's
audio track is `mp4a` — AAC — so an mp3 needs an AAC decoder and an MP3 encoder, a pair of codecs
and tens of thousands of lines of DSP, and `computer` makes no subprocess calls so ffmpeg is not an
option either. What *is* possible is dropping the video track, which is container work rather than
codec work, and is most of what was actually wanted: 15,608,030 bytes becomes 9,510,361.

`remux.funny` does that. `--format audio` on the CLI, and the page's first and default option. The
samples are copied untouched; what is rebuilt is `stco`, because dropping the video moves every
audio chunk. The golden's part eight is built to catch a remux that copies the wrong bytes rather
than the wrong number of them: the fixture's two tracks interleave, every chunk is stamped, and the
test asserts byte-identical audio, audio-only stamps, and every offset landing on a chunk.

### D4 — two things the client must handle that §4.1 did not know about

- The share page is `Transfer-Encoding: chunked` with `Connection: keep-alive` from Cloudflare, so
  **chunked decoding is on the main path, not an edge case** — it is how the very first request of
  every run comes back.
- `HEAD` is unreliable on `cdn2.suno.ai` (403 where `GET` is 200), so `fetch.head` exists but the
  resolver never depends on it. Size and type come from the `GET` that downloads, or from a
  `Range: bytes=0-0`.
