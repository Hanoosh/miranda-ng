////////////////////////////////////////////////////////////////////////////////////////
// Miranda NG: the free IM client for Microsoft* Windows*
//
// Copyright (с) 2012-16 Miranda NG project,
// Copyright (c) 2000-09 Miranda ICQ/IM project,
// all portions of this codebase are copyrighted to the people
// listed in contributors.txt.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// you should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//
// part of tabSRMM messaging plugin for Miranda.
//
// (C) 2005-2010 by silvercircle _at_ gmail _dot_ com and contributors
//
// generic utility functions

#include "stdafx.h"

#include <string>

typedef std::basic_string<wchar_t> tstring;

#define MWF_LOG_BBCODE 1
#define MWF_LOG_TEXTFORMAT 0x2000000
#define MSGDLGFONTCOUNT 22

static TRTFColorTable _rtf_ctable[] =
{
	{ L"red", RGB(255, 0, 0), ID_FONT_RED },
	{ L"blue", RGB(0, 0, 255), ID_FONT_BLUE },
	{ L"green", RGB(0, 255, 0), ID_FONT_GREEN },
	{ L"magenta", RGB(255, 0, 255), ID_FONT_MAGENTA },
	{ L"yellow", RGB(255, 255, 0), ID_FONT_YELLOW },
	{ L"cyan", RGB(0, 255, 255), ID_FONT_CYAN },
	{ L"black", 0, ID_FONT_BLACK },
	{ L"white", RGB(255, 255, 255), ID_FONT_WHITE },
	{ L"", 0, 0 }
};

int				Utils::rtf_ctable_size = 0;
TRTFColorTable* Utils::rtf_ctable = 0;

MWindowList CWarning::hWindowList = 0;

static wchar_t *w_bbcodes_begin[] = { L"[b]", L"[i]", L"[u]", L"[s]", L"[color=" };
static wchar_t *w_bbcodes_end[] = { L"[/b]", L"[/i]", L"[/u]", L"[/s]", L"[/color]" };

static wchar_t *formatting_strings_begin[] = { L"b1 ", L"i1 ", L"u1 ", L"s1 ", L"c1 " };
static wchar_t *formatting_strings_end[] = { L"b0 ", L"i0 ", L"u0 ", L"s0 ", L"c0 " };

#define NR_CODES 5

/////////////////////////////////////////////////////////////////////////////////////////
// this translates formatting tags into rtf sequences...
// flags: loword = words only for simple  * /_ formatting
//        hiword = bbcode support (strip bbcodes if 0)

