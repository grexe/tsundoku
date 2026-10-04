/*
 * BePDF: The PDF reader for Haiku.
 * 	 Copyright (C) 1997 Benoit Triquet.
 * 	 Copyright (C) 1998-2000 Hubert Figuiere.
 * 	 Copyright (C) 2000-2011 Michael Pfeiffer.
 * 	 Copyright (C) 2013 waddlesplash.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <ctype.h>

#include <locale/Catalog.h>
#include <be/app/Application.h>
#include <be/storage/Entry.h>
#include <be/storage/FilePanel.h>
#include <be/app/Roster.h>
#include <be/interface/Screen.h>
#include <be/StorageKit.h>
#include <Deskbar.h>
#include <be/interface/Alert.h>
#include <Bitmap.h>
#include <ControlLook.h>
#include <IconUtils.h>
#include <Resources.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <fs_index.h>
#include <Mime.h>
#include <MimeType.h>
#include <TextView.h>
#include <View.h>
#include <Window.h>

#include "PDFWindow.h"
#include "Application.h"
#include "ResourceLoader.h"
#include "PasswordWindow.h"
#include "Globals.h"
#include "TraceWindow.h"
#include "Document.h"
#include "EpubInfo.h"
#include "FileInfoWindow.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BepdfApplication"

static const char * tsundokuCopyright =
	"© 2026 Gregor B. Rosenauer & Claude\n";

// history of BePDF, newest first
static const char * bePDFCopyright =
    "© 2013-2017 waddlesplash\n"
    "© 2000-2011 Michael Pfeiffer\n"
	"© 1998-2000 Hubert Figuiere\n"
	"© 1997 Benoit Triquet\n";

static const char * licenseCopyright =
    "\n\n"
    "This program is free software under the GNU AGPL v3, or any later version.\n";

static const char *PAGE_NUM_MSG_KEY = "bepdf:page_num";
// a passage to show, found by its text; with SEN:highlight it is also marked in the document (not saved)
static const char *QUOTE_MSG_KEY = "SEN:quote";
static const char *HIGHLIGHT_MSG_KEY = "SEN:highlight";
// the id (the /NM of the PDF) of an annotation to show: the document goes to its page and selects it
static const char *ANNOTATION_MSG_KEY = "SEN:annotation";

static const char *settingsFilename = "Tsundoku";

// Implementation of PDFFilter
class PDFFilter : public BRefFilter {
	static const char *valid_filetypes[];

public:
	bool Filter(const entry_ref *ref, BNode *node, struct stat_beos *st, const char *filetype);
};

static PDFFilter pdfFilter;

BRefFilter* GetPdfFilter() {
	return &pdfFilter;
}

const char * PDFFilter::valid_filetypes[] = {
	"application/x-vnd.Be-directory",
	"application/x-vnd.Be-symlink",
	"application/x-vnd.Be-volume",
	"application/pdf",
	"application/x-pdf",
	NULL
};

bool PDFFilter::Filter(const entry_ref *ref, BNode *node, struct stat_beos *st, const char *filetype) {
	for (int i = 0; valid_filetypes[i]; i++) {
		if (strcmp(filetype, valid_filetypes[i]) == 0) return true;
		// check file extension if filetype has not been set to application/pdf
		BString name(ref->name);
		name.ToUpper();
		int32 l = name.FindLast('.');
		if (l != B_ERROR) {
			if (name.FindFirst(".PDF", l) != B_ERROR) return true;
		}
	}
	return false;
}
///////////////////////////////////////////////////////////
int main()
{
	new BepdfApplication ( );

	be_app->Run();

	delete be_app;
	return 0;
}



// What a book says about itself, as BFS attributes. Nothing is invented for books: the names are the properties of
// schema.org (Book, CreativeWork) with the prefix META: that Haiku's own attributes for documents have, and where
// they exist the attributes of application/pdf are used as they are (META:title, META:author, META:keyw,
// META:subject, META:creator, META:pages), so that the same attribute means the same for every kind of document.
// The comments give the equivalent of Dublin Core (dc:) and schema.org (schema:).
struct BookAttribute {
	const char* name;
	const char* label;
	int32       type;
	int32       width;
};

static const BookAttribute kBookAttributes[] = {
	{ "META:description", B_TRANSLATE_MARK("Description"), B_STRING_TYPE, 200 },	// dc:description, schema:description
	{ "META:publisher", B_TRANSLATE_MARK("Publisher"), B_STRING_TYPE, 150 },		// dc:publisher, schema:publisher
	{ "META:inLanguage", B_TRANSLATE_MARK("Language"), B_STRING_TYPE, 60 },		// dc:language, schema:inLanguage
	{ "META:datePublished", B_TRANSLATE_MARK("Published"), B_TIME_TYPE, 100 },	// dc:date, schema:datePublished
	{ "META:identifier", B_TRANSLATE_MARK("Identifier"), B_STRING_TYPE, 200 },	// dc:identifier, schema:identifier
	{ "META:isbn", B_TRANSLATE_MARK("ISBN"), B_STRING_TYPE, 110 },				// schema:isbn
	{ "META:isPartOf", B_TRANSLATE_MARK("Series"), B_STRING_TYPE, 150 },			// dcterms:isPartOf, schema:isPartOf
	{ "META:position", B_TRANSLATE_MARK("Series number"), B_DOUBLE_TYPE, 60 }		// schema:position
};
static const size_t kBookAttributeCount = sizeof(kBookAttributes) / sizeof(kBookAttributes[0]);

// whether the document has annotations (any kind of document); the annotations of a book are in META:annotations
static const char* const kAnnotatedAttribute = "META:annotated";


static void
AddAttrInfo(BMessage* info, const char* name, const char* label, int32 type, int32 width)
{
	info->AddString("attr:name", name);
	info->AddString("attr:public_name", B_TRANSLATE_NOCOLLECT(label));
	info->AddInt32("attr:type", type);
	info->AddInt32("attr:width", width);
	info->AddInt32("attr:alignment", B_ALIGN_LEFT);
	info->AddBool("attr:viewable", true);
	info->AddBool("attr:editable", false);
	info->AddBool("attr:extra", false);
}


// Haiku does not know EPUB files (one that is not compressed would be taken for a web page), so the type is made
// known, by its extension and by the name of the first file of the container. Which application opens them is
// left to the user.
static void
InstallMimeTypes(const entry_ref* application)
{
	BMimeType epub("application/epub+zip");
	if (epub.InitCheck() != B_OK)
		return;
	if (!epub.IsInstalled()) {
		if (epub.Install() != B_OK)
			return;
		epub.SetShortDescription(B_TRANSLATE("EPUB e-book"));
		epub.SetLongDescription(B_TRANSLATE("Electronic publication (EPUB)"));
		BMessage extensions;
		extensions.AddString("extensions", "epub");
		epub.SetFileExtensions(&extensions);
	}
	// The attributes of a book are those that Tracker, queries and the file info know from PDF files (title, author,
	// subject, creator, keywords), under the same names, so that a column or a query works for both; what only a book
	// has is added in the same way.
	BMimeType pdf("application/pdf");
	BMessage pdfInfo, epubInfo;
	if (pdf.GetAttrInfo(&pdfInfo) == B_OK) {
		static const char* const shared[] = { "META:title", "META:author", "META:subject", "META:creator",
			"META:keyw", "META:pages", NULL };
		const char* name;
		for (int32 i = 0; pdfInfo.FindString("attr:name", i, &name) == B_OK; i++) {
			bool wanted = false;
			for (int k = 0; shared[k] != NULL; k++) {
				if (strcmp(name, shared[k]) == 0)
					wanted = true;
			}
			if (!wanted)
				continue;
			const char* publicName = name;
			int32 type = B_STRING_TYPE, width = 150, alignment = B_ALIGN_LEFT;
			bool viewable = true, editable = false, extra = false;
			pdfInfo.FindString("attr:public_name", i, &publicName);
			pdfInfo.FindInt32("attr:type", i, &type);
			pdfInfo.FindInt32("attr:width", i, &width);
			pdfInfo.FindInt32("attr:alignment", i, &alignment);
			pdfInfo.FindBool("attr:viewable", i, &viewable);
			pdfInfo.FindBool("attr:editable", i, &editable);
			pdfInfo.FindBool("attr:extra", i, &extra);
			epubInfo.AddString("attr:name", name);
			epubInfo.AddString("attr:public_name", publicName);
			epubInfo.AddInt32("attr:type", type);
			epubInfo.AddInt32("attr:width", width);
			epubInfo.AddInt32("attr:alignment", alignment);
			epubInfo.AddBool("attr:viewable", viewable);
			epubInfo.AddBool("attr:editable", editable);
			epubInfo.AddBool("attr:extra", extra);
		}
	}
	for (size_t i = 0; i < kBookAttributeCount; i++)
		AddAttrInfo(&epubInfo, kBookAttributes[i].name, kBookAttributes[i].label, kBookAttributes[i].type,
			kBookAttributes[i].width);
	AddAttrInfo(&epubInfo, kAnnotatedAttribute, B_TRANSLATE_MARK("Annotated"), B_INT32_TYPE, 60);
	epub.SetAttrInfo(&epubInfo);

	// "annotated" is the same attribute for PDF files
	{
		bool known = false;
		const char* name;
		for (int32 i = 0; pdfInfo.FindString("attr:name", i, &name) == B_OK; i++) {
			if (strcmp(name, kAnnotatedAttribute) == 0)
				known = true;
		}
		if (!known && pdfInfo.HasString("attr:name")) {
			pdfInfo.AddString("attr:name", kAnnotatedAttribute);
			pdfInfo.AddString("attr:public_name", B_TRANSLATE("Annotated"));
			pdfInfo.AddInt32("attr:type", B_INT32_TYPE);
			pdfInfo.AddBool("attr:viewable", true);
			pdfInfo.AddBool("attr:editable", false);
			pdfInfo.AddInt32("attr:width", 60);
			pdf.SetAttrInfo(&pdfInfo);
		}
	}

	BString rule;
	if (epub.GetSnifferRule(&rule) != B_OK || rule.Length() == 0)
		epub.SetSnifferRule("1.0 [30] ('mimetypeapplication/epub+zip')");

	// The database knows what an application supports from the entry of its signature, which is only made
	// when the application is entered (mimeset -a). Nobody does that for an application that comes in a package
	// or is built, so the entry is out of date after a new type has been added: Tsundoku would not be offered for
	// EPUB files (Open with...). It is entered here, if it is not a supporting application of a type it names.
	BMessage apps;
	bool listed = false;
	if (application != NULL && epub.GetSupportingApps(&apps) == B_OK) {
		const char* signature;
		for (int32 i = 0; apps.FindString("applications", i, &signature) == B_OK; i++) {
			if (strcasecmp(signature, BEPDF_APP_SIG) == 0)
				listed = true;
		}
		if (!listed) {
			BPath path(application);
			if (path.InitCheck() == B_OK)
				create_app_meta_mime(path.Path(), false, true, true);
		}
	}
}


///////////////////////////////////////////////////////////
BepdfApplication::BepdfApplication()
		: BApplication ( BEPDF_APP_SIG )
{
	mSettings = new GlobalSettings();
	mOpenFilePanel            = NULL;
	mSaveFilePanel            = NULL;
	mSaveToDirectoryFilePanel = NULL;
	mInitialized  = false;
	mGotSomething = false;
	mReadyToQuit  = false;
	mWindow = NULL;
	mAppRef = entry_ref();

	mStdoutTracer = NULL;
	mStderrTracer = NULL;
	pointerCursor = new BCursor(B_CURSOR_ID_SYSTEM_DEFAULT);
	linkCursor = new BCursor(B_CURSOR_ID_CREATE_LINK);
	handCursor = new BCursor(B_CURSOR_ID_GRAB);
	grabCursor = new BCursor(B_CURSOR_ID_GRABBING);
	textSelectionCursor = new BCursor(B_CURSOR_ID_I_BEAM);
	zoomCursor = new BCursor(B_CURSOR_ID_ZOOM_IN);
	splitVCursor = new BCursor(B_CURSOR_ID_RESIZE_NORTH_SOUTH);
	resizeCursor = new BCursor(B_CURSOR_ID_RESIZE_NORTH_WEST_SOUTH_EAST);

	BEntry entry; app_info info;
	if (B_OK == be_app->GetAppInfo(&info)) {
		mTeamID = info.team;
		mAppRef = info.ref;
		entry = BEntry(&info.ref);
		entry.GetPath(&mAppPath);
		mAppPath.GetParent(&mAppPath);
	} else {
		mAppPath.SetTo(".");
	}

	mDefaultPDF = mAppPath;
	mDefaultPDF.Append("docs/Start.pdf");

	BPath path(mAppPath);
	LoadSettings();
	InstallMimeTypes(mAppRef.device >= 0 ? &mAppRef : NULL);

	InitBePDF();
}

void
BepdfApplication::Initialize()
{
	mInitialized = true;
}

///////////////////////////////////////////////////////////
BepdfApplication::~BepdfApplication()
{
	SaveSettings();

	delete mSettings; mSettings = NULL;

	delete linkCursor;          linkCursor = NULL;
	delete handCursor;          handCursor = NULL;
	delete grabCursor;          grabCursor = NULL;
	delete textSelectionCursor; textSelectionCursor = NULL;
	delete zoomCursor;          zoomCursor = NULL;
	delete splitVCursor;        splitVCursor = NULL;
	delete resizeCursor;        resizeCursor = NULL;

	ExitBePDF();
}


///////////////////////////////////////////////////////////
void BepdfApplication::ReadyToRun()
{
#if 1
	mStdoutTracer = new OutputTracer(1, "stdout", GetSettings());
	mStderrTracer = new OutputTracer(2, "stderr", GetSettings());
#else
	mStdoutTracer = mStderrTracer = NULL;
#endif

	Initialize();
	if (! mGotSomething) {
		// open start document
		entry_ref defaultDocument;
		BMessage msg(B_REFS_RECEIVED);
		get_ref_for_path (mDefaultPDF.Path(), &defaultDocument);
		msg.AddRef ("refs", &defaultDocument);
		RefsReceived (&msg);

		if (!mGotSomething) {
			// on error open file open dialog
			OpenFilePanel();
		}
	}
}

///////////////////////////////////////////////////////////
// grey stripe with the app icon centered on its right border, with its center
// at a third of the height; the view reserves the room for the half of the icon sticking out
class AboutStripeView : public BView {
public:
	AboutStripeView(BBitmap *icon)
		: BView("stripe", B_WILL_DRAW), mIcon(icon)
	{
		float spacing = be_control_look->DefaultLabelSpacing();
		float half = (mIcon ? (mIcon->Bounds().Width() + 1) / 2 : 0);
		mStripeWidth = floorf(half + 2 * spacing);
		float width = mStripeWidth + half;
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetExplicitMinSize(BSize(width, B_SIZE_UNSET));
		SetExplicitMaxSize(BSize(width, B_SIZE_UNSET));
	}

	~AboutStripeView() { delete mIcon; }

	void Draw(BRect updateRect)
	{
		BRect stripe = Bounds();
		stripe.right = mStripeWidth - 1;
		SetHighColor(tint_color(ViewColor(), B_DARKEN_1_TINT));
		FillRect(stripe);
		if (mIcon == NULL)
			return;
		BRect b = Bounds(), i = mIcon->Bounds();
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		DrawBitmap(mIcon, BPoint(floorf(mStripeWidth - (i.Width() + 1) / 2),
			floorf(b.Height() / 3 - (i.Height() + 1) / 2)));
	}

private:
	BBitmap *mIcon;
	float mStripeWidth;
};

static BMessenger sAboutWindow;
static const char *kTitleFamily = "Noto Emoji";
static const char *kTitleStyle = "Bold";

void BepdfApplication::AboutRequested()
{
	// only one About window at a time
	BLooper *looper = NULL;
	if (sAboutWindow.IsValid() && sAboutWindow.Target(&looper) != NULL && looper != NULL
		&& looper->Lock()) {
		BWindow *open = dynamic_cast<BWindow*>(looper);
		if (open != NULL)
			open->Activate();
		looper->Unlock();
		return;
	}

	BString version;
	BString str("Tsundoku\n\n");
	str += B_TRANSLATE("a universal document reader based on BePDF, extended for SEN");
	str += "\n";
	str += B_TRANSLATE("Version");
	str += " ";
	str += GetVersion(version);
	str += "\n";
	str += tsundokuCopyright;
	str += "\n";

	str += bePDFCopyright;
	str += "\n";

	str += BString().SetToFormat(B_TRANSLATE_COMMENT("Tsundoku renders with MuPDF %s, %s.", "MuPDF version, copyright"),
		FZ_VERSION, "© Artifex Software, Inc.");

	str += licenseCopyright;

	float spacing = be_control_look->DefaultLabelSpacing();
	float textWidth = be_plain_font->StringWidth("M") * 42;

	BTextView *v = new BTextView(BRect(0, 0, textWidth, 100), "text", BRect(0, 0, textWidth, 100),
		B_FOLLOW_NONE, B_WILL_DRAW);
	v->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	v->SetWordWrap(true);
	v->MakeEditable(false);
	v->MakeSelectable(false);
	v->SetStylable(true);
	v->SetText(str.String());

	rgb_color red = {255, 0, 51, 255};
	rgb_color blue = {0, 102, 255, 255};
	char *text = (char*)v->Text();
	char *s = text;
	// set all Be in BePDF in blue and red
	while ((s = strstr(s, "BePDF")) != NULL) {
		int32 i = s - text;
		v->SetFontAndColor(i, i+1, NULL, 0, &blue);
		v->SetFontAndColor(i+1, i+2, NULL, 0, &red);
		s += 2;
	}
	// first text line: the name, large and bold, set slightly apart from the rest
	s = strchr(text, '\n');
	int32 titleEnd = s - text + 1;
	BFont titleFont(be_plain_font);
	titleFont.SetFamilyAndStyle(kTitleFamily, kTitleStyle);
	titleFont.SetSize(be_plain_font->Size() * 2);
	v->SetFontAndColor(0, titleEnd, &titleFont, B_FONT_ALL);
	// the empty line after it is just a small gap
	BFont gapFont(be_plain_font);
	gapFont.SetSize(be_plain_font->Size() * 0.6);
	v->SetFontAndColor(titleEnd, titleEnd + 1, &gapFont, B_FONT_ALL);

	float textHeight = v->TextHeight(0, v->CountLines() - 1);
	v->SetExplicitMinSize(BSize(textWidth, textHeight));
	v->SetExplicitMaxSize(BSize(textWidth, textHeight));

	// app icon for the stripe, 96px, so the full-detail variant of the icon
	BBitmap *icon = NULL;
	BResources *resources = BApplication::AppResources();
	size_t iconSize = 0;
	const void *iconData = resources ? resources->LoadResource(B_VECTOR_ICON_TYPE, "BEOS:ICON", &iconSize) : NULL;
	if (iconData != NULL) {
		BSize size = BControlLook::ComposeIconSize(96);
		icon = new BBitmap(BRect(0, 0, size.width - 1, size.height - 1), B_RGBA32);
		if (BIconUtils::GetVectorIcon((const uint8 *)iconData, iconSize, icon) != B_OK) {
			delete icon;
			icon = NULL;
		}
	}

	BWindow *about = new BWindow(BRect(0, 0, 100, 100), B_TRANSLATE("About Tsundoku"),
		B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE);
	BButton *ok = new BButton("ok", B_TRANSLATE("OK"), new BMessage(B_QUIT_REQUESTED));
	ok->MakeDefault(true);

	BLayoutBuilder::Group<>(about, B_HORIZONTAL, 0)
		.Add(new AboutStripeView(icon))
		.AddGroup(B_VERTICAL, spacing)
			.SetInsets(spacing * 2)
			.Add(v)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(ok)
			.End()
		.End();

	sAboutWindow = BMessenger(about);
	about->CenterOnScreen();
	about->Show();
}

/*
	open a file panel and ask for a PDF file
	the file panel will tell by itself if openning have been cancelled
	or not.
*/
void BepdfApplication::OpenFilePanel ()
{
	if (mOpenFilePanel == NULL) {
		mOpenFilePanel = new BFilePanel (B_OPEN_PANEL,
						NULL, NULL, B_FILE_NODE, true, NULL, NULL);
		mOpenFilePanel->SetRefFilter(&pdfFilter);
	}
	mOpenFilePanel->SetPanelDirectory(mSettings->GetPanelDirectory());
	mReadyToQuit = true;
	mOpenFilePanel->Show();
}

