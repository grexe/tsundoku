/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
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

#include <stdio.h>

#include <locale/Catalog.h>
#include <Box.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <Directory.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <ScrollView.h>
#include <StringView.h>
#include <String.h>
#include "Application.h"
#include "LayoutUtils.h"
#include "PreferencesWindow.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AnnotationWindow"

void PreferencesWindow::UpdateWorkspace() {
	GlobalSettings* settings = gApp->GetSettings();
	bool enabled = settings->GetOpenInWorkspace();
	int32 ws = settings->GetWorkspace();
	BMenu *m = mOpenInWorkspace->Menu();
	int32 n = m->CountItems();
	int32 i;
	i = (enabled) ? 1 + ws : 0;
	if (i >= n) i = n - 1;
	BMenuItem *item = m->ItemAt(i);
	item->SetMarked(true);
}

void PreferencesWindow::BuildWorkspaceMenu(BMenu *m) {
	m->AddItem(new BMenuItem(B_TRANSLATE("current"), new BMessage(WORKSPACE_CHANGED)));
	m->AddSeparatorItem();
	int n = count_workspaces();

	BString buffer;
	for (int i = 1; i <= n; i ++) {
		buffer.SetToFormat("%d", i);
		m->AddItem(new BMenuItem(buffer.String(), new BMessage(WORKSPACE_CHANGED)));
	}
	m->SetLabelFromMarked(true);
	m->SetRadioMode(true);
}

