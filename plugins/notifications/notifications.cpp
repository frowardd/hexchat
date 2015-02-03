/* HexChat
* Copyright (c) 2014 Leetsoftwerx
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*/

#include <SDKDDKVer.h>

#define STRICT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define STRICT_TYPED_ITEMIDS
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <locale>
#include <codecvt>
#include <unordered_map>
#include <filesystem>

#include <Windows.h>
#include <ShlObj.h>
#include <Shobjidl.h>
#include <Propvarutil.h>
#include <functiondiscoverykeys.h>
#include <VersionHelpers.h>

#include <roapi.h>
#include <windows.ui.notifications.h>
#include <comdef.h>

#include <glib.h>

#include "hexchat-plugin.h"

#define _(x) hexchat_gettext(ph,x)

hexchat_plugin * ph;
const char name[] = "Windows Toast Notifications";
const char desc[] = "Displays Toast notifications";
const char version[] = "1.0";
const char helptext[] = "Notifies the user using Toast notifications";
const wchar_t AppId[] = L"Hexchat.Desktop.Notify";

bool
should_alert ()
{
	int omit_away, omit_focused, omit_tray;

	if (hexchat_get_prefs (ph, "gui_focus_omitalerts", NULL, &omit_focused) == 3 && omit_focused)
	{
		const char *status = hexchat_get_info (ph, "win_status");

		if (status && !strcmp (status, "active"))
			return false;
	}

	if (hexchat_get_prefs (ph, "away_omit_alerts", NULL, &omit_away) == 3 && omit_away)
	{
		if (hexchat_get_info (ph, "away"))
			return false;
	}

	if (hexchat_get_prefs (ph, "gui_tray_quiet", NULL, &omit_tray) == 3 && omit_tray)
	{
		if (!hexchat_get_info (ph, "win_status"))
			return false;
	}

	return true;
}

std::wstring widen(const std::string & to_widen)
{
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > converter;
	return converter.from_bytes(to_widen);
}

std::string narrow(const std::wstring & to_narrow)
{
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > converter;
	return converter.to_bytes(to_narrow);
}

_COM_SMARTPTR_TYPEDEF(IPropertyStore, __uuidof(IPropertyStore));
// we have to create an app compatible shortcut to use toast notifications
HRESULT InstallShortcut(const std::wstring& shortcutPath)
{
	wchar_t exePath[MAX_PATH];

	DWORD charWritten = GetModuleFileNameW(nullptr, exePath, ARRAYSIZE(exePath));
	try
	{
		_com_util::CheckError(charWritten > 0 ? S_OK : E_FAIL);

		IShellLinkWPtr shellLink(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER);
		if (!shellLink)
			_com_issue_error(E_NOINTERFACE);
			
		_com_util::CheckError(shellLink->SetPath(exePath));

		_com_util::CheckError(shellLink->SetArguments(L""));
			
		IPropertyStorePtr propertyStore(shellLink);
		if (!propertyStore)
			_com_issue_error(E_NOINTERFACE);

		PROPVARIANT appIdPropVar;
		_com_util::CheckError(InitPropVariantFromString(AppId, &appIdPropVar));
		std::unique_ptr<PROPVARIANT, decltype(&PropVariantClear)> pro_var(&appIdPropVar, PropVariantClear);
		_com_util::CheckError(propertyStore->SetValue(PKEY_AppUserModel_ID, appIdPropVar));
			
		_com_util::CheckError(propertyStore->Commit());
			
		IPersistFilePtr persistFile(shellLink);
		if (!persistFile)
			_com_issue_error(E_NOINTERFACE);
			
		_com_util::CheckError(persistFile->Save(shortcutPath.c_str(), TRUE));
	}
	catch (const _com_error & ex)
	{
		return ex.Error();
	}
	return S_OK;
}

HRESULT TryInstallAppShortcut()
{
	wchar_t * roaming_path_wide = nullptr;
	HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming_path_wide);
	if (FAILED(hr))
		return hr;

	std::unique_ptr<wchar_t, decltype(&::CoTaskMemFree)> roaming_path(roaming_path_wide, &::CoTaskMemFree);
	std::tr2::sys::wpath path(roaming_path_wide);

	path /= L"\\Microsoft\\Windows\\Start Menu\\Programs\\Hexchat.lnk";
	bool fileExists = std::tr2::sys::exists(path);

	if (!fileExists)
	{
		hr = InstallShortcut(path.string());
	}
	else
	{
		hr = S_FALSE;
	}
	return hr;
}

