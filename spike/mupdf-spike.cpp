// MuPDF spike for Tsundoku: checks the pieces the migration depends on.
//   spike file.pdf outprefix [needle]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <Application.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <File.h>
#include <OS.h>
#include <TranslatorRoster.h>
#include <TranslationDefs.h>
#include <TranslatorFormats.h>

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

// MuPDF only lets a context be cloned for other threads when it was created with lock callbacks
static pthread_mutex_t sMutex[FZ_LOCK_MAX];
static void LockFn(void*, int i) { pthread_mutex_lock(&sMutex[i]); }
static void UnlockFn(void*, int i) { pthread_mutex_unlock(&sMutex[i]); }
static fz_locks_context sLocks = { NULL, LockFn, UnlockFn };

static bigtime_t Now() { return system_time(); }

// the pixmap is BGRA, the byte order of B_RGB32 / B_RGBA32 on little endian: copy rows, no conversion
static BBitmap* RenderToBitmap(fz_context* ctx, fz_page* page, fz_display_list* list, float zoom)
{
	fz_matrix m = fz_scale(zoom, zoom);
	fz_rect bounds = fz_bound_page(ctx, page);
	fz_irect bbox = fz_round_rect(fz_transform_rect(bounds, m));
	fz_pixmap* pix = NULL;
	fz_device* dev = NULL;
	BBitmap* bitmap = NULL;
	fz_var(pix);
	fz_var(dev);
	fz_try(ctx) {
		pix = fz_new_pixmap_with_bbox(ctx, fz_device_bgr(ctx), bbox, NULL, 1);
		fz_clear_pixmap_with_value(ctx, pix, 0xff);
		dev = fz_new_draw_device(ctx, fz_identity, pix);
		if (list != NULL)
			fz_run_display_list(ctx, list, dev, m, fz_infinite_rect, NULL);
		else
			fz_run_page(ctx, page, dev, m, NULL);
		fz_close_device(ctx, dev);
		int w = fz_pixmap_width(ctx, pix), h = fz_pixmap_height(ctx, pix);
		bitmap = new BBitmap(BRect(0, 0, w - 1, h - 1), B_RGB32);
		uint8* src = fz_pixmap_samples(ctx, pix);
		int stride = fz_pixmap_stride(ctx, pix);
		uint8* dst = (uint8*)bitmap->Bits();
		for (int y = 0; y < h; y++)
			memcpy(dst + y * bitmap->BytesPerRow(), src + y * stride, w * 4);
	}
	fz_always(ctx) {
		fz_drop_device(ctx, dev);
		fz_drop_pixmap(ctx, pix);
	}
	fz_catch(ctx) {
		delete bitmap;
		return NULL;
	}
	return bitmap;
}