void PreferencesWindow::SetupView() {
	char workspace[3];

	GlobalSettings* settings = gApp->GetSettings();
	sprintf(workspace, "%d", (int)settings->GetWorkspace());
	mPreferences = new BOutlineListView("mPreferences");
	BScrollView* prefScroll = new BScrollView("SV", mPreferences,
		B_FRAME_EVENTS | B_WILL_DRAW, false, true);

	BListItem *item;
	mPreferences->AddItem(new BStringItem(B_TRANSLATE("Document")));

	mPreferences->AddItem(item = new BStringItem(B_TRANSLATE("Display")));

	mPreferences->SetSelectionMessage(new BMessage(PREFERENCE_SELECTED));
	mPreferences->SetExplicitMinSize(BSize(200, 0));

	BCheckBox *pageNumber = new BCheckBox("pageNumber",
		B_TRANSLATE("Restore page number"), new BMessage(RESTORE_PAGE_NO_CHANGED));
	pageNumber->SetValue(settings->GetRestorePageNumber());

	BCheckBox *windowPos = new BCheckBox("windowPos",
		B_TRANSLATE("Restore window position and size"),
		new BMessage(RESTORE_WINDOW_FRAME_CHANGED));
	windowPos->SetValue(settings->GetRestoreWindowFrame());

	BCheckBox *replaceAttributes = new BCheckBox("replaceAttributes",
		B_TRANSLATE("Replace legacy attributes with standard ones"),
		new BMessage(REPLACE_ATTRIBUTES_CHANGED));
	replaceAttributes->SetValue(settings->GetLegacyAttributes() == 2);
	replaceAttributes->SetToolTip(B_TRANSLATE("Files that BePDF has opened have attributes with its own names "
		"(META:title, META:author, ...). Toji writes the same information with standard names (dc:title, "
		"dc:creator, ...) in any case. The legacy attributes are kept unless this is on: then they are removed."));

	BCheckBox *upgradeIds = new BCheckBox("upgradeIds",
		B_TRANSLATE("Describe annotations of other programs in the file's attributes"),
		new BMessage(UPGRADE_IDS_CHANGED));
	upgradeIds->SetValue(settings->GetUpgradeAnnotationIds());
	upgradeIds->SetToolTip(B_TRANSLATE("When a PDF file is opened, its annotations are described in the attribute "
		"SEN:annotations, and those that have no name get an identifier, so that other programs can refer to them. "
		"The PDF file is not changed."));

	BPopUpMenu *openMenu = new BPopUpMenu("openMenu");
	mOpenInWorkspace = new BMenuField("mOpeninWorkspace",
		B_TRANSLATE("Open in workspace:"), openMenu);

	BTextControl *author = new BTextControl("author", B_TRANSLATE("Author"),
		settings->GetAuthor(), new BMessage(AUTHOR_CHANGED));

	BGroupLayout *docBox = BLayoutBuilder::Group<>(B_VERTICAL, 0)
		.SetInsets(B_USE_SMALL_INSETS, B_USE_SMALL_INSETS, B_USE_SMALL_INSETS, 0)
		.Add(pageNumber)
		.Add(windowPos)
		.AddStrut(B_USE_ITEM_INSETS)
		.Add(replaceAttributes)
		.Add(upgradeIds)
		.AddStrut(B_USE_ITEM_INSETS)
		.Add(mOpenInWorkspace)
		.AddStrut(B_USE_SMALL_INSETS)
		.Add(author)
		.AddGlue();

	BBox *document = new BBox("document");
	document->SetLabel("Document");
	document->AddChild(docBox->View());

	BRadioButton *docOnly = new BRadioButton("docOnly",
		B_TRANSLATE("Show document view only"),
		new BMessage(QUASI_FULLSCREEN_MODE_OFF));
	docOnly->SetValue(!(settings->GetQuasiFullscreenMode()));

	BRadioButton *docMore = new BRadioButton("docMore",
		B_TRANSLATE("Show toolbar, statusbar and scrollbars, too"),
		new BMessage(QUASI_FULLSCREEN_MODE_ON));
	docMore->SetValue(settings->GetQuasiFullscreenMode());

	BGroupLayout *fsBox = BLayoutBuilder::Group<>(B_VERTICAL, 0)
		.SetInsets(B_USE_SMALL_INSETS, B_USE_SMALL_INSETS, B_USE_SMALL_INSETS, 0)
		.Add(docOnly)
		.Add(docMore)
		.AddStrut(B_USE_SMALL_INSETS);

	BBox *fullscreen = new BBox("fullscreen");
	fullscreen->SetLabel(B_TRANSLATE("Fullscreen mode"));
	fullscreen->AddChild(fsBox->View());

	BRadioButton *filledRect = new BRadioButton("filledRect",
		B_TRANSLATE("Filled rectangle"),
		new BMessage(FILLED_SELECTION_FILLED));
	filledRect->SetValue(settings->GetFilledSelection());

	BRadioButton *strokedRect = new BRadioButton("strokedRect",
		B_TRANSLATE("Stroked rectangle"),
		new BMessage(FILLED_SELECTION_STROKED));
	strokedRect->SetValue(!(settings->GetFilledSelection()));

	BGroupLayout *rectBox = BLayoutBuilder::Group<>(B_VERTICAL, 0)
		.SetInsets(B_USE_SMALL_INSETS, B_USE_SMALL_INSETS, B_USE_SMALL_INSETS, 0)
		.Add(filledRect)
		.Add(strokedRect)
		.AddStrut(B_USE_SMALL_INSETS);

	BBox *selection = new BBox("selection");
	selection->SetLabel(B_TRANSLATE("Selection rectangle"));
	selection->AddChild(rectBox->View());

	BCheckBox *scrolling = new BCheckBox("scrolling",
		B_TRANSLATE("Invert vertical mouse scrolling"),
		new BMessage(INVERT_VERTICAL_SCROLLING_CHANGED));
	scrolling->SetValue(settings->GetInvertVerticalScrolling());

	BCheckBox *fancy = new BCheckBox("fancy", B_TRANSLATE("Fancy mode: pages turn"),
		new BMessage(FANCY_MODE_CHANGED));
	fancy->SetValue(settings->GetFancyMode());
	fancy->SetToolTip(B_TRANSLATE("A page that turns like in a book when you go to the next or the previous page "
		"(not in the continuous flow)."));
	BCheckBox *fancySound = new BCheckBox("fancySound", B_TRANSLATE("With sound"),
		new BMessage(FANCY_SOUND_CHANGED));
	fancySound->SetValue(settings->GetFancySound());
	fancySound->SetEnabled(settings->GetFancyMode());
	mFancySound = fancySound;

	BLayoutBuilder::Group<>(this, B_HORIZONTAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(prefScroll)
		.AddCards()
			.Add(document)
			.AddGroup(B_VERTICAL)
				.Add(fullscreen)
				.Add(selection)
				.Add(scrolling)
				.Add(fancy)
				.AddGroup(B_HORIZONTAL, 0)
					.AddStrut(be_plain_font->Size() + be_control_look->DefaultLabelSpacing())
					.Add(fancySound)
					.AddGlue()
				.End()
				.AddGlue()
			.End()
			.GetLayout(&mLayers)
		.End();

	author->SetModificationMessage(new BMessage(AUTHOR_CHANGED));

	mLayers->SetVisibleItem((int32)1);
	mLayers->SetVisibleItem((int32)0);
	mPreferences->Select(0);
#ifdef TOJI_TESTING
	if (getenv("TOJI_PREFS_PAGE") != NULL) {
		// (tests) the page of the settings that is shown
		int32 page = atoi(getenv("TOJI_PREFS_PAGE"));
		mLayers->SetVisibleItem(page);
		mPreferences->Select(page);
	}
#endif

	BuildWorkspaceMenu(mOpenInWorkspace->Menu());
	UpdateWorkspace();
	ResizeTo(0, 0);
	CenterOnScreen();
}

void PreferencesWindow::ClearView()
{
}

PreferencesWindow::PreferencesWindow(GlobalSettings *settings, BLooper *looper)
	: BWindow(BRect(0, 0, 100, 100)
		, B_TRANSLATE("Preferences")
		, B_TITLED_WINDOW_LOOK,
		B_FLOATING_APP_WINDOW_FEEL,
		B_AUTO_UPDATE_SIZE_LIMITS)
	, mLooper(looper)
	, mSettings(settings)
{
	AddCommonFilter(new EscapeMessageFilter(this, B_QUIT_REQUESTED));

	SetupView();

	Show();
}

PreferencesWindow::~PreferencesWindow() {
}

class TranslatedFileItem : public BStringItem {
	BString mFileName;
public:
	TranslatedFileItem(const char* name, const char* filename) :
	  BStringItem(name), mFileName(filename) { }
	const char* FileName() const { return mFileName.String(); }
};

bool PreferencesWindow::DecodeMessage(BMessage *msg, int16 &kind, int16 &which, int16 &index) {
	// assert msg->what == PREFERENCES_CHANGED_NOTIFY
	return ((B_OK == msg->FindInt16("kind", &kind))
		&& (B_OK == msg->FindInt16("which", &which))
		&& (B_OK == msg->FindInt16("index", &index)));
}

void PreferencesWindow::Notify(uint32 what) {
	BMessage m(what);
	mLooper->PostMessage(&m);
}

void PreferencesWindow::NotifyRestartDoc() {
	Notify(RESTART_DOC_NOTIFY);
}

void PreferencesWindow::MessageReceived(BMessage *msg) {
	switch (msg->what) {
	case PREFERENCE_SELECTED:
		if (mPreferences->FullListCurrentSelection() >= 0) {
			mLayers->SetVisibleItem(mPreferences->FullListCurrentSelection());
		}
		break;
	case RESTORE_PAGE_NO_CHANGED: mSettings->SetRestorePageNumber(IsOn(msg));
		break;
	case RESTORE_WINDOW_FRAME_CHANGED: mSettings->SetRestoreWindowFrame(IsOn(msg));
		break;
	case REPLACE_ATTRIBUTES_CHANGED: mSettings->SetLegacyAttributes(IsOn(msg) ? 2 : 1);
		break;
	case UPGRADE_IDS_CHANGED: mSettings->SetUpgradeAnnotationIds(IsOn(msg));
		break;
	case QUASI_FULLSCREEN_MODE_ON:
		gApp->GetSettings()->SetQuasiFullscreenMode(true);
		break;
	case QUASI_FULLSCREEN_MODE_OFF:
		gApp->GetSettings()->SetQuasiFullscreenMode(false);
		break;
	case WORKSPACE_CHANGED: {
		int32 index;
		if (B_OK == msg->FindInt32("index", &index)) {
			if (index == 1) index = 0;
			bool enabled = index != 0;
			gApp->GetSettings()->SetOpenInWorkspace(enabled);
			if (enabled) gApp->GetSettings()->SetWorkspace(index - 1);
			UpdateWorkspace();
		}
		}
		break;
	case AUTHOR_CHANGED: {
		void* p;
			if (msg->FindPointer("source", &p) == B_OK) {
				BTextControl* t = (BTextControl*)p;
				gApp->GetSettings()->SetAuthor(t->Text());
			}
		}
		break;
	case FANCY_MODE_CHANGED:
		mSettings->SetFancyMode(IsOn(msg));
		mFancySound->SetEnabled(IsOn(msg));
		break;
	case FANCY_SOUND_CHANGED:
		mSettings->SetFancySound(IsOn(msg));
		break;
	case INVERT_VERTICAL_SCROLLING_CHANGED:
		mSettings->SetInvertVerticalScrolling(IsOn(msg));
		Notify(UPDATE_NOTIFY);
		break;
	case FILLED_SELECTION_FILLED:
	case FILLED_SELECTION_STROKED: {
		BMessage nmsg(CHANGE_NOTIFY);
		nmsg.AddInt16("kind", DISPLAY);
		nmsg.AddInt16("which", DISPLAY_FILLED_SELECTION);

		nmsg.AddInt16("index", msg->what == FILLED_SELECTION_FILLED ? 0 : 1);
		mLooper->PostMessage(&nmsg);
		}
		break;
	}

	BWindow::MessageReceived(msg);
}

bool PreferencesWindow::QuitRequested() {
	if (mLooper) {
		BMessage msg(QUIT_NOTIFY);
		mLooper->PostMessage(&msg);
	}
	ClearView();
	return true;
}