static void
show_notification (char *title, const char *text)
{
	try
	{
		auto toastTemplate =
			Windows::UI::Notifications::ToastNotificationManager::GetTemplateContent (
			Windows::UI::Notifications::ToastTemplateType::ToastText02);
		auto node_list = toastTemplate->GetElementsByTagName (Platform::StringReference (L"text"));
		UINT node_count = node_list->Length;

		auto wtitle = widen (title);
		node_list->GetAt (0)->AppendChild (
			toastTemplate->CreateTextNode (Platform::StringReference (wtitle.c_str (), wtitle.size ())));

		auto wtext = widen (text);
		node_list->GetAt (1)->AppendChild (
			toastTemplate->CreateTextNode (
			Platform::StringReference (wtext.c_str (), wtext.size ())));

		auto notifier = Windows::UI::Notifications::ToastNotificationManager::CreateToastNotifier (Platform::StringReference (AppId));
		notifier->Show (ref new Windows::UI::Notifications::ToastNotification (toastTemplate));
	}
	catch (Platform::Exception ^ ex)
	{
		auto what = ex->ToString ();

		hexchat_printf (ph, "An Error Occurred Printing a Notification HRESULT: %#X : %s", static_cast<unsigned long>(ex->HResult), narrow (what->Data ()).c_str ());
	}
}

static void
show_notificationf (const char *text, const char *format, ...)
{
	va_list args;
	char *buf, *stripped;

	va_start (args, format);
	buf = g_strdup_vprintf (format, args);
	va_end (args);

	stripped = hexchat_strip (ph, text, -1, 3);
	show_notification (buf, stripped);
	g_free (buf);
	hexchat_free (ph, stripped);
}

static int
incoming_hilight_cb (char *word[], gpointer userdata)
{
	int hilight;

	if (hexchat_get_prefs (ph, "input_balloon_hilight", NULL, &hilight) == 3 && hilight && should_alert())
	{
		show_notificationf (word[2], _("Highlighted message from: %s (%s)"), word[1], hexchat_get_info (ph, "channel"));
	}
	return HEXCHAT_EAT_NONE;
}

static int
incoming_message_cb (char *word[], gpointer userdata)
{
	int message;

	if (hexchat_get_prefs (ph, "input_balloon_chans", NULL, &message) == 3 && message && should_alert ())
	{
		show_notificationf (word[2], _("Channel message from: %s (%s)"), word[1], hexchat_get_info (ph, "channel"));
	}
	return HEXCHAT_EAT_NONE;
}

static int
incoming_priv_cb (char *word[], gpointer userdata)
{
	int priv;

	if (hexchat_get_prefs (ph, "input_balloon_priv", NULL, &priv) == 3 && priv && should_alert ())
	{
		const char *network = hexchat_get_info (ph, "network");
		if (!network)
			network = hexchat_get_info (ph, "server");
		
		show_notificationf (word[2], _("Private message from: %s (%s)"), word[1], network);
	}
	return HEXCHAT_EAT_NONE;
}

static int
tray_cmd_cb (char *word[], char *word_eol[], gpointer userdata)
{
	if (word[2] && !g_ascii_strcasecmp (word[2], "-b") && word[3] && word[4])
	{
		show_notification (word[3], word_eol[4]);
		return HEXCHAT_EAT_ALL;
	}

	return HEXCHAT_EAT_NONE;
}

int
hexchat_plugin_init(hexchat_plugin *plugin_handle, char **plugin_name, char **plugin_desc, char **plugin_version, char *arg)
{
	if (!IsWindows8Point1OrGreater())
		return FALSE;
	if (FAILED(Windows::Foundation::Initialize(RO_INIT_SINGLETHREADED)))
		return FALSE;

	ph = plugin_handle;

	*plugin_name = const_cast<char*>(name);
	*plugin_desc = const_cast<char*>(desc);
	*plugin_version = const_cast<char*>(version);

	if (FAILED(TryInstallAppShortcut()))
		return FALSE;

	hexchat_hook_print (ph, "Channel Msg Hilight", HEXCHAT_PRI_LOWEST, incoming_hilight_cb, NULL);
	hexchat_hook_print (ph, "Channel Action Hilight", HEXCHAT_PRI_LOWEST, incoming_hilight_cb, NULL);

	hexchat_hook_print (ph, "Channel Message", HEXCHAT_PRI_LOWEST, incoming_message_cb, NULL);
	hexchat_hook_print (ph, "Channel Action", HEXCHAT_PRI_LOWEST, incoming_message_cb, NULL);
	hexchat_hook_print (ph, "Channel Notice", HEXCHAT_PRI_LOWEST, incoming_message_cb, NULL);

	hexchat_hook_print (ph, "Private Message", HEXCHAT_PRI_LOWEST, incoming_priv_cb, NULL);
	hexchat_hook_print (ph, "Private Message to Dialog", HEXCHAT_PRI_LOWEST, incoming_priv_cb, NULL);
	hexchat_hook_print (ph, "Private Action", HEXCHAT_PRI_LOWEST, incoming_priv_cb, NULL);
	hexchat_hook_print (ph, "Private Action to Dialog", HEXCHAT_PRI_LOWEST, incoming_priv_cb, NULL);

	hexchat_hook_command (ph, "TRAY", HEXCHAT_PRI_NORM, tray_cmd_cb, NULL, NULL);
	
	hexchat_printf(ph, "%s plugin loaded\n", name);

	return TRUE;
}


int
hexchat_plugin_deinit(void)
{
	Windows::Foundation::Uninitialize();
	hexchat_printf(ph, "%s plugin unloaded\n", name);
	return TRUE;
}
