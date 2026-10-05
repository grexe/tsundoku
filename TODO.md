# TODO

## To report

### DjVuLibre on Haiku: the key for thread-specific data is never created (`libdjvu/miniexp.cpp`)

Found 2026-10-04 while adding DjVu to Toji. For the recipe `app-text/djvu` (patch `djvu-3.5.29.patchset`) and upstream
(<https://sourceforge.net/projects/djvu/>).

**What happens.** `miniexp.cpp` (the s-expressions that `ddjvu_document_get_pagetext()`, `get_pageanno()`, `get_outline()`,
`get_anno()` return) keeps its per-thread garbage collector state under a pthread key:

```cpp
static pthread_key_t gctls_key;
static pthread_once_t gctls_once;                  // zero-initialised
static void gctls_key_alloc() { pthread_key_create(&gctls_key, gctls_destroy); }
...
static void gctls_alloc() {
  pthread_once(&gctls_once, gctls_key_alloc);
  gctls_tv = new gctls_t();
  pthread_setspecific(gctls_key, (void*)gctls_tv);
}
```

POSIX only guarantees that a `pthread_once_t` initialised with `PTHREAD_ONCE_INIT` works. On Haiku that is `{ -1 }`
(`/system/develop/headers/posix/pthread.h`; `src/system/libroot/posix/pthread/pthread_once.cpp` uses negative states, and reads a
non-negative state as the id of a semaphore that a waiting thread has created). A zero control is therefore taken for "another
thread is initialising, wait on semaphore 0"; `acquire_sem(0)` fails at once and `pthread_once()` returns 0 without calling the
routine: **`gctls_key_alloc()` never runs**
(checked: a program that calls `pthread_once()` on a static `pthread_once_t` and on `PTHREAD_ONCE_INIT` has its routine run 0
and 1 times). `gctls_key` stays 0, so every thread that builds an s-expression does `pthread_setspecific(0, gctls_t*)`: it
overwrites the thread-specific value of **key 0 of the process, which is whoever created the first key** (in Toji: the
ICU locale data of libroot; in a plain program: the program's own first key). At thread exit the destructor of that key's owner
is called with a pointer to a `gctls_t`.

**What it looked like in Toji** (three crashes that seemed to have nothing to do with each other, only with MuPDF loaded):
in `gc_run()` of the garbage collector (called from `miniexp_cons` in `pagetext_sub`), in `wcrtomb()` of libroot
(`ICUCategoryData::_GetConverter`, called from `GStringRep::UCS4toNative` when DjVuLibre opens a file by name in a thread that
draws), and at the end of the thread in `~ICUThreadLocalStorageValue`.

**Reproduction** (no MuPDF needed; any DjVu file):

```cpp
#include <libdjvu/ddjvuapi.h>
#include <libdjvu/miniexp.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
int main(int argc, char** argv)
{
	pthread_key_t mine;
	pthread_key_create(&mine, NULL);                       // the first key of the process: key 0
	pthread_setspecific(mine, (void*)0x1234);
	printf("my key is %d, value before: %p\n", (int)mine, pthread_getspecific(mine));

	ddjvu_context_t* c = ddjvu_context_create("clobber");
	ddjvu_document_t* d = ddjvu_document_create_by_filename(c, argv[1], 0);
	while (!ddjvu_document_decoding_done(d)) {
		ddjvu_message_t* m;
		while ((m = ddjvu_message_peek(c))) ddjvu_message_pop(c);
		usleep(1000);
	}
	miniexp_t t;
	while ((t = ddjvu_document_get_pagetext(d, 0, "word")) == miniexp_dummy) usleep(1000);
	printf("my key's value after DjVuLibre built an s-expression: %p\n", pthread_getspecific(mine));
}
```

`g++ clobber.cpp -ldjvulibre -lpthread`, then `./a.out any.djvu`: the output shows `0x1234` before and another pointer after.

**The fix** (one line, also correct on any system, since a zero `pthread_once_t` is not portable):

```diff
-static pthread_once_t gctls_once;
+static pthread_once_t gctls_once = PTHREAD_ONCE_INIT;
```

The same pattern should be looked for elsewhere in the library (`grep -n "pthread_once_t" libdjvu/*.cpp`).

**Workaround in Toji** (`DjvuDocument.cpp`, `KeyGuard`): the value of key 0 is saved and restored around every call that
makes s-expressions; the collector is switched off; text, links, outline and metadata are asked for once and kept. It can go when
the library is fixed (keep the cache, which saves work anyway).

## DjVu

Nothing is open for DjVu (2026-10-05): the year of the metadata is `dc:date`, the text of turned pages is turned to the page as
shown, and text marks (highlight, underline, strike out) keep the page and the quoted words in `SEN:annotations`.
