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
#include <memory>
#include <string>
#include <codecvt>
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

static const wchar_t AppId[] = L"Hexchat.Desktop.Notify";

static std::wstring
widen(const std::string & to_widen)
{
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > converter;
	return converter.from_bytes(to_widen);
}

static std::string
narrow(const std::wstring & to_narrow)
{
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > converter;
	return converter.to_bytes(to_narrow);
}

_COM_SMARTPTR_TYPEDEF(IPropertyStore, __uuidof(IPropertyStore));
// we have to create an app compatible shortcut to use toast notifications
static HRESULT
InstallShortcut(const std::wstring& shortcutPath)
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

static HRESULT
TryInstallAppShortcut ()
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

extern "C"
{
	void
	notification_backend_show (const char *title, const char *text, int timeout)
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
		}
	}

	int
	notification_backend_init (void)
	{
		if (!IsWindows8Point1OrGreater ())
			return 0;

		if (FAILED (Windows::Foundation::Initialize (RO_INIT_SINGLETHREADED)))
			return FALSE;

		if (FAILED (TryInstallAppShortcut ()))
			return 0;

		return 1;
	}

	void
	notification_backend_deinit (void)
	{
		Windows::Foundation::Uninitialize ();
	}

	int
	notification_backend_supported (void)
	{
		/* FIXME: or portable-mode? */
		return IsWindows8Point1OrGreater ();
	}
}