static bool SavePNG(BBitmap* bitmap, const char* path)
{
	BBitmapStream stream(bitmap);
	BFile file(path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = BTranslatorRoster::Default()->Translate(&stream, NULL, NULL, &file, B_PNG_FORMAT);
	BBitmap* back = NULL;
	stream.DetachBitmap(&back);	// keep ownership with the caller
	return status == B_OK;
}

static void PrintOutline(fz_context* ctx, fz_document* doc, fz_outline* o, int depth, int* count)
{
	for (; o != NULL; o = o->next) {
		(*count)++;
		if (*count <= 6) {
			int page = fz_page_number_from_location(ctx, doc, o->page);
			printf("    %*s%s -> page %d\n", depth * 2, "", o->title ? o->title : "(null)", page + 1);
		}
		PrintOutline(ctx, doc, o->down, depth + 1, count);
	}
}

struct ThreadArg {
	fz_context* ctx;	// already a clone for this thread
	const char* path;
	int firstPage, step, pages;
	int rendered, failed;
};

static int32 RenderThread(void* data)
{
	ThreadArg* a = (ThreadArg*)data;
	fz_context* ctx = a->ctx;
	fz_document* doc = NULL;
	fz_var(doc);
	fz_try(ctx) {
		doc = fz_open_document(ctx, a->path);
		int n = fz_count_pages(ctx, doc);
		for (int i = a->firstPage; i < n && i < a->pages; i += a->step) {
			fz_page* page = fz_load_page(ctx, doc, i);
			BBitmap* bitmap = RenderToBitmap(ctx, page, NULL, 1.0f);
			fz_drop_page(ctx, page);
			if (bitmap != NULL) {
				a->rendered++;
				delete bitmap;
			} else
				a->failed++;
		}
	}
	fz_always(ctx) {
		fz_drop_document(ctx, doc);
	}
	fz_catch(ctx) {
		a->failed++;
	}
	return 0;
}

int main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: spike file out-prefix [needle]\n");
		return 1;
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	BApplication app("application/x-vnd.sen-labs.mupdf-spike");	// BBitmap needs the app_server connection
	const char* path = argv[1];
	const char* prefix = argv[2];
	const char* needle = argc > 3 ? argv[3] : NULL;

	for (int i = 0; i < FZ_LOCK_MAX; i++)
		pthread_mutex_init(&sMutex[i], NULL);
	fz_context* ctx = fz_new_context(NULL, &sLocks, FZ_STORE_DEFAULT);
	if (ctx == NULL)
		return 1;
	fz_try(ctx)
		fz_register_document_handlers(ctx);
	fz_catch(ctx) {
		fprintf(stderr, "register failed\n");
		return 1;
	}

	fz_document* doc = NULL;
	fz_try(ctx)
		doc = fz_open_document(ctx, path);
	fz_catch(ctx) {
		fprintf(stderr, "open failed: %s\n", fz_caught_message(ctx));
		return 1;
	}

	printf("== document\n");
	printf("  needs password: %d, can copy: %d, can edit: %d, can annotate: %d\n", fz_needs_password(ctx, doc),
		fz_has_permission(ctx, doc, FZ_PERMISSION_COPY), fz_has_permission(ctx, doc, FZ_PERMISSION_EDIT),
		fz_has_permission(ctx, doc, FZ_PERMISSION_ANNOTATE));
	int pages = fz_count_pages(ctx, doc);
	char buf[256];
	if (fz_lookup_metadata(ctx, doc, FZ_META_INFO_TITLE, buf, sizeof(buf)) > 0)
		printf("  title: %s\n", buf);
	printf("  pages: %d, is PDF: %s\n", pages, pdf_specifics(ctx, doc) ? "yes" : "no");

	printf("== render page 1 into a BBitmap, saved as PNG\n");
	fz_page* page = fz_load_page(ctx, doc, 0);
	fz_rect bounds = fz_bound_page(ctx, page);
	printf("  page size: %.0f x %.0f pt, label: '%s'\n", bounds.x1 - bounds.x0, bounds.y1 - bounds.y0,
		fz_page_label(ctx, page, buf, sizeof(buf)));
	BBitmap* bitmap = RenderToBitmap(ctx, page, NULL, 1.0f);
	if (bitmap != NULL) {
		char name[512];
		snprintf(name, sizeof(name), "%s-page1.png", prefix);
		printf("  bitmap %.0fx%.0f, saved: %s\n", bitmap->Bounds().Width() + 1, bitmap->Bounds().Height() + 1,
			SavePNG(bitmap, name) ? name : "FAILED");
		delete bitmap;
	}

	printf("== timing: render each time vs display list (ms)\n");
	float zooms[] = { 0.5f, 1.0f, 1.5f, 2.0f };
	for (int i = 0; i < 4; i++) {
		bigtime_t t = Now();
		BBitmap* b = RenderToBitmap(ctx, page, NULL, zooms[i]);
		bigtime_t direct = Now() - t;
		delete b;
		fz_display_list* list = fz_new_display_list_from_page(ctx, page);
		t = Now();
		b = RenderToBitmap(ctx, page, list, zooms[i]);
		bigtime_t viaList = Now() - t;
		delete b;
		fz_drop_display_list(ctx, list);
		printf("  zoom %.1f: direct %.1f, display list %.1f\n", zooms[i], direct / 1000.0, viaList / 1000.0);
	}

	printf("== outline\n");
	fz_outline* outline = NULL;
	fz_try(ctx)
		outline = fz_load_outline(ctx, doc);
	fz_catch(ctx)
		printf("  none (%s)\n", fz_caught_message(ctx));
	if (outline != NULL) {
		int count = 0;
		PrintOutline(ctx, doc, outline, 0, &count);
		printf("  %d entries\n", count);
		fz_drop_outline(ctx, outline);
	}

	printf("== links on page 1\n");
	fz_link* links = fz_load_links(ctx, page);
	int linkCount = 0;
	for (fz_link* l = links; l != NULL; l = l->next) {
		if (linkCount++ < 3) {
			float x, y;
			fz_location loc = fz_resolve_link(ctx, doc, l->uri, &x, &y);
			printf("  %s -> page %d\n", l->uri, fz_page_number_from_location(ctx, doc, loc) + 1);
		}
	}
	printf("  %d links\n", linkCount);
	fz_drop_link(ctx, links);

	printf("== text\n");
	fz_stext_page* text = fz_new_stext_page_from_page(ctx, page, NULL);
	fz_rect top = bounds;
	top.y1 = bounds.y1;
	char* copied = fz_copy_rectangle(ctx, text, top, 0);
	printf("  copy of the whole page (%d chars): %.100s\n", (int)strlen(copied), copied);
	fz_free(ctx, copied);
	if (needle != NULL) {
		fz_quad hits[64];
		bigtime_t t = Now();
		int n = 0;
		int found = 0;
		for (int i = 0; i < pages; i++) {
			int hit = fz_search_page_number(ctx, doc, i, needle, NULL, hits, 64);
			if (hit > 0 && found++ < 3)
				printf("  '%s' on page %d: %d hits, first at %.0f,%.0f\n", needle, i + 1, hit, hits[0].ul.x,
					hits[0].ul.y);
			n += hit;
		}
		printf("  search over %d pages: %d hits in %.0f ms\n", pages, n, (Now() - t) / 1000.0);
	}
	fz_drop_stext_page(ctx, text);
	fz_drop_page(ctx, page);

	printf("== two threads, own context and document each\n");
	int use = pages < 30 ? pages : 30;
	ThreadArg a[2];
	thread_id ids[2];
	bigtime_t t = Now();
	for (int i = 0; i < 2; i++) {
		a[i].ctx = fz_clone_context(ctx);
		a[i].path = path;
		a[i].firstPage = i;
		a[i].step = 2;
		a[i].pages = use;
		a[i].rendered = a[i].failed = 0;
		ids[i] = spawn_thread(RenderThread, "render", B_NORMAL_PRIORITY, &a[i]);
		resume_thread(ids[i]);
	}
	for (int i = 0; i < 2; i++) {
		status_t s;
		wait_for_thread(ids[i], &s);
	}
	printf("  rendered %d + %d pages (failed %d + %d) in %.0f ms\n", a[0].rendered, a[1].rendered, a[0].failed,
		a[1].failed, (Now() - t) / 1000.0);
	for (int i = 0; i < 2; i++)
		fz_drop_context(a[i].ctx);

	fz_drop_document(ctx, doc);
	fz_drop_context(ctx);
	return 0;
}
