/////////////////////////////////////////////////////////////////////////////////////////
// Miranda NG: the free IM client for Microsoft* Windows*
//
// Copyright (�) 2012-16 Miranda NG project,
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
// Implements a queued, asynchronous sending system for tabSRMM.

#include "stdafx.h"

SendQueue *sendQueue = 0;

/////////////////////////////////////////////////////////////////////////////////////////
// searches the queue for a message belonging to the given contact which has been marked
// as "failed" by either the ACKRESULT_FAILED or a timeout handler
// returns: zero-based queue index or -1 if none was found

int SendQueue::findNextFailed(const TWindowData *dat) const
{
	if (dat)
		for (int i = 0; i < NR_SENDJOBS; i++)
			if (m_jobs[i].hContact == dat->hContact && m_jobs[i].iStatus == SQ_ERROR)
				return i;

	return -1;
}

void SendQueue::handleError(TWindowData *dat, const int iEntry) const
{
	if (!dat) return;

	wchar_t szErrorMsg[500];

	dat->iCurrentQueueError = iEntry;
	wcsncpy_s(szErrorMsg, m_jobs[iEntry].szErrorMsg, _TRUNCATE);
	logError(dat, iEntry, szErrorMsg);
	recallFailed(dat, iEntry);
	showErrorControls(dat, TRUE);
	::HandleIconFeedback(dat, PluginConfig.g_iconErr);
}

/////////////////////////////////////////////////////////////////////////////////////////
//add a message to the sending queue.
// iLen = required size of the memory block to hold the message

