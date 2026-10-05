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

#ifndef _BE_PDFAPPLICATION_
#define _BE_PDFAPPLICATION_

#include <be/app/Application.h>
#include <be/app/Cursor.h>
#include <be/storage/FilePanel.h>
#include <be/storage/Path.h>
#include <be/storage/Node.h>
#include "Settings.h"

#define BEPDF_APP_SIG "application/x-vnd.sen-labs.Tsundoku"

class PDFWindow;
class OutputTracer;
class Document;

// Returns a filter for PDF files
BRefFilter* GetPdfFilter();

///////////////////////////////////////////////////////////
class BepdfApplication
	: BApplication
{
public:
	BepdfApplication();
	~BepdfApplication();

	virtual void ReadyToRun();
	virtual void RefsReceived(BMessage* msg );
	virtual void ArgvReceived(int32 argc, char** argv );
	virtual void MessageReceived (BMessage* msg);
	virtual void AboutRequested();
	virtual bool QuitRequested();
	
	// open file panel to open a PDF file
	void OpenFilePanel();
	// open file panel to save a file
	void OpenSaveFilePanel(BHandler* handler, BRefFilter* filter, BMessage* msg = NULL, const char* name = NULL);
	void OpenSaveToDirectoryFilePanel(BHandler* handler, BRefFilter* filter, BMessage* msg = NULL, const char* name = NULL);
		
	void LoadSettings();
	void SaveSettings();
		
	GlobalSettings* GetSettings() { return mSettings; };
		
	BCursor* pointerCursor; 
	BCursor* linkCursor;
	BCursor* handCursor;
	BCursor* grabCursor;
	BCursor* textSelectionCursor;
	BCursor* zoomCursor;
	BCursor* splitVCursor;
	BCursor* resizeCursor;
			
	BPath  *GetAppPath() { return &mAppPath; }
	BPath  *DefaultPDF() { return &mDefaultPDF; }
	team_id GetTeamID() { return mTeamID; }
	
	enum {
		NOTIFY_OPEN_MSG   = 'BPop', // BePDF document opened
		NOTIFY_CLOSE_MSG  = 'BPcl', // BePDF closed
		NOTIFY_QUIT_MSG   = 'BPqt', // Close all BePDF applications
		REQUEST_TITLE_MSG = 'BPrt', // Request the window titles
	};

	void Notify(uint32 cmd);
	void WindowClosed()       { mWindow = NULL; }
	
	static void UpdateAttr(BNode &node, const char* name, type_code type, off_t offset, void* buffer, size_t length);
	static void UpdatePublished(BNode &node, const char* date);
	static void UpdateFileAttributes(Document* doc, entry_ref* ref);
	// Whether the file has attributes from BePDF (META:title, bepdf:zoom, ...). The setting LegacyAttributes (0 = not asked yet,
	// 1 = keep them besides the standard ones, 2 = replace them) says what to do with them.
	static bool FileHasLegacyAttributes(entry_ref* ref);
	// Replaces the legacy attributes of the file by the standard ones if the setting says so.
	static void ApplyLegacyChoice(entry_ref* ref);
	
private:
	const char* GetVersion(BString &version);
	void Initialize();
	void OpenSaveFilePanel(BHandler* handler, bool fileMode, BRefFilter* filter, BMessage* msg, const char* name);

	bool           mInitialized;
	bool           mGotSomething;
	bool           mReadyToQuit;
	BFilePanel*    mOpenFilePanel;
	BFilePanel*    mSaveFilePanel;
	BFilePanel*    mSaveToDirectoryFilePanel;
	BPath          mAppPath;
	BPath          mDefaultPDF;
	team_id        mTeamID;
	entry_ref      mAppRef;
	PDFWindow*     mWindow;

	GlobalSettings* mSettings;
	OutputTracer*   mStdoutTracer;
	OutputTracer*   mStderrTracer;
};

#define gApp ((BepdfApplication*)(be_app))

#endif
