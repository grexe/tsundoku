/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tsundoku: a universal document reader for Haiku, extended for SEN.
 * 	 Copyright (C) 2026 Gregor B. Rosenauer & Claude
 *
 * Based on BePDF:
 * 	 Copyright (C) 1997 Benoit Triquet.
 * 	 Copyright (C) 1998-2000 Hubert Figuiere.
 * 	 Copyright (C) 2000-2011 Michael Pfeiffer.
 * 	 Copyright (C) 2013 waddlesplash.
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
 * License for more details.
 */

#ifndef _ATTACHMENTS_VIEW_H_
#define _ATTACHMENTS_VIEW_H_

#include <vector>

#include <FilePanel.h>
#include <View.h>

#include "Document.h"

class BButton;
class BListView;

// The files embedded in the document, with a button (or a double click) to save one of them.
class AttachmentsView : public BView {
public:
	AttachmentsView(Document* document);
	virtual ~AttachmentsView();

	void SetDocument(Document* document);
	int  Count() const { return (int)fAttachments.size(); }

	virtual void AttachedToWindow();
	virtual void MessageReceived(BMessage* message);

private:
	void Fill();
	void SaveSelected();
	void Update();

	Document*      fDocument;
	std::vector<DocAttachment> fAttachments;
	BListView*     fList;
	BButton*       fSave;
	BFilePanel*    fPanel;
	int            fSaving;
};

#endif