int SendQueue::addTo(TWindowData *dat, size_t iLen, int dwFlags)
{
	int i;
	int iFound = NR_SENDJOBS;

	if (m_currentIndex >= NR_SENDJOBS) {
		_DebugPopup(dat->hContact, L"Send queue full");
		return 0;
	}

	// find a mir_free entry in the send queue...
	for (i = 0; i < NR_SENDJOBS; i++) {
		if (m_jobs[i].hContact != 0 || m_jobs[i].iStatus != 0) {
			// this entry is used, check if it's orphaned and can be removed...
			if (m_jobs[i].hOwnerWnd && IsWindow(m_jobs[i].hOwnerWnd)) // window exists, do not reuse it
				continue;
			if (time(NULL) - m_jobs[i].dwTime < 120) // non-acked entry, but not old enough, don't re-use it
				continue;
			clearJob(i);
			iFound = i;
			goto entry_found;
		}
		iFound = i;
		break;
	}
entry_found:
	if (iFound == NR_SENDJOBS) {
		_DebugPopup(dat->hContact, L"Send queue full");
		return 0;
	}

	SendJob &job = m_jobs[iFound];
	job.szSendBuffer = (char*)mir_alloc(iLen);
	memcpy(job.szSendBuffer, dat->sendBuffer, iLen);

	job.dwFlags = dwFlags;
	job.dwTime = time(NULL);

	HWND	hwndDlg = dat->hwnd;

	dat->cache->saveHistory(0, 0);
	::SetDlgItemText(hwndDlg, IDC_MESSAGE, L"");
	::SetFocus(GetDlgItem(hwndDlg, IDC_MESSAGE));

	UpdateSaveAndSendButton(dat);
	sendQueued(dat, iFound);
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////
// threshold for word - wrapping when sending messages in chunks

#define SPLIT_WORD_CUTOFF 20

static void DoSplitSendA(LPVOID param)
{
	Thread_SetName("TabSRMM: DoSplitSendA");

	SendJob *job = sendQueue->getJobByIndex((INT_PTR)param);

	size_t iLen = mir_strlen(job->szSendBuffer);
	ptrA   szBegin((char*)mir_alloc(iLen + 1));
	char  *szTemp = szBegin;
	memcpy(szTemp, job->szSendBuffer, iLen + 1);

	bool fFirstSend = false, fSplitting = true;
	size_t iCur = 0;
	do {
		iCur += job->chunkSize;
		if (iCur > iLen)
			fSplitting = FALSE;

		if (fSplitting) {
			job->iAcksNeeded++;

			char *szSaved = &szBegin[iCur];
			size_t iSavedCur = iCur;
			for (int i = 0; iCur; i++, iCur--) {
				if (szBegin[iCur] == ' ') {
					szSaved = &szBegin[iCur];
					break;
				}
				if (i == SPLIT_WORD_CUTOFF) {
					iCur = iSavedCur;
					szSaved = &szBegin[iCur];
					break;
				}
			}

			char savedChar = *szSaved;
			*szSaved = 0;
			int id = ProtoChainSend(job->hContact, PSS_MESSAGE, job->dwFlags, (LPARAM)szTemp);
			if (!fFirstSend) {
				job->hSendId = (HANDLE)id;
				fFirstSend = TRUE;
				PostMessage(PluginConfig.g_hwndHotkeyHandler, DM_SPLITSENDACK, (WPARAM)param, 0);
			}
			*szSaved = savedChar;
			szTemp = szSaved;
			if (savedChar == ' ') {
				szTemp++;
				iCur++;
			}
		}
		else {
			int id = ProtoChainSend(job->hContact, PSS_MESSAGE, job->dwFlags, (LPARAM)szTemp);
			if (!fFirstSend) {
				job->hSendId = (HANDLE)id;
				fFirstSend = TRUE;
				PostMessage(PluginConfig.g_hwndHotkeyHandler, DM_SPLITSENDACK, (WPARAM)param, 0);
			}
		}
		Sleep(500L);
	}
		while (fSplitting);
}

/////////////////////////////////////////////////////////////////////////////////////////
// return effective length of the message in bytes (utf-8 encoded)

size_t SendQueue::getSendLength(const int iEntry)
{
	SendJob &p = m_jobs[iEntry];
	p.iSendLength = mir_strlen(p.szSendBuffer);
	return p.iSendLength;
}

int SendQueue::sendQueued(TWindowData *dat, const int iEntry)
{
	HWND hwndDlg = dat->hwnd;
	CContactCache *ccActive = CContactCache::getContactCache(dat->hContact);

	if (dat->sendMode & SMODE_MULTIPLE) {
		int iJobs = 0;
		size_t iMinLength = 0;

		m_jobs[iEntry].iStatus = SQ_INPROGRESS;
		m_jobs[iEntry].hContact = ccActive->getActiveContact();
		m_jobs[iEntry].hOwnerWnd = hwndDlg;

		size_t iSendLength = getSendLength(iEntry);

		for (MCONTACT hContact = db_find_first(); hContact; hContact = db_find_next(hContact)) {
			HANDLE hItem = (HANDLE)SendDlgItemMessage(hwndDlg, IDC_CLIST, CLM_FINDCONTACT, hContact, 0);
			if (hItem && SendDlgItemMessage(hwndDlg, IDC_CLIST, CLM_GETCHECKMARK, (WPARAM)hItem, 0)) {
				CContactCache *c = CContactCache::getContactCache(hContact);
				iMinLength = (iMinLength == 0 ? c->getMaxMessageLength() : min(c->getMaxMessageLength(), iMinLength));
			}
		}

		if (iSendLength >= iMinLength) {
			wchar_t	tszError[256];
			mir_snwprintf(tszError, TranslateT("The message cannot be sent delayed or to multiple contacts, because it exceeds the maximum allowed message length of %d bytes"), iMinLength);
			::SendMessage(dat->hwnd, DM_ACTIVATETOOLTIP, IDC_MESSAGE, LPARAM(tszError));
			sendQueue->clearJob(iEntry);
			return 0;
		}

		for (MCONTACT hContact = db_find_first(); hContact; hContact = db_find_next(hContact)) {
			HANDLE hItem = (HANDLE)SendDlgItemMessage(hwndDlg, IDC_CLIST, CLM_FINDCONTACT, hContact, 0);
			if (hItem && SendDlgItemMessage(hwndDlg, IDC_CLIST, CLM_GETCHECKMARK, (WPARAM)hItem, 0)) {
				doSendLater(iEntry, 0, hContact, false);
				iJobs++;
			}
		}

		sendQueue->clearJob(iEntry);
		if (iJobs)
			sendLater->flushQueue(); // force queue processing
		return 0;
	}

	if (dat->hContact == NULL)
		return 0;  //never happens

	dat->nMax = (int)dat->cache->getMaxMessageLength(); // refresh length info

	if (M.GetByte("autosplit", 0) && !(dat->sendMode & SMODE_SENDLATER)) {
		// determine send buffer length
		BOOL fSplit = FALSE;
		if ((int)getSendLength(iEntry) >= dat->nMax)
			fSplit = true;

		if (!fSplit)
			goto send_unsplitted;

		m_jobs[iEntry].hContact = ccActive->getActiveContact();
		m_jobs[iEntry].hOwnerWnd = hwndDlg;
		m_jobs[iEntry].iStatus = SQ_INPROGRESS;
		m_jobs[iEntry].iAcksNeeded = 1;
		m_jobs[iEntry].chunkSize = dat->nMax;

		DWORD dwOldFlags = m_jobs[iEntry].dwFlags;
		mir_forkthread(DoSplitSendA, (LPVOID)iEntry);
		m_jobs[iEntry].dwFlags = dwOldFlags;
	}
	else {
	send_unsplitted:
		m_jobs[iEntry].hContact = ccActive->getActiveContact();
		m_jobs[iEntry].hOwnerWnd = hwndDlg;
		m_jobs[iEntry].iStatus = SQ_INPROGRESS;
		m_jobs[iEntry].iAcksNeeded = 1;
		if (dat->sendMode & SMODE_SENDLATER) {
			wchar_t	tszError[256];

			size_t iSendLength = getSendLength(iEntry);
			if ((int)iSendLength >= dat->nMax) {
				mir_snwprintf(tszError, TranslateT("The message cannot be sent delayed or to multiple contacts, because it exceeds the maximum allowed message length of %d bytes"), dat->nMax);
				SendMessage(dat->hwnd, DM_ACTIVATETOOLTIP, IDC_MESSAGE, LPARAM(tszError));
				clearJob(iEntry);
				return 0;
			}
			doSendLater(iEntry, dat);
			clearJob(iEntry);
			return 0;
		}
		m_jobs[iEntry].hSendId = (HANDLE)ProtoChainSend(dat->hContact, PSS_MESSAGE, m_jobs[iEntry].dwFlags, (LPARAM)m_jobs[iEntry].szSendBuffer);

		if (dat->sendMode & SMODE_NOACK) {              // fake the ack if we are not interested in receiving real acks
			ACKDATA ack = { 0 };
			ack.hContact = dat->hContact;
			ack.hProcess = m_jobs[iEntry].hSendId;
			ack.type = ACKTYPE_MESSAGE;
			ack.result = ACKRESULT_SUCCESS;
			SendMessage(hwndDlg, HM_EVENTSENT, (WPARAM)MAKELONG(iEntry, 0), (LPARAM)&ack);
		}
		else SetTimer(hwndDlg, TIMERID_MSGSEND + iEntry, PluginConfig.m_MsgTimeout, NULL);
	}

	dat->iOpenJobs++;
	m_currentIndex++;

	// give icon feedback...
	if (dat->pContainer->hwndActive == hwndDlg)
		::UpdateReadChars(dat);

	if (!(dat->sendMode & SMODE_NOACK))
		::HandleIconFeedback(dat, PluginConfig.g_IconSend);

	if (M.GetByte(SRMSGSET_AUTOMIN, SRMSGDEFSET_AUTOMIN))
		::SendMessage(dat->pContainer->hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
	return 0;
}

void SendQueue::clearJob(const int iIndex)
{
	SendJob &job = m_jobs[iIndex];
	mir_free(job.szSendBuffer);
	memset(&job, 0, sizeof(SendJob));
}

/////////////////////////////////////////////////////////////////////////////////////////
// this is called when :
//
// ) a delivery has completed successfully
// ) user decided to cancel a failed send
// it removes the completed / canceled send job from the queue and schedules the next job to send (if any)

void SendQueue::checkQueue(const TWindowData *dat) const
{
	if (dat) {
		HWND	hwndDlg = dat->hwnd;

		if (dat->iOpenJobs == 0)
			::HandleIconFeedback(const_cast<TWindowData *>(dat), (HICON)INVALID_HANDLE_VALUE);
		else if (!(dat->sendMode & SMODE_NOACK))
			::HandleIconFeedback(const_cast<TWindowData *>(dat), PluginConfig.g_IconSend);

		if (dat->pContainer->hwndActive == hwndDlg)
			::UpdateReadChars(const_cast<TWindowData *>(dat));
	}
}

/////////////////////////////////////////////////////////////////////////////////////////
// logs an error message to the message window.Optionally, appends the original message
// from the given sendJob (queue index)

void SendQueue::logError(const TWindowData *dat, int iSendJobIndex, const wchar_t *szErrMsg) const
{
	if (dat == 0)
		return;

	size_t iMsgLen;
	DBEVENTINFO	dbei = { sizeof(dbei) };
	dbei.eventType = EVENTTYPE_ERRMSG;
	if (iSendJobIndex >= 0) {
		dbei.pBlob = (BYTE *)m_jobs[iSendJobIndex].szSendBuffer;
		iMsgLen = mir_strlen(m_jobs[iSendJobIndex].szSendBuffer) + 1;
	}
	else {
		iMsgLen = 0;
		dbei.pBlob = NULL;
	}

	dbei.flags = DBEF_UTF;
	dbei.cbBlob = (int)iMsgLen;
	dbei.timestamp = time(NULL);
	dbei.szModule = (char *)szErrMsg;
	StreamInEvents(dat->hwnd, NULL, 1, 1, &dbei);
}

/////////////////////////////////////////////////////////////////////////////////////////
// enable or disable the sending controls in the given window
// ) input area
// ) multisend contact list instance
// ) send button

void SendQueue::EnableSending(const TWindowData *dat, const int iMode)
{
	if (dat) {
		HWND hwndDlg = dat->hwnd;
		::SendDlgItemMessage(hwndDlg, IDC_MESSAGE, EM_SETREADONLY, (WPARAM)iMode ? FALSE : TRUE, 0);
		Utils::enableDlgControl(hwndDlg, IDC_CLIST, iMode ? TRUE : FALSE);
		::EnableSendButton(dat, iMode);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////
// show or hide the error control button bar on top of the window

void SendQueue::showErrorControls(TWindowData *dat, const int showCmd) const
{
	UINT	myerrorControls[] = { IDC_STATICERRORICON, IDC_STATICTEXT, IDC_RETRY, IDC_CANCELSEND, IDC_MSGSENDLATER };
	HWND	hwndDlg = dat->hwnd;

	if (showCmd) {
		TCITEM item = { 0 };
		dat->hTabIcon = PluginConfig.g_iconErr;
		item.mask = TCIF_IMAGE;
		item.iImage = 0;
		TabCtrl_SetItem(GetDlgItem(dat->pContainer->hwnd, IDC_MSGTABS), dat->iTabID, &item);
		dat->dwFlags |= MWF_ERRORSTATE;
	}
	else {
		dat->dwFlags &= ~MWF_ERRORSTATE;
		dat->hTabIcon = dat->hTabStatusIcon;
	}

	for (int i = 0; i < 5; i++)
		if (IsWindow(GetDlgItem(hwndDlg, myerrorControls[i])))
			Utils::showDlgControl(hwndDlg, myerrorControls[i], showCmd ? SW_SHOW : SW_HIDE);

	SendMessage(hwndDlg, WM_SIZE, 0, 0);
	DM_ScrollToBottom(dat, 0, 1);
	if (m_jobs[0].hContact != 0)
		EnableSending(dat, TRUE);
}

void SendQueue::recallFailed(const TWindowData *dat, int iEntry) const
{
	if (dat == NULL)
		return;

	int iLen = GetWindowTextLength(GetDlgItem(dat->hwnd, IDC_MESSAGE));
	NotifyDeliveryFailure(dat);
	if (iLen != 0)
		return;

	// message area is empty, so we can recall the failed message...
	SETTEXTEX stx = { ST_DEFAULT, CP_UTF8 };
	SendDlgItemMessage(dat->hwnd, IDC_MESSAGE, EM_SETTEXTEX, (WPARAM)&stx, (LPARAM)m_jobs[iEntry].szSendBuffer);
	UpdateSaveAndSendButton(const_cast<TWindowData *>(dat));
	SendDlgItemMessage(dat->hwnd, IDC_MESSAGE, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
}

void SendQueue::UpdateSaveAndSendButton(TWindowData *dat)
{
	if (dat) {
		HWND hwndDlg = dat->hwnd;

		GETTEXTLENGTHEX gtxl = { 0 };
		gtxl.codepage = CP_UTF8;
		gtxl.flags = GTL_DEFAULT | GTL_PRECISE | GTL_NUMBYTES;

		int len = SendDlgItemMessage(hwndDlg, IDC_MESSAGE, EM_GETTEXTLENGTHEX, (WPARAM)&gtxl, 0);
		if (len && GetSendButtonState(hwndDlg) == PBS_DISABLED)
			EnableSendButton(dat, TRUE);
		else if (len == 0 && GetSendButtonState(hwndDlg) != PBS_DISABLED)
			EnableSendButton(dat, FALSE);

		if (len) {          // looks complex but avoids flickering on the button while typing.
			if (!(dat->dwFlags & MWF_SAVEBTN_SAV)) {
				SendDlgItemMessage(hwndDlg, IDC_SAVE, BM_SETIMAGE, IMAGE_ICON, (LPARAM)PluginConfig.g_buttonBarIcons[ICON_BUTTON_SAVE]);
				SendDlgItemMessage(hwndDlg, IDC_SAVE, BUTTONADDTOOLTIP, (WPARAM)pszIDCSAVE_save, BATF_UNICODE);
				dat->dwFlags |= MWF_SAVEBTN_SAV;
			}
		}
		else {
			SendDlgItemMessage(hwndDlg, IDC_SAVE, BM_SETIMAGE, IMAGE_ICON, (LPARAM)PluginConfig.g_buttonBarIcons[ICON_BUTTON_CANCEL]);
			SendDlgItemMessage(hwndDlg, IDC_SAVE, BUTTONADDTOOLTIP, (WPARAM)pszIDCSAVE_close, BATF_UNICODE);
			dat->dwFlags &= ~MWF_SAVEBTN_SAV;
		}
		dat->textLen = len;
	}
}

void SendQueue::NotifyDeliveryFailure(const TWindowData *dat)
{
	if (M.GetByte("adv_noErrorPopups", 0))
		return;

	if (CallService(MS_POPUP_QUERY, PUQS_GETSTATUS, 0) != 1)
		return;

	POPUPDATAT ppd = { 0 };
	ppd.lchContact = dat->hContact;
	wcsncpy_s(ppd.lptzContactName, dat->cache->getNick(), _TRUNCATE);
	wcsncpy_s(ppd.lptzText, TranslateT("A message delivery has failed.\nClick to open the message window."), _TRUNCATE);

	if (!(BOOL)M.GetByte(MODULE, OPT_COLDEFAULT_ERR, TRUE)) {
		ppd.colorText = (COLORREF)M.GetDword(MODULE, OPT_COLTEXT_ERR, DEFAULT_COLTEXT);
		ppd.colorBack = (COLORREF)M.GetDword(MODULE, OPT_COLBACK_ERR, DEFAULT_COLBACK);
	}
	else ppd.colorText = ppd.colorBack = 0;

	ppd.PluginWindowProc = Utils::PopupDlgProcError;
	ppd.lchIcon = PluginConfig.g_iconErr;
	ppd.PluginData = 0;
	ppd.iSeconds = (int)M.GetDword(MODULE, OPT_DELAY_ERR, (DWORD)DEFAULT_DELAY);
	PUAddPopupT(&ppd);
}

/////////////////////////////////////////////////////////////////////////////////////////
// searches string for characters typical for RTL text(hebrew and other RTL languages

int SendQueue::RTL_Detect(const WCHAR *pszwText)
{
	size_t iLen = mir_wstrlen(pszwText);

	WORD *infoTypeC2 = (WORD*)mir_calloc(sizeof(WORD) * (iLen + 2));
	if (infoTypeC2 == NULL)
		return 0;

	GetStringTypeW(CT_CTYPE2, pszwText, (int)iLen, infoTypeC2);

	int n = 0;
	for (size_t i = 0; i < iLen; i++)
		if (infoTypeC2[i] == C2_RIGHTTOLEFT)
			n++;

	mir_free(infoTypeC2);
	return(n >= 2 ? 1 : 0);
}

int SendQueue::ackMessage(TWindowData *dat, WPARAM wParam, LPARAM lParam)
{
	ACKDATA *ack = (ACKDATA *)lParam;

	TContainerData *m_pContainer = 0;
	if (dat)
		m_pContainer = dat->pContainer;

	int iFound = (int)(LOWORD(wParam));
	SendJob &job = m_jobs[iFound];

	if (job.iStatus == SQ_ERROR) { // received ack for a job which is already in error state...
		if (dat) {                  // window still open
			if (dat->iCurrentQueueError == iFound) {
				dat->iCurrentQueueError = -1;
				showErrorControls(dat, FALSE);
			}
		}
		// we must discard this job, because there is no message window open to handle the
		// error properly. But we display a tray notification to inform the user about the problem.
		else goto inform_and_discard;
	}

	// failed acks are only handled when the window is still open. with no window open, they will be *silently* discarded

	if (ack->result == ACKRESULT_FAILED) {
		if (dat) {
			// "hard" errors are handled differently in multisend. There is no option to retry - once failed, they
			// are discarded and the user is notified with a small log message.
			if (!nen_options.iNoSounds && !(m_pContainer->dwFlags & CNT_NOSOUND))
				SkinPlaySound("SendError");

			mir_snwprintf(job.szErrorMsg, TranslateT("Delivery failure: %s"), _A2T((char *)ack->lParam));
			job.iStatus = SQ_ERROR;
			KillTimer(dat->hwnd, TIMERID_MSGSEND + iFound);
			if (!(dat->dwFlags & MWF_ERRORSTATE))
				handleError(dat, iFound);
			return 0;
		}

	inform_and_discard:
		_DebugPopup(job.hContact, TranslateT("A message delivery has failed after the contacts chat window was closed. You may want to resend the last message"));
		clearJob(iFound);
		return 0;
	}

	DBEVENTINFO dbei = { sizeof(dbei) };
	dbei.eventType = EVENTTYPE_MESSAGE;
	dbei.flags = DBEF_SENT;
	dbei.szModule = GetContactProto(job.hContact);
	dbei.timestamp = time(NULL);
	dbei.cbBlob = (int)mir_strlen(job.szSendBuffer) + 1;

	if (dat)
		dat->cache->updateStats(TSessionStats::BYTES_SENT, dbei.cbBlob - 1);
	else {
		CContactCache *cc = CContactCache::getContactCache(job.hContact);
		cc->updateStats(TSessionStats::BYTES_SENT, dbei.cbBlob - 1);
	}

	if (job.dwFlags & PREF_RTL)
		dbei.flags |= DBEF_RTL;
	dbei.flags |= DBEF_UTF;
	dbei.pBlob = (PBYTE)job.szSendBuffer;

	MessageWindowEvent evt = { sizeof(evt), (INT_PTR)job.hSendId, job.hContact, &dbei };
	NotifyEventHooks(PluginConfig.m_event_WriteEvent, 0, (LPARAM)&evt);

	job.szSendBuffer = (char*)dbei.pBlob;
	MEVENT hNewEvent = db_event_add(job.hContact, &dbei);

	if (m_pContainer)
		if (!nen_options.iNoSounds && !(m_pContainer->dwFlags & CNT_NOSOUND))
			SkinPlaySound("SendMsg");

	M.BroadcastMessage(DM_APPENDMCEVENT, job.hContact, LPARAM(hNewEvent));

	job.hSendId = NULL;
	job.iAcksNeeded--;

	if (job.iAcksNeeded == 0) {              // everything sent
		clearJob(iFound);
		if (dat) {
			KillTimer(dat->hwnd, TIMERID_MSGSEND + iFound);
			dat->iOpenJobs--;
		}
		m_currentIndex--;
	}
	if (dat) {
		checkQueue(dat);

		int iNextFailed = findNextFailed(dat);
		if (iNextFailed >= 0 && !(dat->dwFlags & MWF_ERRORSTATE))
			handleError(dat, iNextFailed);
		else {
			if (M.GetByte("AutoClose", 0)) {
				if (M.GetByte("adv_AutoClose_2", 0))
					SendMessage(dat->hwnd, WM_CLOSE, 0, 1);
				else
					SendMessage(dat->pContainer->hwnd, WM_CLOSE, 0, 0);
			}
		}
	}
	return 0;
}

LRESULT SendQueue::WarnPendingJobs(unsigned int)
{
	return MessageBox(0,
		TranslateT("There are unsent messages waiting for confirmation.\nIf you close the window now, Miranda will try to send them but may be unable to inform you about possible delivery errors.\nDo you really want to close the window(s)?"),
		TranslateT("Message window warning"), MB_YESNO | MB_ICONHAND);
}

/////////////////////////////////////////////////////////////////////////////////////////
// This just adds the message to the database for later delivery and
// adds the contact to the list of contacts that have queued messages
//
// @param iJobIndex int: index of the send job
// dat: Message window data
// fAddHeader: add the "message was sent delayed" header (default = true)
// hContact  : contact to which the job should be added (default = hOwner of the send job)
//
// @return the index on success, -1 on failure

int SendQueue::doSendLater(int iJobIndex, TWindowData *dat, MCONTACT hContact, bool fIsSendLater)
{
	bool  fAvail = sendLater->isAvail();

	const wchar_t *szNote = 0;

	if (fIsSendLater && dat) {
		if (fAvail)
			szNote = TranslateT("Message successfully queued for later delivery.\nIt will be sent as soon as possible and a popup will inform you about the result.");
		else
			szNote = TranslateT("The send later feature is not available on this protocol.");

		T2Utf utfText(szNote);
		DBEVENTINFO dbei;
		dbei.cbSize = sizeof(dbei);
		dbei.eventType = EVENTTYPE_MESSAGE;
		dbei.flags = DBEF_SENT | DBEF_UTF;
		dbei.szModule = GetContactProto(dat->hContact);
		dbei.timestamp = time(NULL);
		dbei.cbBlob = (int)mir_strlen(utfText) + 1;
		dbei.pBlob = (PBYTE)(char*)utfText;
		StreamInEvents(dat->hwnd, 0, 1, 1, &dbei);
		if (dat->hDbEventFirst == NULL)
			SendMessage(dat->hwnd, DM_REMAKELOG, 0, 0);
		dat->cache->saveHistory(0, 0);
		EnableSendButton(dat, FALSE);
		if (dat->pContainer->hwndActive == dat->hwnd)
			UpdateReadChars(dat);
		SendDlgItemMessage(dat->hwnd, IDC_SAVE, BM_SETIMAGE, IMAGE_ICON, (LPARAM)PluginConfig.g_buttonBarIcons[ICON_BUTTON_CANCEL]);
		SendDlgItemMessage(dat->hwnd, IDC_SAVE, BUTTONADDTOOLTIP, (WPARAM)pszIDCSAVE_close, BATF_UNICODE);
		dat->dwFlags &= ~MWF_SAVEBTN_SAV;

		if (!fAvail)
			return 0;
	}

	if (iJobIndex >= 0 && iJobIndex < NR_SENDJOBS) {
		SendJob *job = &m_jobs[iJobIndex];
		char szKeyName[20];
		wchar_t tszHeader[150];

		if (fIsSendLater) {
			time_t now = time(0);
			wchar_t tszTimestamp[30];
			wcsftime(tszTimestamp, _countof(tszTimestamp), L"%Y.%m.%d - %H:%M", _localtime32((__time32_t *)&now));
			mir_snprintf(szKeyName, "S%d", now);
			mir_snwprintf(tszHeader, TranslateT("\n(Sent delayed. Original timestamp %s)"), tszTimestamp);
		}
		else mir_snwprintf(tszHeader, L"M%d|", time(0));

		T2Utf utf_header(tszHeader);
		size_t required = mir_strlen(utf_header) + mir_strlen(job->szSendBuffer) + 10;
		char *tszMsg = reinterpret_cast<char *>(mir_alloc(required));

		if (fIsSendLater) {
			mir_snprintf(tszMsg, required, "%s%s", job->szSendBuffer, utf_header);
			db_set_s(hContact ? hContact : job->hContact, "SendLater", szKeyName, tszMsg);
		}
		else {
			mir_snprintf(tszMsg, required, "%s%s", utf_header, job->szSendBuffer);
			sendLater->addJob(tszMsg, hContact);
		}
		mir_free(tszMsg);

		if (fIsSendLater) {
			int iCount = db_get_dw(hContact ? hContact : job->hContact, "SendLater", "count", 0);
			iCount++;
			db_set_dw(hContact ? hContact : job->hContact, "SendLater", "count", iCount);
			sendLater->addContact(hContact ? hContact : job->hContact);
		}
		return iJobIndex;
	}
	return -1;
}