/*
	open a file panel and ask for a PDF file
	the file panel will tell by itself if openning have been cancelled
	or not.
*/
void BepdfApplication::OpenSaveFilePanel(BHandler* handler, bool fileMode, BRefFilter* filter, BMessage* msg, const char* name) {
	BFilePanel* panel = NULL;

	// lazy construct file panel
	if (fileMode) {
		// file panel for selection of file
		if (mSaveFilePanel == NULL) {
			mSaveFilePanel = new BFilePanel (B_SAVE_PANEL,
							NULL, NULL, B_FILE_NODE, false, NULL, NULL, true);
		}

		// hide other file panel
		if (mSaveToDirectoryFilePanel != NULL && mSaveToDirectoryFilePanel->IsShowing()) {
			mSaveToDirectoryFilePanel->Hide();
		}

		panel = mSaveFilePanel;
	} else {
		// file panel for selection of directory
		if (mSaveToDirectoryFilePanel == NULL) {
			mSaveToDirectoryFilePanel = new BFilePanel (B_OPEN_PANEL,
							NULL, NULL, B_DIRECTORY_NODE, false, NULL, NULL, true);
		}

		// hide other file panel
		if (mSaveFilePanel != NULL && mSaveFilePanel->IsShowing()) {
			mSaveFilePanel->Hide();
		}

		panel = mSaveToDirectoryFilePanel;
	}

	// (re)-set to directory of currently opened PDF file
	// TODO decide if directory should be independent from PDF file
	panel->SetPanelDirectory(mSettings->GetPanelDirectory());

	if (name != NULL) {
		panel->SetSaveText(name);
	}
	else if (fileMode) {
		panel->SetSaveText("");
	}

	// set/reset filter
	panel->SetRefFilter(filter);

	// add kind to message
	BMessage message(B_SAVE_REQUESTED);
	if (msg == NULL) {
		msg = &message;
	}
	panel->SetMessage(msg);

	// set target
	BMessenger msgr(handler);
	panel->SetTarget(msgr);

	panel->Refresh();

	panel->Show();
}

