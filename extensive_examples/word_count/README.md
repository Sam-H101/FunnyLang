# word_count — counting words on several threads

Counts every word in a directory of files, using as many threads as you ask for, and prints the
most common ones.

```console
$ funny run extensive_examples/word_count/count.funny -- ./corpus --workers 8 --top 5
counted 1736000 words in 240 files with 8 workers (pool) in 1.53 s
the  372000
dog  248000
and  124000
quick  62000
brown  62000
```

| | |
|---|---|
| `count.funny` | The command line: flags, timing, printing |
| `counter.funny` | The three ways of splitting the work |
| `worker.funny` | One intern, counting whatever it is sent |
| `words.funny` | What counts as a word, and the counting itself |
| `test_count.funny` | The golden: a generated corpus whose answer is arithmetic |

`funny test extensive_examples` runs the last one on every platform CI builds.

## Running it

```console
$ funny run extensive_examples/word_count/count.funny -- DIR [--workers N] [--top N] [--split WAY]
```

`--workers` defaults to `interns.headcount()`, this machine's logical CPUs. `--top` defaults to 20.
`--split` is `pool` (the default), `static` or `file`.

## Three ways to split the work

This is what the example is actually about. The counting is identical in all three — the same
worker, the same tokenizer — so what the timings compare is the scheduling and nothing else.

- **pool** — hire N workers, send each one file, and send the next file to whichever worker answers
  first. Nobody sits idle while somebody else is still on a big file.
- **static** — hire N workers and deal every file up front, round-robin. The run takes as long as
  the unluckiest worker's share takes.
- **file** — hire one intern per file. Every file gets its own thread and its own heap.

A worker is sent a path with `interns.dm` and sends its counts back the same way; `"stop"` ends it.
The counts cross as an ordinary `groupchat`, deep-copied like any other message, so nothing is
shared between the threads that produce them and the thread that merges them.

## What it measured

An 8-core machine under WSL2. The corpus is 240 files, 8.6 MB, 1,736,000 words — and deliberately
uneven: every twelfth file is twenty times the size of the others, because a corpus of equal files
cannot tell the three strategies apart.

| workers | pool |
|---|---|
| 1 | 5.54 s |
| 2 | 3.21 s |
| 4 | 1.96 s |
| 8 | 1.63 s |

Four workers, one corpus, three strategies:

| | 4 workers |
|---|---|
| pool | 1.96 s |
| one intern per file | 2.00 s |
| static split | 2.12 s |

The pool wins, and by less than the story usually goes. Two hundred and forty files is enough that
a round-robin split averages out most of its bad luck, so the gap is eight per cent rather than the
factor you would see with a dozen files of wildly different sizes. One intern per file lands
between them: it balances perfectly, and pays for 240 threads and 240 heaps to do it.

Eight workers beat four by less than four beat two, which is the ordinary shape of this curve on an
8-core machine where the counting is memory-bound and the main thread has to merge every result.

## What it is not

- **Not a benchmark of FunnyLang against anything.** `wc -w` will beat it, and so will a page of C.
  The numbers here compare three schedulings of the same FunnyLang program on one machine.
- **Not a tokenizer anybody should reuse.** A word is a run of Unicode letters with apostrophes
  kept inside it, lowercased. That is enough for prose and wrong for hyphenated compounds, for
  languages that do not put spaces between words, and for anything where numbers are words.
- **Not incremental.** It reads every file every time; there is no cache and no watch mode.
- **Not streaming.** A file is read whole into memory, so a file larger than memory will not work.
  The corpus can be any size; a single file cannot.
- **No stop-word list, no stemming, no normalization** beyond lowercasing — `run` and `running` are
  two words, and so are `café` and `cafe`.