const wchar_t* Utils::FormatRaw(TWindowData *dat, const wchar_t *msg, int flags, BOOL isSent)
{
	bool 	clr_was_added = false, was_added;
	static tstring message(msg);
	size_t beginmark = 0, endmark = 0, tempmark = 0, index;
	int i, endindex;
	wchar_t endmarker;
	DWORD	dwFlags = dat->dwFlags;
	message.assign(msg);

	if (dwFlags & MWF_LOG_BBCODE) {
		beginmark = 0;
		while (true) {
			for (i = 0; i < NR_CODES; i++) {
				if ((tempmark = message.find(w_bbcodes_begin[i], 0)) != message.npos)
					break;
			}
			if (i >= NR_CODES)
				break;

			beginmark = tempmark;
			endindex = i;
			endmark = message.find(w_bbcodes_end[i], beginmark);
			if (endindex == 4) {                                 // color
				size_t closing = message.find_first_of(L"]", beginmark);
				was_added = false;

				if (closing == message.npos) {                      // must be an invalid [color=] tag w/o closing bracket
					message[beginmark] = ' ';
					continue;
				}

				tstring colorname = message.substr(beginmark + 7, 8);
search_again:
				bool clr_found = false;
				for (int ii = 0; ii < rtf_ctable_size; ii++) {
					if (!wcsnicmp((wchar_t*)colorname.c_str(), rtf_ctable[ii].szName, mir_wstrlen(rtf_ctable[ii].szName))) {
						closing = beginmark + 7 + mir_wstrlen(rtf_ctable[ii].szName);
						if (endmark != message.npos) {
							message.erase(endmark, 4);
							message.replace(endmark, 4, L"c0 ");
						}
						message.erase(beginmark, (closing - beginmark));

						wchar_t szTemp[5];
						message.insert(beginmark, L"cxxx ");
						mir_snwprintf(szTemp, L"%02d", MSGDLGFONTCOUNT + 13 + ii);
						message[beginmark + 3] = szTemp[0];
						message[beginmark + 4] = szTemp[1];
						clr_found = true;
						if (was_added) {
							wchar_t wszTemp[100];
							mir_snwprintf(wszTemp, L"##col##%06u:%04u", endmark - closing, ii);
							wszTemp[99] = 0;
							message.insert(beginmark, wszTemp);
						}
						break;
					}
				}
				if (!clr_found) {
					size_t c_closing = colorname.find_first_of(L"]", 0);
					if (c_closing == colorname.npos)
						c_closing = colorname.length();
					const wchar_t *wszColname = colorname.c_str();
					if (endmark != message.npos && c_closing > 2 && c_closing <= 6 && iswalnum(colorname[0]) && iswalnum(colorname[c_closing - 1])) {
						RTF_ColorAdd(wszColname, c_closing);
						if (!was_added) {
							clr_was_added = was_added = true;
							goto search_again;
						}
						else goto invalid_code;
					}
					else {
invalid_code:
						if (endmark != message.npos)
							message.erase(endmark, 8);
						if (closing != message.npos && closing < (size_t)endmark)
							message.erase(beginmark, (closing - beginmark) + 1);
						else
							message[beginmark] = ' ';
					}
				}
				continue;
			}
			if (endmark != message.npos)
				message.replace(endmark, 4, formatting_strings_end[i]);
			message.insert(beginmark, L" ");
			message.replace(beginmark, 4, formatting_strings_begin[i]);
		}
	}

	if (!(dwFlags & MWF_LOG_TEXTFORMAT) || message.find(L"://") != message.npos) {
		dat->clr_added = clr_was_added ? TRUE : FALSE;
		return(message.c_str());
	}

	while ((beginmark = message.find_first_of(L"*/_", beginmark)) != message.npos) {
		endmarker = message[beginmark];
		if (LOWORD(flags)) {
			if (beginmark > 0 && !iswspace(message[beginmark - 1]) && !iswpunct(message[beginmark - 1])) {
				beginmark++;
				continue;
			}

			// search a corresponding endmarker which fulfills the criteria
			INT_PTR mark = beginmark + 1;
			while ((endmark = message.find(endmarker, mark)) != message.npos) {
				if (iswpunct(message[endmark + 1]) || iswspace(message[endmark + 1]) || message[endmark + 1] == 0 || wcschr(L"*/_", message[endmark + 1]) != NULL)
					goto ok;
				mark = endmark + 1;
			}
			break;
		}
		else {
			if ((endmark = message.find(endmarker, beginmark + 1)) == message.npos)
				break;
		}
ok:
		if ((endmark - beginmark) < 2) {
			beginmark++;
			continue;
		}
		index = 0;
		switch (endmarker) {
		case '*':
			index = 0;
			break;
		case '/':
			index = 1;
			break;
		case '_':
			index = 2;
			break;
		}

		// check if the code enclosed by simple formatting tags is a valid smiley code and skip formatting if
		// it really is one.
		if (PluginConfig.g_SmileyAddAvail && (endmark > (beginmark + 1))) {
			tstring smcode;
			smcode.assign(message, beginmark, (endmark - beginmark) + 1);

			SMADD_BATCHPARSE2 smbp = { 0 };
			smbp.cbSize = sizeof(smbp);
			smbp.Protocolname = dat->cache->getActiveProto();
			smbp.flag = SAFL_TCHAR | SAFL_PATH | (isSent ? SAFL_OUTGOING : 0);
			smbp.str = (wchar_t*)smcode.c_str();
			smbp.hContact = dat->hContact;
			SMADD_BATCHPARSERES *smbpr = (SMADD_BATCHPARSERES *)CallService(MS_SMILEYADD_BATCHPARSE, 0, (LPARAM)&smbp);
			if (smbpr) {
				CallService(MS_SMILEYADD_BATCHFREE, 0, (LPARAM)smbpr);
				beginmark = endmark + 1;
				continue;
			}
		}
		message.insert(endmark, L"%%%");
		message.replace(endmark, 4, formatting_strings_end[index]);
		message.insert(beginmark, L"%%%");
		message.replace(beginmark, 4, formatting_strings_begin[index]);
	}
	dat->clr_added = clr_was_added ? TRUE : FALSE;
	return(message.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////
// format the title bar string for IM chat sessions using placeholders.
// the caller must mir_free() the returned string

static wchar_t* Trunc500(wchar_t *str)
{
	if (mir_wstrlen(str) > 500)
		str[500] = 0;
	return str;
}

bool Utils::FormatTitleBar(const TWindowData *dat, const wchar_t *szFormat, CMStringW &dest)
{
	if (dat == 0)
		return false;

	for (const wchar_t *src = szFormat; *src; src++) {
		if (*src != '%') {
			dest.AppendChar(*src);
			continue;
		}

		switch (*++src) {
		case 'n':
			dest.Append(dat->cache->getNick());
			break;

		case 'p':
		case 'a':
			dest.Append(dat->cache->getRealAccount());
			break;

		case 's':
			dest.Append(dat->szStatus);
			break;

		case 'u':
			dest.Append(dat->cache->getUIN());
			break;

		case 'c':
			dest.Append(!mir_wstrcmp(dat->pContainer->szName, L"default") ? TranslateT("Default container") : dat->pContainer->szName);
			break;

		case 'o':
			{
				const char *szProto = dat->cache->getActiveProto();
				if (szProto)
					dest.Append(_A2T(szProto));
			}
			break;

		case 'x':
			{
				BYTE xStatus = dat->cache->getXStatusId();
				if (dat->wStatus != ID_STATUS_OFFLINE && xStatus > 0 && xStatus <= 31) {
					ptrW szXStatus(db_get_wsa(dat->hContact, dat->szProto, "XStatusName"));
					dest.Append((szXStatus != NULL) ? Trunc500(szXStatus) : xStatusDescr[xStatus - 1]);
				}
			}
			break;

		case 'm':
			{
				BYTE xStatus = dat->cache->getXStatusId();
				if (dat->wStatus != ID_STATUS_OFFLINE && xStatus > 0 && xStatus <= 31) {
					ptrW szXStatus(db_get_wsa(dat->hContact, dat->szProto, "XStatusName"));
					dest.Append((szXStatus != NULL) ? Trunc500(szXStatus) : xStatusDescr[xStatus - 1]);
				}
				else dest.Append(dat->szStatus[0] ? dat->szStatus : L"(undef)");
			}
			break;

		// status message (%T will skip the "No status message" for empty messages)
		case 't':
		case 'T':
			{
				ptrW tszStatus(dat->cache->getNormalizedStatusMsg(dat->cache->getStatusMsg(), true));
				if (tszStatus)
					dest.Append(tszStatus);
				else if (*src == 't')
					dest.Append(TranslateT("No status message"));
			}
			break;

		case 'g':
			{
				ptrW tszGroup(db_get_wsa(dat->hContact, "CList", "Group"));
				if (tszGroup != NULL)
					dest.Append(tszGroup);
			}
			break;

		case 0: // wrongly formed format string
			return true;
		}
	}

	return true;
}

/////////////////////////////////////////////////////////////////////////////////////////

char* Utils::FilterEventMarkers(char *szText)
{
	for (char *p = strstr(szText, "~-+"); p != NULL; p = strstr(p, "~-+")) {
		char *pEnd = strstr(p + 3, "+-~");
		if (pEnd == NULL)
			break;

		strdel(p, (pEnd - p) + 3);
	}

	return szText;
}

WCHAR* Utils::FilterEventMarkers(WCHAR *wszText)
{
	for (WCHAR *p = wcsstr(wszText, L"~-+"); p != NULL; p = wcsstr(p, L"~-+")) {
		WCHAR *pEnd = wcsstr(p + 3, L"+-~");
		if (pEnd == NULL)
			break;

		strdelw(p, (pEnd - p) + 3);
	}

	return wszText;
}

/////////////////////////////////////////////////////////////////////////////////////////

void Utils::DoubleAmpersands(wchar_t *pszText, size_t len)
{
	CMStringW text(pszText);
	text.Replace(L"&", L"&&");
	mir_wstrncpy(pszText, text.c_str(), len);
}

/////////////////////////////////////////////////////////////////////////////////////////
// Get a preview of the text with an ellipsis appended(...)
//
// @param szText	source text
// @param iMaxLen	max length of the preview
// @return wchar_t*   result (caller must mir_free() it)

wchar_t* Utils::GetPreviewWithEllipsis(wchar_t *szText, size_t iMaxLen)
{
	size_t uRequired;
	wchar_t *p = 0, cSaved;
	bool	 fEllipsis = false;

	if (mir_wstrlen(szText) <= iMaxLen) {
		uRequired = mir_wstrlen(szText) + 4;
		cSaved = 0;
	}
	else {
		p = &szText[iMaxLen - 1];
		fEllipsis = true;

		while (p >= szText && *p != ' ')
			p--;
		if (p == szText)
			p = szText + iMaxLen - 1;

		cSaved = *p;
		*p = 0;
		uRequired = (p - szText) + 6;
	}
	wchar_t *szResult = reinterpret_cast<wchar_t *>(mir_alloc((uRequired + 1) * sizeof(wchar_t)));
	mir_snwprintf(szResult, (uRequired + 1), fEllipsis ? L"%s..." : L"%s", szText);

	if (p)
		*p = cSaved;

	return szResult;
}

/////////////////////////////////////////////////////////////////////////////////////////
// returns != 0 when one of the installed keyboard layouts belongs to an rtl language
// used to find out whether we need to configure the message input box for bidirectional mode

int Utils::FindRTLLocale(TWindowData *dat)
{
	HKL layouts[20];
	int i, result = 0;
	LCID lcid;
	WORD wCtype2[5];

	if (dat->iHaveRTLLang == 0) {
		memset(layouts, 0, sizeof(layouts));
		GetKeyboardLayoutList(20, layouts);
		for (i = 0; i < 20 && layouts[i]; i++) {
			lcid = MAKELCID(LOWORD(layouts[i]), 0);
			GetStringTypeA(lcid, CT_CTYPE2, "���", 3, wCtype2);
			if (wCtype2[0] == C2_RIGHTTOLEFT || wCtype2[1] == C2_RIGHTTOLEFT || wCtype2[2] == C2_RIGHTTOLEFT)
				result = 1;
		}
		dat->iHaveRTLLang = (result ? 1 : -1);
	}
	else result = dat->iHaveRTLLang == 1 ? 1 : 0;

	return result;
}

/////////////////////////////////////////////////////////////////////////////////////////
// init default color table. the table may grow when using custom colors via bbcodes

void Utils::RTF_CTableInit()
{
	int iSize = sizeof(TRTFColorTable) * RTF_CTABLE_DEFSIZE;

	rtf_ctable = (TRTFColorTable *)mir_alloc(iSize);
	memset(rtf_ctable, 0, iSize);
	memcpy(rtf_ctable, _rtf_ctable, iSize);
	rtf_ctable_size = RTF_CTABLE_DEFSIZE;
}

/////////////////////////////////////////////////////////////////////////////////////////
// add a color to the global rtf color table

void Utils::RTF_ColorAdd(const wchar_t *tszColname, size_t length)
{
	wchar_t *stopped;

	rtf_ctable_size++;
	rtf_ctable = (TRTFColorTable *)mir_realloc(rtf_ctable, sizeof(TRTFColorTable) * rtf_ctable_size);
	COLORREF clr = wcstol(tszColname, &stopped, 16);
	mir_snwprintf(rtf_ctable[rtf_ctable_size - 1].szName, length + 1, L"%06x", clr);
	rtf_ctable[rtf_ctable_size - 1].menuid = 0;

	clr = wcstol(tszColname, &stopped, 16);
	rtf_ctable[rtf_ctable_size - 1].clr = (RGB(GetBValue(clr), GetGValue(clr), GetRValue(clr)));
}

/////////////////////////////////////////////////////////////////////////////////////////
// generic error popup dialog procedure

LRESULT CALLBACK Utils::PopupDlgProcError(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	MCONTACT hContact = PUGetContact(hWnd);

	switch (message) {
	case WM_COMMAND:
		PostMessage(PluginConfig.g_hwndHotkeyHandler, DM_HANDLECLISTEVENT, hContact, 0);
		PUDeletePopup(hWnd);
		break;
	case WM_CONTEXTMENU:
		PostMessage(PluginConfig.g_hwndHotkeyHandler, DM_HANDLECLISTEVENT, hContact, 0);
		PUDeletePopup(hWnd);
		break;
	case WM_MOUSEWHEEL:
		break;
	case WM_SETCURSOR:
		break;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

/////////////////////////////////////////////////////////////////////////////////////////
// read a blob from db into the container settings structure
// @param hContact:	contact handle (0 = read global)
// @param cs		TContainerSettings* target structure
// @return			0 on success, 1 failure (blob does not exist OR is not a valid private setting structure

int Utils::ReadContainerSettingsFromDB(const MCONTACT hContact, TContainerSettings *cs, const char *szKey)
{
	memcpy(cs, &PluginConfig.globalContainerSettings, sizeof(TContainerSettings));

	DBVARIANT dbv = { 0 };
	if (0 == db_get(hContact, SRMSGMOD_T, szKey ? szKey : CNT_KEYNAME, &dbv)) {
		if (dbv.type == DBVT_BLOB && dbv.cpbVal > 0 && dbv.cpbVal <= sizeof(TContainerSettings)) {
			::memcpy((void*)cs, (void*)dbv.pbVal, dbv.cpbVal);
			::db_free(&dbv);
			if (hContact == 0 && szKey == 0)
				cs->fPrivate = false;
			return 0;
		}
		cs->fPrivate = false;
		db_free(&dbv);
		return 1;
	}

	cs->fPrivate = false;
	return 1;
}

int Utils::WriteContainerSettingsToDB(const MCONTACT hContact, TContainerSettings *cs, const char *szKey)
{
	::db_set_blob(hContact, SRMSGMOD_T, szKey ? szKey : CNT_KEYNAME, cs, sizeof(TContainerSettings));
	return 0;
}

void Utils::SettingsToContainer(TContainerData *pContainer)
{
	pContainer->dwFlags = pContainer->settings->dwFlags;
	pContainer->dwFlagsEx = pContainer->settings->dwFlagsEx;
	pContainer->avatarMode = pContainer->settings->avatarMode;
	pContainer->ownAvatarMode = pContainer->settings->ownAvatarMode;
}

void Utils::ContainerToSettings(TContainerData *pContainer)
{
	pContainer->settings->dwFlags = pContainer->dwFlags;
	pContainer->settings->dwFlagsEx = pContainer->dwFlagsEx;
	pContainer->settings->avatarMode = pContainer->avatarMode;
	pContainer->settings->ownAvatarMode = pContainer->ownAvatarMode;
}

/////////////////////////////////////////////////////////////////////////////////////////
// read settings for a container with private settings enabled.
//
// @param pContainer	container window info struct
// @param fForce		true -> force them private, even if they were not marked as private in the db

void Utils::ReadPrivateContainerSettings(TContainerData *pContainer, bool fForce)
{
	char szCname[50];
	TContainerSettings csTemp = { 0 };

	mir_snprintf(szCname, "%s%d_Blob", CNT_BASEKEYNAME, pContainer->iContainerIndex);
	Utils::ReadContainerSettingsFromDB(0, &csTemp, szCname);
	if (csTemp.fPrivate || fForce) {
		if (pContainer->settings == 0 || pContainer->settings == &PluginConfig.globalContainerSettings)
			pContainer->settings = (TContainerSettings *)mir_alloc(sizeof(TContainerSettings));
		memcpy((void*)pContainer->settings, (void*)&csTemp, sizeof(TContainerSettings));
		pContainer->settings->fPrivate = true;
	}
	else pContainer->settings = &PluginConfig.globalContainerSettings;
}

void Utils::SaveContainerSettings(TContainerData *pContainer, const char *szSetting)
{
	char szCName[50];

	pContainer->dwFlags &= ~(CNT_DEFERREDCONFIGURE | CNT_CREATE_MINIMIZED | CNT_DEFERREDSIZEREQUEST | CNT_CREATE_CLONED);
	if (pContainer->settings->fPrivate) {
		mir_snprintf(szCName, "%s%d_Blob", szSetting, pContainer->iContainerIndex);
		WriteContainerSettingsToDB(0, pContainer->settings, szCName);
	}
	mir_snprintf(szCName, "%s%d_theme", szSetting, pContainer->iContainerIndex);
	if (mir_wstrlen(pContainer->szRelThemeFile) > 1) {
		if (pContainer->fPrivateThemeChanged == TRUE) {
			PathToRelativeW(pContainer->szRelThemeFile, pContainer->szAbsThemeFile, M.getDataPath());
			db_set_ws(NULL, SRMSGMOD_T, szCName, pContainer->szAbsThemeFile);
			pContainer->fPrivateThemeChanged = FALSE;
		}
	}
	else {
		::db_unset(NULL, SRMSGMOD_T, szCName);
		pContainer->fPrivateThemeChanged = FALSE;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////
// calculate new width and height values for a user picture (avatar)
//
// @param: maxHeight -	determines maximum height for the picture, width will
// 					be scaled accordingly.

void Utils::scaleAvatarHeightLimited(const HBITMAP hBm, double& dNewWidth, double& dNewHeight, LONG maxHeight)
{
	BITMAP	bm;
	double	dAspect;

	GetObject(hBm, sizeof(bm), &bm);

	if (bm.bmHeight > bm.bmWidth) {
		if (bm.bmHeight > 0)
			dAspect = (double)(maxHeight) / (double)bm.bmHeight;
		else
			dAspect = 1.0;
		dNewWidth = (double)bm.bmWidth * dAspect;
		dNewHeight = (double)maxHeight;
	}
	else {
		if (bm.bmWidth > 0)
			dAspect = (double)(maxHeight) / (double)bm.bmWidth;
		else
			dAspect = 1.0;
		dNewHeight = (double)bm.bmHeight * dAspect;
		dNewWidth = (double)maxHeight;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////
// convert the avatar bitmap to icon format so that it can be used on the task bar
// tries to keep correct aspect ratio of the avatar image
//
// @param dat: _MessageWindowData* pointer to the window data
// @return HICON: the icon handle

HICON Utils::iconFromAvatar(const TWindowData *dat)
{
	if (!ServiceExists(MS_AV_GETAVATARBITMAP) || dat == NULL)
		return 0;

	AVATARCACHEENTRY *ace = (AVATARCACHEENTRY *)CallService(MS_AV_GETAVATARBITMAP, dat->hContact, 0);
	if (ace == NULL || ace->hbmPic == NULL)
		return NULL;

	LONG lIconSize = Win7Taskbar->getIconSize();
	double dNewWidth, dNewHeight;
	scaleAvatarHeightLimited(ace->hbmPic, dNewWidth, dNewHeight, lIconSize);

	// resize picture to fit it on the task bar, use an image list for converting it to
	// 32bpp icon format. dat->hTaskbarIcon will cache it until avatar is changed
	bool fFree = false;
	HBITMAP hbmResized = CSkin::ResizeBitmap(ace->hbmPic, (LONG)dNewWidth, (LONG)dNewHeight, fFree);
	HIMAGELIST hIml_c = ::ImageList_Create(lIconSize, lIconSize, ILC_COLOR32 | ILC_MASK, 1, 0);

	RECT rc = { 0, 0, lIconSize, lIconSize };

	HDC hdc = ::GetDC(dat->pContainer->hwnd);
	HDC dc = ::CreateCompatibleDC(hdc);
	HDC dcResized = ::CreateCompatibleDC(hdc);

	ReleaseDC(dat->pContainer->hwnd, hdc);

	HBITMAP hbmNew = CSkin::CreateAeroCompatibleBitmap(rc, dc);
	HBITMAP hbmOld = reinterpret_cast<HBITMAP>(::SelectObject(dc, hbmNew));
	HBITMAP hbmOldResized = reinterpret_cast<HBITMAP>(::SelectObject(dcResized, hbmResized));

	LONG ix = (lIconSize - (LONG)dNewWidth) / 2;
	LONG iy = (lIconSize - (LONG)dNewHeight) / 2;
	CSkin::m_default_bf.SourceConstantAlpha = M.GetByte("taskBarIconAlpha", 255);
	GdiAlphaBlend(dc, ix, iy, (LONG)dNewWidth, (LONG)dNewHeight, dcResized, 0, 0, (LONG)dNewWidth, (LONG)dNewHeight, CSkin::m_default_bf);

	CSkin::m_default_bf.SourceConstantAlpha = 255;
	::SelectObject(dc, hbmOld);
	::ImageList_Add(hIml_c, hbmNew, 0);
	::DeleteObject(hbmNew);
	::DeleteDC(dc);

	::SelectObject(dcResized, hbmOldResized);
	if (hbmResized != ace->hbmPic)
		::DeleteObject(hbmResized);
	::DeleteDC(dcResized);
	HICON hIcon = ::ImageList_GetIcon(hIml_c, 0, ILD_NORMAL);
	::ImageList_RemoveAll(hIml_c);
	::ImageList_Destroy(hIml_c);
	return hIcon;
}

AVATARCACHEENTRY* Utils::loadAvatarFromAVS(const MCONTACT hContact)
{
	if (!ServiceExists(MS_AV_GETAVATARBITMAP))
		return 0;

	return (AVATARCACHEENTRY*)CallService(MS_AV_GETAVATARBITMAP, hContact, 0);
}

void Utils::sendContactMessage(MCONTACT hContact, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	HWND h = M.FindWindow(hContact);
	if (h != NULL)
		PostMessage(h, uMsg, wParam, lParam);
}

void Utils::getIconSize(HICON hIcon, int& sizeX, int& sizeY)
{
	ICONINFO ii;
	BITMAP bm;
	::GetIconInfo(hIcon, &ii);
	::GetObject(ii.hbmColor, sizeof(bm), &bm);
	sizeX = bm.bmWidth;
	sizeY = bm.bmHeight;
	::DeleteObject(ii.hbmMask);
	::DeleteObject(ii.hbmColor);
}

/////////////////////////////////////////////////////////////////////////////////////////
// add a menu item to a ownerdrawn menu. mii must be pre-initialized
//
// @param m			menu handle
// @param mii		menu item info structure
// @param hIcon		the icon (0 allowed -> no icon)
// @param szText	menu item text (must NOT be 0)
// @param uID		the item command id
// @param pos		zero-based position index

void Utils::addMenuItem(const HMENU& m, MENUITEMINFO &mii, HICON hIcon, const wchar_t *szText, UINT uID, UINT pos)
{
	mii.wID = uID;
	mii.dwItemData = (ULONG_PTR)hIcon;
	mii.dwTypeData = const_cast<wchar_t *>(szText);
	mii.cch = (int)mir_wstrlen(mii.dwTypeData) + 1;

	::InsertMenuItem(m, pos, TRUE, &mii);
}

/////////////////////////////////////////////////////////////////////////////////////////
// return != 0 when the sound effect must be played for the given
// session. Uses container sound settings

int Utils::mustPlaySound(const TWindowData *dat)
{
	if (!dat)
		return 0;

	if (dat->pContainer->fHidden)		// hidden container is treated as closed, so play the sound
		return 1;

	if (dat->pContainer->dwFlags & CNT_NOSOUND || nen_options.iNoSounds)
		return 0;

	bool fActiveWindow = (dat->pContainer->hwnd == ::GetForegroundWindow() ? true : false);
	bool fActiveTab = (dat->pContainer->hwndActive == dat->hwnd ? true : false);
	bool fIconic = (::IsIconic(dat->pContainer->hwnd) ? true : false);

	// window minimized, check if sound has to be played
	if (fIconic)
		return(dat->pContainer->dwFlagsEx & CNT_EX_SOUNDS_MINIMIZED ? 1 : 0);

	// window in foreground
	if (!fActiveWindow)
		return(dat->pContainer->dwFlagsEx & CNT_EX_SOUNDS_UNFOCUSED ? 1 : 0);

	if (fActiveTab)
		return(dat->pContainer->dwFlagsEx & CNT_EX_SOUNDS_FOCUSED ? 1 : 0);
	return(dat->pContainer->dwFlagsEx & CNT_EX_SOUNDS_INACTIVETABS ? 1 : 0);
}

/////////////////////////////////////////////////////////////////////////////////////////
// enable or disable a dialog control

void Utils::enableDlgControl(const HWND hwnd, UINT id, bool fEnable)
{
	::EnableWindow(::GetDlgItem(hwnd, id), fEnable);
}

/////////////////////////////////////////////////////////////////////////////////////////
// show or hide a dialog control

void Utils::showDlgControl(const HWND hwnd, UINT id, int showCmd)
{
	::ShowWindow(::GetDlgItem(hwnd, id), showCmd);
}

/////////////////////////////////////////////////////////////////////////////////////////
// stream function to write the contents of the message log to an rtf file
DWORD CALLBACK Utils::StreamOut(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG * pcb)
{
	wchar_t *szFilename = (wchar_t*)dwCookie;
	HANDLE hFile = CreateFile(szFilename, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile != INVALID_HANDLE_VALUE) {
		SetFilePointer(hFile, 0, NULL, FILE_END);
		FilterEventMarkers((char*)pbBuff);
		WriteFile(hFile, pbBuff, cb, (DWORD *)pcb, NULL);
		*pcb = cb;
		CloseHandle(hFile);
		return 0;
	}
	return 1;
}

/////////////////////////////////////////////////////////////////////////////////////////
// extract a resource from the given module
// tszPath must end with \ 

bool Utils::extractResource(const HMODULE h, const UINT uID, const wchar_t *tszName, const wchar_t *tszPath,
	const wchar_t *tszFilename, bool fForceOverwrite)
{
	HRSRC hRes = FindResource(h, MAKEINTRESOURCE(uID), tszName);
	if (hRes) {
		HGLOBAL hResource = LoadResource(h, hRes);
		if (hResource) {
			char 	*pData = (char *)LockResource(hResource);
			DWORD	dwSize = SizeofResource(g_hInst, hRes), written = 0;

			wchar_t	szFilename[MAX_PATH];
			mir_snwprintf(szFilename, L"%s%s", tszPath, tszFilename);
			if (!fForceOverwrite)
				if (PathFileExists(szFilename))
					return true;

			HANDLE hFile = CreateFile(szFilename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
			if (hFile == INVALID_HANDLE_VALUE)
				return false;

			WriteFile(hFile, (void*)pData, dwSize, &written, NULL);
			CloseHandle(hFile);
		}
	}
	return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
// extract the clicked URL from a rich edit control. Return the URL as wchar_t*
// caller MUST mir_free() the returned string
// @param 	hwndRich -  rich edit window handle
// @return	wchar_t*	extracted URL

wchar_t* Utils::extractURLFromRichEdit(const ENLINK* _e, const HWND hwndRich)
{
	CHARRANGE sel = { 0 };
	::SendMessage(hwndRich, EM_EXGETSEL, 0, (LPARAM)&sel);
	if (sel.cpMin != sel.cpMax)
		return 0;

	TEXTRANGE tr;
	tr.chrg = _e->chrg;
	tr.lpstrText = (wchar_t*)mir_alloc(sizeof(wchar_t) * (tr.chrg.cpMax - tr.chrg.cpMin + 8));
	::SendMessage(hwndRich, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
	if (wcschr(tr.lpstrText, '@') != NULL && wcschr(tr.lpstrText, ':') == NULL && wcschr(tr.lpstrText, '/') == NULL) {
		mir_wstrncpy(tr.lpstrText, L"mailto:", 7);
		mir_wstrncpy(tr.lpstrText + 7, tr.lpstrText, tr.chrg.cpMax - tr.chrg.cpMin + 1);
	}
	return tr.lpstrText;
}

/////////////////////////////////////////////////////////////////////////////////////////
// generic command dispatcher
// used in various places (context menus, info panel menus etc.)

LRESULT Utils::CmdDispatcher(UINT uType, HWND hwndDlg, UINT cmd, WPARAM wParam, LPARAM lParam, TWindowData *dat, TContainerData *pContainer)
{
	switch (uType) {
	case CMD_CONTAINER:
		if (pContainer && hwndDlg)
			return(DM_ContainerCmdHandler(pContainer, cmd, wParam, lParam));
		break;

	case CMD_MSGDIALOG:
		if (pContainer && hwndDlg && dat)
			return(DM_MsgWindowCmdHandler(hwndDlg, pContainer, dat, cmd, wParam, lParam));
		break;

	case CMD_INFOPANEL:
		if (MsgWindowMenuHandler(dat, cmd, MENU_LOGMENU) == 0)
			return(DM_MsgWindowCmdHandler(hwndDlg, pContainer, dat, cmd, wParam, lParam));
		break;
	}
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////
// filters out invalid characters from a string used as part of a file
// or folder name. All invalid characters will be replaced by spaces.
//
// @param tszFilename - string to filter.

void Utils::sanitizeFilename(wchar_t* tszFilename)
{
	static wchar_t *forbiddenCharacters = L"%/\\':|\"<>?";
	static size_t forbiddenCharactersLen = mir_wstrlen(forbiddenCharacters);

	for (size_t i = 0; i < forbiddenCharactersLen; i++) {
		wchar_t*	szFound = 0;

		while ((szFound = wcschr(tszFilename, (int)forbiddenCharacters[i])) != NULL)
			*szFound = ' ';
	}
}

/////////////////////////////////////////////////////////////////////////////////////////
// ensure that a path name ends on a trailing backslash
// @param szPathname - pathname to check

void Utils::ensureTralingBackslash(wchar_t *szPathname)
{
	if (szPathname[mir_wstrlen(szPathname) - 1] != '\\')
		mir_wstrcat(szPathname, L"\\");
}

/////////////////////////////////////////////////////////////////////////////////////////
// load a system library from the Windows system path and return its module
// handle.
//
// return 0 and throw an exception if something goes wrong.

HMODULE Utils::loadSystemLibrary(const wchar_t* szFilename)
{
	wchar_t sysPathName[MAX_PATH + 2];
	if (0 == ::GetSystemDirectoryW(sysPathName, MAX_PATH))
		return 0;

	sysPathName[MAX_PATH - 1] = 0;
	if (mir_wstrlen(sysPathName) + mir_wstrlen(szFilename) >= MAX_PATH)
		return 0;

	mir_wstrcat(sysPathName, szFilename);
	HMODULE _h = LoadLibraryW(sysPathName);
	if (0 == _h)
		return 0;

	return _h;
}

/////////////////////////////////////////////////////////////////////////////////////////
// setting avatar's contact

void Utils::setAvatarContact(HWND hWnd, MCONTACT hContact)
{
	MCONTACT hSub = db_mc_getSrmmSub(hContact);
	SendMessage(hWnd, AVATAR_SETCONTACT, 0, (hSub) ? hSub : hContact);
}

/////////////////////////////////////////////////////////////////////////////////////////
// stub for copying data to clipboard

size_t Utils::CopyToClipBoard(const wchar_t *str, const HWND hwndOwner)
{
	if (!OpenClipboard(hwndOwner) || str == 0)
		return 0;

	size_t i = sizeof(wchar_t) * (mir_wstrlen(str) + 1);

	EmptyClipboard();
	HGLOBAL hData = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, i);

	memcpy((void*)GlobalLock(hData), str, i);
	GlobalUnlock(hData);
	SetClipboardData(CF_UNICODETEXT, hData);
	CloseClipboard();
	return i;
}

/////////////////////////////////////////////////////////////////////////////////////////
// file list handler

void Utils::AddToFileList(wchar_t ***pppFiles, int *totalCount, LPCTSTR szFilename)
{
	*pppFiles = (wchar_t**)mir_realloc(*pppFiles, (++*totalCount + 1) * sizeof(wchar_t*));
	(*pppFiles)[*totalCount] = NULL;
	(*pppFiles)[*totalCount - 1] = mir_wstrdup(szFilename);

	if (GetFileAttributes(szFilename) & FILE_ATTRIBUTE_DIRECTORY) {
		WIN32_FIND_DATA fd;
		wchar_t szPath[MAX_PATH];
		mir_wstrcpy(szPath, szFilename);
		mir_wstrcat(szPath, L"\\*");
		HANDLE hFind = FindFirstFile(szPath, &fd);
		if (hFind != INVALID_HANDLE_VALUE) {
			do {
				if (!mir_wstrcmp(fd.cFileName, L".") || !mir_wstrcmp(fd.cFileName, L".."))
					continue;
				mir_wstrcpy(szPath, szFilename);
				mir_wstrcat(szPath, L"\\");
				mir_wstrcat(szPath, fd.cFileName);
				AddToFileList(pppFiles, totalCount, szPath);
			} while (FindNextFile(hFind, &fd));
			FindClose(hFind);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////
// implementation of the CWarning class
//
// IMPORTANT note to translators for translation of the warning dialogs:
// 
//  Make sure to NOT remove the pipe character ( | ) from the strings. This separates the
//  warning title from the actual warning text.
// 
//  Also, do NOT insert multiple | characters in the translated string. Not well-formatted
//  warnings cannot be translated and the plugin will show the untranslated versions.
// 
//  strings marked with a NOT TRANSLATABLE comment cannot be translated at all. This
//  will be used for important and critical error messages only.
// 
//  some strings are empty, this is intentional and used for error messages that share
//  the message with other possible error notifications (popups, tool tips etc.)
// 
//  Entries that do not use the LPGENW() macro are NOT TRANSLATABLE, so don't bother translating them.

static wchar_t* warnings[] = {
	LPGENW("Important release notes|A test warning message"),							/* WARN_TEST */ /* reserved for important notes after upgrade - NOT translatable */
	LPGENW("Icon pack version check|The installed icon pack is outdated and might be incompatible with TabSRMM version 3.\n\n\\b1Missing or misplaced icons are possible issues with the currently installed icon pack.\\b0"),			/* WARN_ICONPACKVERSION */
	LPGENW("Edit user notes|You are editing the user notes. Click the button again or use the hotkey (default: Alt-N) to save the notes and return to normal messaging mode"),  /* WARN_EDITUSERNOTES */
	LPGENW("Missing component|The icon pack is missing. Please install it to the default icons folder.\n\nNo icons will be available"),		/* WARN_ICONPACKMISSING */
	LPGENW("Aero peek warning|You have enabled Aero Peek features and loaded a custom container window skin\n\nThis can result in minor visual anomalies in the live preview feature."),	/* WARN_AEROPEEKSKIN */
	LPGENW("File transfer problem|Sending the image by file transfer failed.\n\nPossible reasons: File transfers not supported, either you or the target contact is offline, or you are invisible and the target contact is not on your visibility list."), /* WARN_IMGSVC_MISSING */
	LPGENW("Settings problem|The option \\b1 History -> Imitate IEView API\\b0  is enabled and the History++ plugin is active. This can cause problems when using IEView as message log viewer.\n\nShould I correct the option (a restart is required)?"), /* WARN_HPP_APICHECK */
	L" ", /* WARN_NO_SENDLATER */ /*uses "Configuration issue|The unattended send feature is disabled. The \\b1 send later\\b0  and \\b1 send to multiple contacts\\b0  features depend on it.\n\nYou must enable it under \\b1Options -> Message sessions -> Advanced tweaks\\b0. Changing this option requires a restart." */
	LPGENW("Closing Window|You are about to close a window with multiple tabs open.\n\nProceed?"),		/* WARN_CLOSEWINDOW */
	LPGENW("Closing options dialog|To reflect the changes done by importing a theme in the options dialog, the dialog must be closed after loading a theme \\b1 and unsaved changes might be lost\\b0 .\n\nDo you want to continue?"), /* WARN_OPTION_CLOSE */
	LPGENW("Loading a theme|Loading a color and font theme can overwrite the settings defined by your skin.\n\nDo you want to continue?"), /* WARN_THEME_OVERWRITE */
};

CWarning::CWarning(const wchar_t *tszTitle, const wchar_t *tszText, const UINT uId, const DWORD dwFlags) :
	m_szTitle(mir_wstrdup(tszTitle)),
	m_szText(mir_wstrdup(tszText))
{
	m_uId = uId;
	m_hFontCaption = 0;
	m_dwFlags = dwFlags;

	m_fIsModal = ((m_dwFlags & MB_YESNO || m_dwFlags & MB_YESNOCANCEL) ? true : false);
}

CWarning::~CWarning()
{
	if (m_hFontCaption)
		::DeleteObject(m_hFontCaption);
}

LRESULT CWarning::ShowDialog() const
{
	if (!m_fIsModal) {
		::CreateDialogParam(g_hInst, MAKEINTRESOURCE(IDD_WARNING), 0, stubDlgProc, LPARAM(this));
		return 0;
	}

	return ::DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_WARNING), 0, stubDlgProc, LPARAM(this));
}

__int64 CWarning::getMask()
{
	__int64 mask = 0;

	DWORD	dwLow = M.GetDword("cWarningsL", 0);
	DWORD	dwHigh = M.GetDword("cWarningsH", 0);

	mask = ((((__int64)dwHigh) << 32) & 0xffffffff00000000) | dwLow;
	return(mask);
}

/////////////////////////////////////////////////////////////////////////////////////////
// send cancel message to all open warning dialogs so they are destroyed
// before TabSRMM is unloaded.
// 
// called by the OkToExit handler in globals.cpp

void CWarning::destroyAll()
{
	if (hWindowList)
		WindowList_Broadcast(hWindowList, WM_COMMAND, MAKEWPARAM(IDCANCEL, 0), 0);
}

/////////////////////////////////////////////////////////////////////////////////////////
// show a CWarning dialog using the id value. Check whether the user has chosen to
// not show this message again. This has room for 64 different warning dialogs, which
// should be enough in the first place. Extending it should not be too hard though.

LRESULT CWarning::show(const int uId, DWORD dwFlags, const wchar_t* tszTxt)
{
	wchar_t*	separator_pos = 0;
	__int64 	mask = 0, val = 0;
	LRESULT 	result = 0;
	wchar_t*	_s = 0;

	if (0 == hWindowList)
		hWindowList = WindowList_Create();

	// don't open new warnings when shutdown was initiated (modal ones will otherwise
	// block the shutdown)
	if (CMimAPI::m_shutDown)
		return -1;

	if (tszTxt)
		_s = const_cast<wchar_t *>(tszTxt);
	else {
		if (uId != -1) {
			if (dwFlags & CWF_UNTRANSLATED)
				_s = TranslateW(warnings[uId]);
			else {
				// revert to untranslated warning when the translated message
				// is not well-formatted.
				_s = TranslateW(warnings[uId]);

				if (mir_wstrlen(_s) < 3 || 0 == wcschr(_s, '|'))
					_s = TranslateW(warnings[uId]);
			}
		}
		else
			return -1;
	}

	if ((mir_wstrlen(_s) > 3) && ((separator_pos = wcschr(_s, '|')) != 0)) {
		if (uId >= 0) {
			mask = getMask();
			val = ((__int64)1L) << uId;
		}
		else mask = val = 0;

		if (0 == (mask & val) || dwFlags & CWF_NOALLOWHIDE) {
			wchar_t *s = reinterpret_cast<wchar_t *>(mir_alloc((mir_wstrlen(_s) + 1) * 2));
			mir_wstrcpy(s, _s);
			separator_pos = wcschr(s, '|');

			if (separator_pos) {
				separator_pos[0] = 0;

				CWarning *w = new CWarning(s, &separator_pos[1], uId, dwFlags);
				if (!(dwFlags & MB_YESNO || dwFlags & MB_YESNOCANCEL)) {
					w->ShowDialog();
					mir_free(s);
				}
				else {
					result = w->ShowDialog();
					mir_free(s);
					return(result);
				}
			}
			else
				mir_free(s);
		}
	}
	return -1;
}

/////////////////////////////////////////////////////////////////////////////////////////
// stub dlg procedure.Just register the object pointer in WM_INITDIALOG

INT_PTR CALLBACK CWarning::stubDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	CWarning	*w = reinterpret_cast<CWarning *>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
	if (w)
		return(w->dlgProc(hwnd, msg, wParam, lParam));

	switch (msg) {
	case WM_INITDIALOG:
		w = reinterpret_cast<CWarning *>(lParam);
		if (w) {
			::SetWindowLongPtr(hwnd, GWLP_USERDATA, lParam);
			return(w->dlgProc(hwnd, msg, wParam, lParam));
		}
		break;

	#if defined(__LOGDEBUG_)
	case WM_NCDESTROY:
		_DebugTraceW(L"window destroyed");
		break;
	#endif
	}
	return FALSE;
}

/////////////////////////////////////////////////////////////////////////////////////////
// dialog procedure for the warning dialog box

INT_PTR CALLBACK CWarning::dlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_INITDIALOG:
		{
			HICON		hIcon = 0;
			UINT		uResId = 0;
			wchar_t		temp[1024];
			SETTEXTEX	stx = { ST_SELECTION, CP_UTF8 };
			size_t		pos = 0;

			m_hwnd = hwnd;

			::SetWindowTextW(hwnd, TranslateT("TabSRMM warning message"));
			::Window_SetSkinIcon_IcoLib(hwnd, SKINICON_OTHER_MIRANDA);
			::SendDlgItemMessage(hwnd, IDC_WARNTEXT, EM_AUTOURLDETECT, TRUE, 0);
			::SendDlgItemMessage(hwnd, IDC_WARNTEXT, EM_SETEVENTMASK, 0, ENM_LINK);

			mir_snwprintf(temp, RTF_DEFAULT_HEADER, 0, 0, 0, 30 * 15);
			tstring *str = new tstring(temp);

			str->append(m_szText);
			str->append(L"}");

			TranslateDialogDefault(hwnd);

			/*
			* convert normal line breaks to rtf
			*/
			while ((pos = str->find(L"\n")) != str->npos) {
				str->erase(pos, 1);
				str->insert(pos, L"\\line ");
			}

			::SendDlgItemMessage(hwnd, IDC_WARNTEXT, EM_SETTEXTEX, (WPARAM)&stx, T2Utf(str->c_str()));
			delete str;

			::SetDlgItemTextW(hwnd, IDC_CAPTION, m_szTitle);

			if (m_dwFlags & CWF_NOALLOWHIDE)
				Utils::showDlgControl(hwnd, IDC_DONTSHOWAGAIN, SW_HIDE);
			if (m_dwFlags & MB_YESNO || m_dwFlags & MB_YESNOCANCEL) {
				Utils::showDlgControl(hwnd, IDOK, SW_HIDE);
				::SetFocus(::GetDlgItem(hwnd, IDCANCEL));
			}
			else {
				Utils::showDlgControl(hwnd, IDCANCEL, SW_HIDE);
				Utils::showDlgControl(hwnd, IDYES, SW_HIDE);
				Utils::showDlgControl(hwnd, IDNO, SW_HIDE);
				::SetFocus(::GetDlgItem(hwnd, IDOK));
			}
			if (m_dwFlags & MB_ICONERROR || m_dwFlags & MB_ICONHAND)
				uResId = 32513;
			else if (m_dwFlags & MB_ICONEXCLAMATION || m_dwFlags & MB_ICONWARNING)
				uResId = 32515;
			else if (m_dwFlags & MB_ICONASTERISK || m_dwFlags & MB_ICONINFORMATION)
				uResId = 32516;
			else if (m_dwFlags & MB_ICONQUESTION)
				uResId = 32514;

			if (uResId)
				hIcon = reinterpret_cast<HICON>(::LoadImage(0, MAKEINTRESOURCE(uResId), IMAGE_ICON, 0, 0, LR_SHARED | LR_DEFAULTSIZE));
			else
				hIcon = ::Skin_LoadIcon(SKINICON_EVENT_MESSAGE, true);

			::SendDlgItemMessageW(hwnd, IDC_WARNICON, STM_SETICON, reinterpret_cast<WPARAM>(hIcon), 0);
			if (!(m_dwFlags & MB_YESNO || m_dwFlags & MB_YESNOCANCEL))
				::ShowWindow(hwnd, SW_SHOWNORMAL);

			WindowList_Add(hWindowList, hwnd, (UINT_PTR)hwnd);
		}
		return TRUE;

	case WM_CTLCOLORSTATIC:
		{
			HWND hwndChild = reinterpret_cast<HWND>(lParam);
			UINT id = ::GetDlgCtrlID(hwndChild);
			if (0 == m_hFontCaption) {
				HFONT hFont = reinterpret_cast<HFONT>(::SendDlgItemMessage(hwnd, IDC_CAPTION, WM_GETFONT, 0, 0));
				LOGFONT lf = { 0 };

				::GetObject(hFont, sizeof(lf), &lf);
				lf.lfHeight = (int)((double)lf.lfHeight * 1.7f);
				m_hFontCaption = ::CreateFontIndirect(&lf);
				::SendDlgItemMessage(hwnd, IDC_CAPTION, WM_SETFONT, (WPARAM)m_hFontCaption, FALSE);
			}

			if (IDC_CAPTION == id) {
				::SetTextColor(reinterpret_cast<HDC>(wParam), ::GetSysColor(COLOR_HIGHLIGHT));
				::SendMessage(hwndChild, WM_SETFONT, (WPARAM)m_hFontCaption, FALSE);
			}

			if (IDC_WARNGROUP != id && IDC_DONTSHOWAGAIN != id) {
				::SetBkColor((HDC)wParam, ::GetSysColor(COLOR_WINDOW));
				return reinterpret_cast<INT_PTR>(::GetSysColorBrush(COLOR_WINDOW));
			}
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
		case IDCANCEL:
		case IDYES:
		case IDNO:
			if (!m_fIsModal && (IDOK == LOWORD(wParam) || IDCANCEL == LOWORD(wParam))) {		// modeless dialogs can receive a IDCANCEL from destroyAll()
				::SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
				delete this;
				WindowList_Remove(hWindowList, hwnd);
				::DestroyWindow(hwnd);
			}
			else {
				::SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
				delete this;
				WindowList_Remove(hWindowList, hwnd);
				::EndDialog(hwnd, LOWORD(wParam));
			}
			break;

		case IDC_DONTSHOWAGAIN:
			__int64 mask = getMask(), val64 = ((__int64)1L << m_uId), newVal = 0;
			newVal = mask | val64;

			if (::IsDlgButtonChecked(hwnd, IDC_DONTSHOWAGAIN)) {
				DWORD val = (DWORD)(newVal & 0x00000000ffffffff);
				db_set_dw(0, SRMSGMOD_T, "cWarningsL", val);
				val = (DWORD)((newVal >> 32) & 0x00000000ffffffff);
				db_set_dw(0, SRMSGMOD_T, "cWarningsH", val);
			}
		}
		break;

	case WM_NOTIFY:
		switch (((NMHDR*)lParam)->code) {
		case EN_LINK:
			switch (((ENLINK*)lParam)->msg) {
			case WM_LBUTTONUP:
				ENLINK *e = reinterpret_cast<ENLINK *>(lParam);

				const wchar_t *wszUrl = Utils::extractURLFromRichEdit(e, ::GetDlgItem(hwnd, IDC_WARNTEXT));
				if (wszUrl) {
					Utils_OpenUrlW(wszUrl);
					mir_free(const_cast<wchar_t *>(wszUrl));
				}
			}
		}
		break;
	}

	return FALSE;
}