void BepdfApplication::OpenSaveFilePanel(BHandler* handler, BRefFilter* filter, BMessage* msg, const char* name) {
	OpenSaveFilePanel(handler, true, filter, msg, name);
}

void BepdfApplication::OpenSaveToDirectoryFilePanel(BHandler* handler, BRefFilter* filter, BMessage* msg, const char* name) {
	OpenSaveFilePanel(handler, false, filter, msg, name);
}


/*
  NOTIFY_QUIT_MSG:
  Or to quit all BePDF applications.
*/
void BepdfApplication::Notify(uint32 cmd) {
	BList list;
	be_roster->GetAppList(BEPDF_APP_SIG, &list);
	const int n = list.CountItems()-1;
	BMessage msg(cmd);
	// notify all but this team
	for (int i = n; i >= 0; i --) {
		team_id who = (team_id)(addr_t)list.ItemAt(i);
		if (who == mTeamID) continue; // skip own team
		status_t status;
		BMessenger app(BEPDF_APP_SIG, who, &status);
		if (status == B_OK) {
			app.SendMessage(&msg, (BHandler*)NULL, 0);
		}
	}
	// notify ourself
	PostMessage(&msg, (BHandler*)NULL, 0);
}

bool BepdfApplication::QuitRequested() {
	delete mStdoutTracer; mStdoutTracer = NULL;
	delete mStderrTracer; mStderrTracer = NULL;

	bool shortcut;
	if (B_OK == CurrentMessage()->FindBool("shortcut", &shortcut) && shortcut) {
		Notify(NOTIFY_QUIT_MSG);
	}
	return BApplication::QuitRequested();
}

