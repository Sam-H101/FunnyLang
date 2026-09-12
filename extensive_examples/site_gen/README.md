# site_gen — a static site generator

Reads a directory of Markdown, fills a layout, and writes a site: a page per file, an index of
posts, a page per tag, an Atom feed and a sitemap.

```console
$ funny run extensive_examples/site_gen/build.funny extensive_examples/site_gen/example/site ./out --title "A small site"
built 2 pages, 2 posts, 2 tags in 0.07 s -> ./out
about.html
feed.xml
index.html
posts.html
posts/hello.html
posts/second.html
sitemap.txt
static/dot.png
static/style.css
tags/funnylang.html
tags/intro.html
```

| | |
|---|---|
| `build.funny` | The command line: flags, timing, printing |
| `site.funny` | Front matter, the page model, and writing the site out |
| `markdown.funny` | Markdown to HTML |
| `example/site/` | A small site to build |
| `example/expected/` | What building it should produce, committed |
| `test_build.funny` | A golden: builds the example and compares it, byte for byte |
| `test_markdown.funny` | A golden: the renderer against CommonMark's own examples |

`funny test extensive_examples` runs the last two on every platform CI builds.

## The beginner's example

No threads, no sockets, no interns, no TLS. It reads files, transforms them, and writes files —
which is the smallest thing that is still a real program. If you know Markdown, you can read every
line of this one.

## A site

```
site/
  layout.html      the template, with {{ title }}, {{ site_title }} and {{ content }} holes
  index.md         a page
  about.md         another
  posts/hello.md   a post: it has a date, so it appears in the index, its tags and the feed
  static/          copied through, byte for byte
```

Front matter is a block between two `---` rules at the top of a file: `title`, `date`, `tags` and
`draft`, one per line. A draft appears nowhere — not on the index, not on a tag page, not in the
feed, not as a page of its own.

## Deterministic on purpose

Building the same site twice gives the same bytes, and the golden asserts it. Every list is sorted,
including the file list, the tags and the sitemap; posts are ordered by date and then by slug, so
two posts sharing a date never swap places; and the feed's `updated` is the newest post's own date
rather than the clock. A generated site you cannot diff is one you cannot review, and a feed
timestamped "now" changes every build for no reason.

## What it measured

An 8-core machine under WSL2, though nothing here uses more than one of those cores. Each
figure is the steadiest of three runs, after the first has warmed the file cache.

| | files in | files out | time |
|---|---|---|---|
| `example/site` | 8 | 11 | 0.07 s |
| a generated site | 220 Markdown, 105 KB | 226 | 0.44 s |

The larger site is 200 posts and 20 pages, each with a heading, two paragraphs,
emphasis, strong text, a code span, two links, a bullet list, a block quote and a fenced
code block. That is about 2 ms per page, most of it in the inline scanner, which walks each
paragraph character by character. Every post there carries the same two tags, so the build
writes 226 files rather than one per tag; a site whose posts each had their own tag would
write a page for every one of them.

## What it is not

- **Not CommonMark.** It does headings, paragraphs, emphasis, strong, code spans, fenced code,
  links, images, bullet and numbered lists, block quotes and rules. It does **not** do tables,
  footnotes, nested lists, reference links, setext headings, hard line breaks or loose-list
  paragraph wrapping. A document using those still builds; it just does not render them.
  `test_markdown.funny` runs the spec's own examples through the renderer and commits what came
  back, so the exact list of departures lives in the repository rather than in this paragraph.
- **Not an HTML pass-through.** Raw HTML in a post is escaped, so `<script>` on a page arrives as
  text. That makes this not-a-sanitizer by construction, which is the safe direction for a
  generator to be wrong in.
- **Not incremental, and no watch mode.** Every build reads every file and writes every output.
- **No syntax highlighting.** A fenced block's info string becomes `class="language-funny"` and
  nothing else; colouring it is a stylesheet's job, or a browser's.
- **Front matter is four keys, not YAML.** `title`, `date`, `tags`, `draft`. Anything else in the
  block is kept as a string and ignored rather than half-understood.
- **No pagination, no search, no related posts.** The index lists every post.
