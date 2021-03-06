/*

Copyright 2000-12 Miranda IM, 2012-16 Miranda NG project,
all portions of this codebase are copyrighted to the people
listed in contributors.txt.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "stdafx.h"


// The code for streaming the text is to a large extent copied from
// the srmm module and then modified to fit the chat module.

static DWORD CALLBACK Log_StreamCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG * pcb)
{
	LOGSTREAMDATA *lstrdat = (LOGSTREAMDATA *) dwCookie;
	if (lstrdat) {
		// create the RTF
		if (lstrdat->buffer == NULL) {
			lstrdat->bufferOffset = 0;
			lstrdat->buffer = pci->Log_CreateRTF(lstrdat);
			lstrdat->bufferLen = (int)mir_strlen(lstrdat->buffer);
		}

		// give the RTF to the RE control
		*pcb = min(cb, lstrdat->bufferLen - lstrdat->bufferOffset);
		memcpy(pbBuff, lstrdat->buffer + lstrdat->bufferOffset, *pcb);
		lstrdat->bufferOffset += *pcb;

		// free stuff if the streaming operation is complete
		if (lstrdat->bufferOffset == lstrdat->bufferLen) {
			mir_free(lstrdat->buffer);
			lstrdat->buffer = NULL;
		}
	}

	return 0;
}

void Log_StreamInEvent(HWND hwndDlg,  LOGINFO* lin, SESSION_INFO *si, BOOL bRedraw, BOOL)
{
	CHARRANGE oldsel, sel, newsel;
	POINT point ={0};
	WPARAM wp;

	if (hwndDlg == 0 || lin == 0 || si == 0)
		return;

	if (!bRedraw && si->iType == GCW_CHATROOM && si->bFilterEnabled && (si->iLogFilterFlags & lin->iType) == 0)
		return;

	HWND hwndRich = GetDlgItem(hwndDlg, IDC_LOG);
	
	LOGSTREAMDATA streamData;
	memset(&streamData, 0, sizeof(streamData));
	streamData.hwnd = hwndRich;
	streamData.si = si;
	streamData.lin = lin;
	streamData.bStripFormat = FALSE;

	BOOL bFlag = FALSE;

	EDITSTREAM stream = { 0 };
	stream.pfnCallback = Log_StreamCallback;
	stream.dwCookie = (DWORD_PTR) & streamData;

	SCROLLINFO scroll;
	scroll.cbSize = sizeof(SCROLLINFO);
	scroll.fMask= SIF_RANGE | SIF_POS|SIF_PAGE;
	GetScrollInfo(GetDlgItem(hwndDlg, IDC_LOG), SB_VERT, &scroll);
	SendMessage(hwndRich, EM_GETSCROLLPOS, 0, (LPARAM) &point);

	// do not scroll to bottom if there is a selection
	SendMessage(hwndRich, EM_EXGETSEL, 0, (LPARAM) &oldsel);
	if (oldsel.cpMax != oldsel.cpMin)
		SendMessage(hwndRich, WM_SETREDRAW, FALSE, 0);

	//set the insertion point at the bottom
	sel.cpMin = sel.cpMax = GetRichTextLength(hwndRich);
	SendMessage(hwndRich, EM_EXSETSEL, 0, (LPARAM) &sel);

	// fix for the indent... must be a M$ bug
	if (sel.cpMax == 0)
		bRedraw = TRUE;

	// should the event(s) be appended to the current log
	wp = bRedraw?SF_RTF:SFF_SELECTION|SF_RTF;

	//get the number of pixels per logical inch
	if (bRedraw) {
		HDC hdc = GetDC(NULL);
		pci->logPixelSY = GetDeviceCaps(hdc, LOGPIXELSY);
		pci->logPixelSX = GetDeviceCaps(hdc, LOGPIXELSX);
		ReleaseDC(NULL, hdc);
		SendMessage(hwndRich, WM_SETREDRAW, FALSE, 0);
		bFlag = TRUE;
	}

	// stream in the event(s)
	streamData.lin = lin;
	streamData.bRedraw = bRedraw;
	SendMessage(hwndRich, EM_STREAMIN, wp, (LPARAM) & stream);

	// do smileys
	if (SmileyAddInstalled && (bRedraw
		|| (lin->ptszText
		&& lin->iType != GC_EVENT_JOIN
		&& lin->iType != GC_EVENT_NICK
		&& lin->iType != GC_EVENT_ADDSTATUS
		&& lin->iType != GC_EVENT_REMOVESTATUS )))
	{
		SMADD_RICHEDIT3 sm = {0};

		newsel.cpMax = -1;
		newsel.cpMin = sel.cpMin;
		if (newsel.cpMin < 0)
			newsel.cpMin = 0;
		memset(&sm, 0, sizeof(sm));
		sm.cbSize = sizeof(sm);
		sm.hwndRichEditControl = hwndRich;
		sm.Protocolname = si->pszModule;
		sm.rangeToReplace = bRedraw?NULL:&newsel;
		sm.disableRedraw = TRUE;
		sm.hContact = si->hContact;
		CallService(MS_SMILEYADD_REPLACESMILEYS, 0, (LPARAM)&sm);
	}

	// scroll log to bottom if the log was previously scrolled to bottom, else restore old position
	if (bRedraw ||  (UINT)scroll.nPos >= (UINT)scroll.nMax-scroll.nPage-5 || scroll.nMax-scroll.nMin-scroll.nPage < 50)
		SendMessage(GetParent(hwndRich), GC_SCROLLTOBOTTOM, 0, 0);
	else
		SendMessage(hwndRich, EM_SETSCROLLPOS, 0, (LPARAM) &point);

	// do we need to restore the selection
	if (oldsel.cpMax != oldsel.cpMin) {
		SendMessage(hwndRich, EM_EXSETSEL, 0, (LPARAM) & oldsel);
		SendMessage(hwndRich, WM_SETREDRAW, TRUE, 0);
		InvalidateRect(hwndRich, NULL, TRUE);
	}

	// need to invalidate the window
	if (bFlag) {
		sel.cpMin = sel.cpMax = GetRichTextLength(hwndRich);
		SendMessage(hwndRich, EM_EXSETSEL, 0, (LPARAM) &sel);
		SendMessage(hwndRich, WM_SETREDRAW, TRUE, 0);
		InvalidateRect(hwndRich, NULL, TRUE);
}	}

/////////////////////////////////////////////////////////////////////////////////////////

static DWORD CALLBACK Message_StreamCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG * pcb)
{
	static DWORD dwRead;
	char ** ppText = (char **)dwCookie;

	if (*ppText == NULL) {
		*ppText = (char *)mir_alloc(cb + 1);
		memcpy(*ppText, pbBuff, cb);
		(*ppText)[cb] = 0;
		*pcb = cb;
		dwRead = cb;
	}
	else {
		char *p = (char *)mir_alloc(dwRead + cb + 1);
		memcpy(p, *ppText, dwRead);
		memcpy(p + dwRead, pbBuff, cb);
		p[dwRead + cb] = 0;
		mir_free(*ppText);
		*ppText = p;
		*pcb = cb;
		dwRead += cb;
	}

	return 0;
}

char* Message_GetFromStream(HWND hwndDlg, SESSION_INFO *si)
{
	EDITSTREAM stream;
	char* pszText = NULL;
	DWORD dwFlags;

	if (hwndDlg == 0 || si == 0)
		return NULL;

	memset(&stream, 0, sizeof(stream));
	stream.pfnCallback = Message_StreamCallback;
	stream.dwCookie = (DWORD_PTR)&pszText; // pass pointer to pointer

	dwFlags = SF_RTFNOOBJS | SFF_PLAINRTF | SF_USECODEPAGE | (CP_UTF8 << 16);
	SendDlgItemMessage(hwndDlg, IDC_MESSAGE, EM_STREAMOUT, dwFlags, (LPARAM)& stream);
	return pszText; // pszText contains the text
}

/////////////////////////////////////////////////////////////////////////////////////////

void ShowRoom(SESSION_INFO *si, WPARAM wp, BOOL bSetForeground)
{
	if (!si)
		return;

	if (g_Settings.bTabsEnable) {
		// the session is not the current tab, so we copy the necessary
		// details into the SESSION_INFO for the tabbed window
		if (!si->hWnd) {
			g_TabSession.iEventCount = si->iEventCount;
			g_TabSession.iStatusCount = si->iStatusCount;
			g_TabSession.iType = si->iType;
			g_TabSession.nUsersInNicklist = si->nUsersInNicklist;
			g_TabSession.pLog = si->pLog;
			g_TabSession.pLogEnd = si->pLogEnd;
			g_TabSession.pMe = si->pMe;
			g_TabSession.pStatuses = si->pStatuses;
			g_TabSession.ptszID = si->ptszID;
			g_TabSession.pszModule = si->pszModule;
			g_TabSession.ptszName = si->ptszName;
			g_TabSession.ptszStatusbarText = si->ptszStatusbarText;
			g_TabSession.ptszTopic = si->ptszTopic;
			g_TabSession.pUsers = si->pUsers;
			g_TabSession.hContact = si->hContact;
			g_TabSession.wStatus = si->wStatus;
			g_TabSession.lpCommands = si->lpCommands;
			g_TabSession.lpCurrentCommand = NULL;
		}

		// Do we need to create a tabbed window?
		if (g_TabSession.hWnd == NULL)
			g_TabSession.hWnd = CreateDialogParam(g_hInst, MAKEINTRESOURCE(IDD_CHANNEL), NULL, RoomWndProc, (LPARAM)&g_TabSession);

		SetWindowLongPtr(g_TabSession.hWnd, GWL_EXSTYLE, GetWindowLongPtr(g_TabSession.hWnd, GWL_EXSTYLE) | WS_EX_APPWINDOW);

		// if the session was not the current tab we need to tell the window to
		// redraw to show the contents of the current SESSION_INFO
		if (!si->hWnd) {
			SM_SetTabbedWindowHwnd(si, g_TabSession.hWnd);
			SendMessage(g_TabSession.hWnd, GC_ADDTAB, -1, (LPARAM)si);
			SendMessage(g_TabSession.hWnd, GC_TABCHANGE, 0, (LPARAM)&g_TabSession);
		}

		pci->SetActiveSession(si->ptszID, si->pszModule);

		if (!IsWindowVisible(g_TabSession.hWnd) || wp == WINDOW_HIDDEN)
			SendMessage(g_TabSession.hWnd, GC_CONTROL_MSG, wp, 0);
		else {
			if (IsIconic(g_TabSession.hWnd))
				ShowWindow(g_TabSession.hWnd, SW_NORMAL);

			PostMessage(g_TabSession.hWnd, WM_SIZE, 0, 0);
			if (si->iType != GCW_SERVER)
				SendMessage(g_TabSession.hWnd, GC_UPDATENICKLIST, 0, 0);
			else
				SendMessage(g_TabSession.hWnd, GC_UPDATETITLE, 0, 0);
			SendMessage(g_TabSession.hWnd, GC_REDRAWLOG, 0, 0);
			SendMessage(g_TabSession.hWnd, GC_UPDATESTATUSBAR, 0, 0);
			ShowWindow(g_TabSession.hWnd, SW_SHOW);
			if (bSetForeground)
				SetForegroundWindow(g_TabSession.hWnd);
		}
		SendMessage(g_TabSession.hWnd, WM_MOUSEACTIVATE, 0, 0);
		SetFocus(GetDlgItem(g_TabSession.hWnd, IDC_MESSAGE));
		return;
	}

	// Do we need to create a window?
	if (si->hWnd == NULL)
		si->hWnd = CreateDialogParam(g_hInst, MAKEINTRESOURCE(IDD_CHANNEL), NULL, RoomWndProc, (LPARAM)si);

	SetWindowLongPtr(si->hWnd, GWL_EXSTYLE, GetWindowLongPtr(si->hWnd, GWL_EXSTYLE) | WS_EX_APPWINDOW);
	if (!IsWindowVisible(si->hWnd) || wp == WINDOW_HIDDEN)
		SendMessage(si->hWnd, GC_CONTROL_MSG, wp, 0);
	else {
		if (IsIconic(si->hWnd))
			ShowWindow(si->hWnd, SW_NORMAL);
		ShowWindow(si->hWnd, SW_SHOW);
		SetForegroundWindow(si->hWnd);
	}

	SendMessage(si->hWnd, WM_MOUSEACTIVATE, 0, 0);
	SetFocus(GetDlgItem(si->hWnd, IDC_MESSAGE));
}

bool LoadMessageFont(LOGFONT *lf, COLORREF *colour)
{
	char str[32];
	int i = 8; // MSGFONTID_MESSAGEAREA

	if (colour) {
		mir_snprintf(str, "SRMFont%dCol", i);
		*colour = db_get_dw(NULL, "SRMM", str, 0);
	}
	if (lf) {
		mir_snprintf(str, "SRMFont%dSize", i);
		lf->lfHeight = (char)db_get_b(NULL, "SRMM", str, -12);
		lf->lfWidth = 0;
		lf->lfEscapement = 0;
		lf->lfOrientation = 0;
		mir_snprintf(str, "SRMFont%dSty", i);
		int style = db_get_b(NULL, "SRMM", str, 0);
		lf->lfWeight = style & DBFONTF_BOLD ? FW_BOLD : FW_NORMAL;
		lf->lfItalic = style & DBFONTF_ITALIC ? 1 : 0;
		lf->lfUnderline = 0;
		lf->lfStrikeOut = 0;
		lf->lfOutPrecision = OUT_DEFAULT_PRECIS;
		lf->lfClipPrecision = CLIP_DEFAULT_PRECIS;
		lf->lfQuality = DEFAULT_QUALITY;
		lf->lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
		mir_snprintf(str, "SRMFont%d", i);

		DBVARIANT dbv;
		if (db_get_ws(NULL, "SRMM", str, &dbv))
			mir_wstrcpy(lf->lfFaceName, L"Arial");
		else {
			mir_wstrncpy(lf->lfFaceName, dbv.ptszVal, _countof(lf->lfFaceName));
			db_free(&dbv);
		}
		mir_snprintf(str, "SRMFont%dSet", i);
		lf->lfCharSet = db_get_b(NULL, "SRMM", str, DEFAULT_CHARSET);
	}
	return true;
}

int GetRichTextLength(HWND hwnd)
{
	GETTEXTLENGTHEX gtl;
	gtl.flags = GTL_PRECISE;
	gtl.codepage = CP_ACP;
	return (int)SendMessage(hwnd, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
}

int GetColorIndex(const char* pszModule, COLORREF cr)
{
	MODULEINFO * pMod = pci->MM_FindModule(pszModule);
	int i = 0;

	if (!pMod || pMod->nColorCount == 0)
		return -1;

	for (i = 0; i < pMod->nColorCount; i++)
	if (pMod->crColors[i] == cr)
		return i;

	return -1;
}

// obscure function that is used to make sure that any of the colors
// passed by the protocol is used as fore- or background color
// in the messagebox. THis is to vvercome limitations in the richedit
// that I do not know currently how to fix

void CheckColorsInModule(const char* pszModule)
{
	MODULEINFO *pMod = pci->MM_FindModule(pszModule);
	int i = 0;
	COLORREF crBG = (COLORREF)db_get_dw(NULL, CHAT_MODULE, "ColorMessageBG", GetSysColor(COLOR_WINDOW));

	if (!pMod)
		return;

	for (i = 0; i < pMod->nColorCount; i++) {
		if (pMod->crColors[i] == g_Settings.MessageAreaColor || pMod->crColors[i] == crBG) {
			if (pMod->crColors[i] == RGB(255, 255, 255))
				pMod->crColors[i]--;
			else
				pMod->crColors[i]++;
		}
	}
}

UINT CreateGCMenu(HWND hwndDlg, HMENU *hMenu, int iIndex, POINT pt, SESSION_INFO *si, wchar_t* pszUID, wchar_t* pszWordText)
{
	HMENU hSubMenu = 0;
	*hMenu = GetSubMenu(g_hMenu, iIndex);
	TranslateMenu(*hMenu);

	GCMENUITEMS gcmi = { 0 };
	gcmi.pszID = si->ptszID;
	gcmi.pszModule = si->pszModule;
	gcmi.pszUID = pszUID;

	if (iIndex == 1) {
		int i = GetRichTextLength(GetDlgItem(hwndDlg, IDC_LOG));

		EnableMenuItem(*hMenu, ID_CLEARLOG, MF_ENABLED);
		EnableMenuItem(*hMenu, ID_COPYALL, MF_ENABLED);
		ModifyMenu(*hMenu, 4, MF_GRAYED | MF_BYPOSITION, 4, NULL);
		if (!i) {
			EnableMenuItem(*hMenu, ID_COPYALL, MF_BYCOMMAND | MF_GRAYED);
			EnableMenuItem(*hMenu, ID_CLEARLOG, MF_BYCOMMAND | MF_GRAYED);
			if (pszWordText && pszWordText[0])
				ModifyMenu(*hMenu, 4, MF_ENABLED | MF_BYPOSITION, 4, NULL);
		}

		if (pszWordText && pszWordText[0]) {
			wchar_t szMenuText[4096];
			mir_snwprintf(szMenuText, TranslateT("Look up '%s':"), pszWordText);
			ModifyMenu(*hMenu, 4, MF_STRING | MF_BYPOSITION, 4, szMenuText);
		}
		else ModifyMenu(*hMenu, 4, MF_STRING | MF_GRAYED | MF_BYPOSITION, 4, TranslateT("No word to look up"));
		gcmi.Type = MENU_ON_LOG;
	}
	else if (iIndex == 0) {
		wchar_t szTemp[50];
		if (pszWordText)
			mir_snwprintf(szTemp, TranslateT("&Message %s"), pszWordText);
		else
			mir_wstrncpy(szTemp, TranslateT("&Message"), _countof(szTemp) - 1);

		if (mir_wstrlen(szTemp) > 40)
			mir_wstrcpy(szTemp + 40, L"...");
		ModifyMenu(*hMenu, ID_MESS, MF_STRING | MF_BYCOMMAND, ID_MESS, szTemp);
		gcmi.Type = MENU_ON_NICKLIST;
	}

	NotifyEventHooks(pci->hBuildMenuEvent, 0, (WPARAM)&gcmi);

	if (gcmi.nItems > 0)
		AppendMenu(*hMenu, MF_SEPARATOR, 0, 0);

	for (int i = 0; i < gcmi.nItems; i++) {
		wchar_t* ptszText = TranslateW(gcmi.Item[i].pszDesc);
		DWORD dwState = gcmi.Item[i].bDisabled ? MF_GRAYED : 0;

		if (gcmi.Item[i].uType == MENU_NEWPOPUP) {
			hSubMenu = CreateMenu();
			AppendMenu(*hMenu, dwState | MF_POPUP, (UINT_PTR)hSubMenu, ptszText);
		}
		else if (gcmi.Item[i].uType == MENU_POPUPHMENU)
			AppendMenu(hSubMenu == 0 ? *hMenu : hSubMenu, dwState | MF_POPUP, gcmi.Item[i].dwID, ptszText);
		else if (gcmi.Item[i].uType == MENU_POPUPITEM)
			AppendMenu(hSubMenu == 0 ? *hMenu : hSubMenu, dwState | MF_STRING, gcmi.Item[i].dwID, ptszText);
		else if (gcmi.Item[i].uType == MENU_POPUPCHECK)
			AppendMenu(hSubMenu == 0 ? *hMenu : hSubMenu, dwState | MF_CHECKED | MF_STRING, gcmi.Item[i].dwID, ptszText);
		else if (gcmi.Item[i].uType == MENU_POPUPSEPARATOR)
			AppendMenu(hSubMenu == 0 ? *hMenu : hSubMenu, MF_SEPARATOR, 0, ptszText);
		else if (gcmi.Item[i].uType == MENU_SEPARATOR)
			AppendMenu(*hMenu, MF_SEPARATOR, 0, ptszText);
		else if (gcmi.Item[i].uType == MENU_HMENU)
			AppendMenu(*hMenu, dwState | MF_POPUP, gcmi.Item[i].dwID, ptszText);
		else if (gcmi.Item[i].uType == MENU_ITEM)
			AppendMenu(*hMenu, dwState | MF_STRING, gcmi.Item[i].dwID, ptszText);
		else if (gcmi.Item[i].uType == MENU_CHECK)
			AppendMenu(*hMenu, dwState | MF_CHECKED | MF_STRING, gcmi.Item[i].dwID, ptszText);
	}
	return TrackPopupMenu(*hMenu, TPM_RETURNCMD, pt.x, pt.y, 0, hwndDlg, NULL);
}

void DestroyGCMenu(HMENU *hMenu, int iIndex)
{
	MENUITEMINFO mii = { 0 };
	mii.cbSize = sizeof(mii);
	mii.fMask = MIIM_SUBMENU;
	while (GetMenuItemInfo(*hMenu, iIndex, TRUE, &mii)) {
		if (mii.hSubMenu != NULL)
			DestroyMenu(mii.hSubMenu);
		RemoveMenu(*hMenu, iIndex, MF_BYPOSITION);
	}
}

void ValidateFilename(wchar_t *filename)
{
	wchar_t *p1 = filename;
	wchar_t szForbidden[] = L"\\/:*?\"<>|";
	while (*p1 != '\0') {
		if (wcschr(szForbidden, *p1))
			*p1 = '_';
		p1 += 1;
	}
}
