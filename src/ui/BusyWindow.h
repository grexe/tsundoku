/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Toji: a universal document reader for Haiku, extended for SEN.
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


#ifndef _BUSY_WINDOW_H_
#define _BUSY_WINDOW_H_

#include <Window.h>

class BarberPole;
class BStringView;

// A small window over another one that says that something takes a while, with a barber pole. It does not block the
// window it is over.
class BusyWindow : public BWindow {
public:
	BusyWindow(BWindow* over, const char* text);

	void Appear();
	void Disappear();

private:
	BWindow*     fOver;
	BarberPole*  fPole;
};

#endif