///////////////////////////////////////////////////////////
/*
	Opens everything.
*/
void BepdfApplication::RefsReceived(BMessage *msg)
{
	uint32 type;
	int32 count;
	mReadyToQuit = false;
    status_t result;

	msg->GetInfo("refs", &type, &count);

	if (type != B_REF_TYPE) {
        BAlert *error = new BAlert(B_TRANSLATE("Error"), B_TRANSLATE("Invalid file reference received!"), B_TRANSLATE("Close"), NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
        error->Go();

        return;
    }

	BString ownerPassword, userPassword;
	const char *owner = NULL;
	const char *user  = NULL;
    int32 pageNum = 0;
	BString quote;
	BString annotationId;
	bool highlight = false;
	entry_ref ref;

	if (B_OK == msg->FindString("ownerPassword", &ownerPassword)) {
		owner = ownerPassword.String();
	}
	if (B_OK == msg->FindString("userPassword", &userPassword)) {
		user = userPassword.String();
	}
    result = msg->FindInt32(PAGE_NUM_MSG_KEY, &pageNum);
    if (result != B_OK) {
        if (result != B_NAME_NOT_FOUND) {
            BAlert *error = new BAlert(B_TRANSLATE("Error"), B_TRANSLATE("Error getting page number!"), B_TRANSLATE("Close"), NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
            error->Go();
        }
	}

	msg->FindString(QUOTE_MSG_KEY, &quote);
	msg->FindString(ANNOTATION_MSG_KEY, &annotationId);
	msg->FindBool(HIGHLIGHT_MSG_KEY, &highlight);

	Initialize();

	for (int32 i = --count ; i >= 0; i-- ) {
		if ( msg->FindRef("refs", i, &ref ) == B_OK ) {
			/*
				Open the document...
				WARNING: The application thread is used to open a file!
			*/
			PDFWindow *win;
			BRect rect(mSettings->GetWindowRect());
			bool ok;
			bool encrypted = false;

			if (mWindow == NULL) {
				win = new PDFWindow(&ref, rect, owner, user, &encrypted);
				ok = win->IsOk();
			} else {
				win = mWindow;
				win->Lock();
				ok = mWindow->LoadFile(&ref, owner, user, &encrypted);
				win->Unlock();
			}

			if (!ok) {
				if (!encrypted) {
			 		BAlert *error = new BAlert(B_TRANSLATE("Error"), B_TRANSLATE("Tsundoku: Error opening file!"), B_TRANSLATE("Close"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
			 		error->Go();

                    if (mWindow == NULL) {  // fixme: always true even if a PDF window is already open!
                        OpenFilePanel();
                    }
		 		} else {
		 			new PasswordWindow(&ref, rect, this);
                }
		 		if (mWindow == NULL) delete win;

			} else if (mWindow == NULL) {
				mWindow = win;
				win->Show();
			}
            // jump to page if provided
            if (pageNum != 0) {
                BMessage goToPageMsg(PDFWindow::GOTO_PAGE_CMD);
                goToPageMsg.AddInt32("page", pageNum);
                mWindow->MessageReceived(&goToPageMsg);
            }
            if (annotationId.Length() > 0 && mWindow != NULL) {
                BMessage annotationMsg(PDFWindow::SHOW_ANNOTATION_CMD);
                annotationMsg.AddString("id", annotationId);
                mWindow->PostMessage(&annotationMsg);
            }
            if (quote.Length() > 0 && mWindow != NULL) {
                BMessage quoteMsg(PDFWindow::SHOW_QUOTE_CMD);
                quoteMsg.AddString("quote", quote);
                quoteMsg.AddInt32("page", pageNum);
                quoteMsg.AddBool("annotate", highlight);
                mWindow->PostMessage(&quoteMsg);
            }
			// stop after first document
			mGotSomething = true;
			break;
		}
	}
}



///////////////////////////////////////////////////////////
void
BepdfApplication::MessageReceived (BMessage * msg)
{
	if (msg == NULL) {
		fprintf (stderr, "xpdf: message NULL received\n");
		return;
	}

	switch (msg->what) {
	case NOTIFY_QUIT_MSG:
		if (mWindow) {
			BWindow* w = mWindow;
			w->Lock();
			w->PostMessage(B_QUIT_REQUESTED);
			w->Unlock();
		}
		break;
	case NOTIFY_CLOSE_MSG:
		if (mWindow) {
			mWindow->Lock();
			mWindow->UpdateWindowsMenu();
			mWindow->Unlock();
		}
		break;
	case B_CANCEL:
		if (!mWindow && mReadyToQuit) {
			PostMessage(B_QUIT_REQUESTED);
		}
		break;
	default:
		BApplication::MessageReceived(msg);
	}
}





///////////////////////////////////////////////////////////
void
BepdfApplication::ArgvReceived (int32 argc, char **argv)
{
	int pg;
	entry_ref fileToOpen;

	// check command line
	if (!(argc == 2 || argc == 3) || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		fprintf(stderr, "usage: %s [<file> [<page>]]\n", argv[0]);
		exit(1);
	}
	if (argc == 3) {
		pg = atoi(argv[2]);
	} else {
		pg = 1;
	}

	BMessage msg(B_REFS_RECEIVED);
	msg.AddInt32 (PAGE_NUM_MSG_KEY, pg);
	get_ref_for_path (argv[1], &fileToOpen);
	msg.AddRef ("refs", &fileToOpen);
	PostMessage (&msg);
	mGotSomething = true;
}


///////////////////////////////////////////////////////////
void BepdfApplication::LoadSettings() {
BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK &&
		path.Append(settingsFilename) == B_OK ) {
		mSettings->Load(path.Path());
	}
}

///////////////////////////////////////////////////////////
void BepdfApplication::SaveSettings() {
BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK &&
		path.Append(settingsFilename) == B_OK) {
		mSettings->Save(path.Path());
	}
}

static struct {
	const char *name;
	const char *public_name;
	const char *pdf_name;
	int32 type_code;
} gAttrInfo[] = {
	{"META:subject",    "Subject",     "Subject",      B_STRING_TYPE},
	{"META:title",      "Title",       "Title",        B_STRING_TYPE},
	{"META:creator",    "Creator",     "Creator",      B_STRING_TYPE},
	{"META:author",     "Author",      "Author",       B_STRING_TYPE},
	{"META:keyw",    	"Keywords",    "Keywords",     B_STRING_TYPE},
	{"PDF:producer",    "Producer",    "Producer",     B_STRING_TYPE},
	{"PDF:created",     "Created",     "CreationDate", B_TIME_TYPE},
	{"PDF:modified",    "Modified",    "ModDate",      B_TIME_TYPE},
	{"META:pages",      "Pages",       NULL,           B_INT32_TYPE},
	{NULL, NULL, NULL, 0}
};

// A query only finds files by an attribute that is indexed on their volume, so the indices for what Tsundoku writes
// are made (once for a volume) if they are missing.
static void
EnsureIndices(dev_t device)
{
	static const struct { const char* name; uint32 type; } kIndices[] = {
		{ "META:title", B_STRING_TYPE }, { "META:author", B_STRING_TYPE }, { "META:subject", B_STRING_TYPE },
		{ "META:creator", B_STRING_TYPE }, { "META:keyw", B_STRING_TYPE }, { "META:pages", B_INT32_TYPE },
		{ "META:description", B_STRING_TYPE }, { "META:publisher", B_STRING_TYPE },
		{ "META:inLanguage", B_STRING_TYPE }, { "META:identifier", B_STRING_TYPE }, { "META:isbn", B_STRING_TYPE },
		{ "META:isPartOf", B_STRING_TYPE }, { "META:position", B_DOUBLE_TYPE },
		{ "META:datePublished", B_INT64_TYPE }, { "META:annotated", B_INT32_TYPE },
		{ "PDF:created", B_INT64_TYPE }, { "PDF:modified", B_INT64_TYPE }
	};
	static dev_t sDone[16];
	static int sDoneCount = 0;
	for (int i = 0; i < sDoneCount; i++) {
		if (sDone[i] == device)
			return;
	}
	if (sDoneCount < 16)
		sDone[sDoneCount++] = device;

	for (size_t i = 0; i < sizeof(kIndices) / sizeof(kIndices[0]); i++) {
		index_info info;
		if (fs_stat_index(device, kIndices[i].name, &info) != 0)
			fs_create_index(device, kIndices[i].name, kIndices[i].type, 0);
	}
}


///////////////////////////////////////////////////////////
void
BepdfApplication::UpdateAttr(BNode &node, const char *name, type_code type, off_t offset, void *buffer, size_t length) {
	char dummy[10];
	if (B_ENTRY_NOT_FOUND == node.ReadAttr(name, type, offset, (char*)dummy, sizeof(dummy))) {
		node.WriteAttr(name, type, offset, buffer, length);
	}
}


///////////////////////////////////////////////////////////
void
BepdfApplication::UpdateFileAttributes(Document *doc, entry_ref *ref) {
	BNode node(ref);
	if (node.InitCheck() != B_OK) return;
	EnsureIndices(ref->device);

	const bool force_overwrite = (modifiers() & B_COMMAND_KEY) == B_COMMAND_KEY;

	if (force_overwrite) {
		for (int i = 0; gAttrInfo[i].name; i++) {
			node.RemoveAttr(gAttrInfo[i].name);
		}
	}

	// The number of pages of a book is what it has in the standard configuration (6 x 9 inches, the default text
	// size): an estimate that stays the same for inventory and citations, like the page count that shops give for
	// an e-book. A book that is read at another text size says nothing about it.
	if (!doc->IsReflowable() || doc->TextSize() == Document::kDefaultTextSize) {
		int32 pages = (int32)doc->PageCount();
		UpdateAttr(node, "META:pages", B_INT32_TYPE, 0, &pages, sizeof(int32));
	}

	for (int i = 0; gAttrInfo[i].name; i++) {
		if (gAttrInfo[i].pdf_name == NULL) continue;

		time_t time;
		BString value;
		if (FileInfoWindow::GetProperty(doc, gAttrInfo[i].pdf_name, &value, &time)) {
			if (gAttrInfo[i].type_code == B_TIME_TYPE) {
				if (time != 0) {
					UpdateAttr(node, gAttrInfo[i].name, B_TIME_TYPE, 0, &time, sizeof(time));
				}
			} else {
				UpdateAttr(node, gAttrInfo[i].name, B_STRING_TYPE, 0, (void*)value.String(), value.Length()+1);
			}
		}
	}

	// what only a book says, one attribute for each (so they can be shown in Tracker and queried)
	if (const EpubInfo* epub = doc->Epub()) {
		// (those of the first versions of Tsundoku had names of their own)
		static const char* const kOld[] = { "EPUB:language", "EPUB:publisher", "EPUB:published", "EPUB:identifier",
			"EPUB:series", "EPUB:series_index", "EPUB:version", NULL };
		for (int i = 0; kOld[i] != NULL; i++)
			node.RemoveAttr(kOld[i]);

		struct { const char* name; const BString* value; } strings[] = {
			{ "META:description", &epub->description }, { "META:publisher", &epub->publisher },
			{ "META:inLanguage", &epub->language }, { "META:identifier", &epub->identifier },
			{ "META:isbn", &epub->isbn }, { "META:isPartOf", &epub->series }
		};
		for (size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); i++) {
			if (strings[i].value->Length() > 0)
				UpdateAttr(node, strings[i].name, B_STRING_TYPE, 0, (void*)strings[i].value->String(),
					strings[i].value->Length() + 1);
		}
		if (epub->seriesIndex.Length() > 0) {
			double position = atof(epub->seriesIndex.String());
			UpdateAttr(node, "META:position", B_DOUBLE_TYPE, 0, &position, sizeof(position));
		}
		// the date as far as it is given: 2026, 2026-09 or 2026-09-01
		int year = 0, month = 1, day = 1;
		if (sscanf(epub->date.String(), "%d-%d-%d", &year, &month, &day) >= 1 && year > 0) {
			struct tm date;
			memset(&date, 0, sizeof(date));
			date.tm_year = year - 1900;
			date.tm_mon = month >= 1 && month <= 12 ? month - 1 : 0;
			date.tm_mday = day >= 1 && day <= 31 ? day : 1;
			date.tm_hour = 12;
			time_t published = mktime(&date);
			if (published != (time_t)-1)
				UpdateAttr(node, "META:datePublished", B_TIME_TYPE, 0, &published, sizeof(published));
		}
	}

	// whether it has annotations (for a book also the ones that are only in the attribute)
	BPath annotatedPath(ref);
	if (annotatedPath.InitCheck() == B_OK)
		doc->SyncAnnotatedAttribute(annotatedPath.Path());
}


const char* BepdfApplication::GetVersion(BString &version) {
	version = "?.?.?";
	if (be_app == NULL) {
		return version.String();
	}

	app_info info;
	if (be_app->GetAppInfo(&info) != B_OK) {
		return version.String();
	}

	BFile file(&info.ref, B_READ_ONLY);
	if (file.InitCheck() != B_OK) {
		return version.String();
	}

	BAppFileInfo appFileInfo(&file);
	version_info appVersion;
	if (appFileInfo.GetVersionInfo(&appVersion, B_APP_VERSION_KIND) != B_OK) {
		return version.String();
	}

	BString variety = B_TRANSLATE("Unknown");
	switch (appVersion.variety) {
		case 0: variety = B_TRANSLATE("Development");
			break;
		case 1: variety = B_TRANSLATE("Alpha");
			break;
		case 2: variety = B_TRANSLATE("Beta");
			break;
		case 3: variety = B_TRANSLATE("Gamma");
			break;
		case 4: variety = B_TRANSLATE("Golden Master");
			break;
		case 5:
			if (appVersion.internal == 0) {
				// hide variety
				variety = "";
			}
			else {
				variety = B_TRANSLATE("Final");
			}
			break;
	};
	version = "";
	version << appVersion.major << "."
		<< appVersion.middle << "."
		<< appVersion.minor
		<< " " << variety;
	if (appVersion.internal != 0) {
		version << " "<< appVersion.internal;
	}
	return version.String();
}

