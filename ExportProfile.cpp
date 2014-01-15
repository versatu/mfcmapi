// ExportProfile.cpp : implementation file
// Export profile to file

#include "stdafx.h"
#include "MAPIProfileFunctions.h"
#include "MAPIFunctions.h"

void ExportProfileSection(FILE* fProfile, LPPROFSECT lpSect)
{
	if (!fProfile || !lpSect) return;

	HRESULT hRes = S_OK;
	LPSPropValue lpAllProps = NULL;
	ULONG cValues = 0L;

	WC_H_GETPROPS(GetPropsNULL(lpSect,
		fMapiUnicode,
		&cValues,
		&lpAllProps));
	if (FAILED(hRes))
	{
		OutputToFilef(fProfile, _T("<properties error=\"0x%08X\" />\n"), hRes);
	}
	else if (lpAllProps)
	{
		OutputToFile(fProfile, _T("<properties listtype=\"section\">\n"));

		OutputPropertiesToFile(fProfile, cValues, lpAllProps, NULL, false);

		OutputToFile(fProfile, _T("</properties>\n"));

		MAPIFreeBuffer(lpAllProps);
	}
}

void ExportProfileProvider(FILE* fProfile, int iRow, LPPROVIDERADMIN lpProviderAdmin, LPSRow lpRow)
{
	if (!fProfile || !lpRow) return;

	Outputf(DBGNoDebug, fProfile, true, _T("<provider index = \"0x%08X\">\n"), iRow);

	OutputToFile(fProfile, _T("<properties listtype=\"row\">\n"));
	OutputSRowToFile(fProfile, lpRow, NULL);
	OutputToFile(fProfile, _T("</properties>\n"));

	HRESULT hRes = S_OK;
	LPSPropValue lpProviderUID = NULL;

	lpProviderUID = PpropFindProp(
		lpRow->lpProps,
		lpRow->cValues,
		PR_PROVIDER_UID);

	if (lpProviderUID)
	{
		LPPROFSECT lpSect = NULL;
		EC_H(OpenProfileSection(
			lpProviderAdmin,
			&lpProviderUID->Value.bin,
			&lpSect));
		if (lpSect)
		{
			ExportProfileSection(fProfile, lpSect);
			lpSect->Release();
		}
	}

	OutputToFile(fProfile, _T("</provider>\n"));
}

void ExportProfileService(FILE* fProfile, int iRow, LPSERVICEADMIN lpServiceAdmin, LPSRow lpRow)
{
	if (!fProfile || !lpRow) return;

	Outputf(DBGNoDebug, fProfile, true, _T("<service index = \"0x%08X\">\n"), iRow);

	OutputToFile(fProfile, _T("<properties listtype=\"row\">\n"));
	OutputSRowToFile(fProfile, lpRow, NULL);
	OutputToFile(fProfile, _T("</properties>\n"));

	HRESULT hRes = S_OK;
	LPSPropValue lpServiceUID = NULL;

	lpServiceUID = PpropFindProp(
		lpRow->lpProps,
		lpRow->cValues,
		PR_SERVICE_UID);

	if (lpServiceUID)
	{
		LPPROFSECT lpSect = NULL;
		EC_H(OpenProfileSection(
			lpServiceAdmin,
			&lpServiceUID->Value.bin,
			&lpSect));
		if (lpSect)
		{
			ExportProfileSection(fProfile, lpSect);
			lpSect->Release();
		}

		LPPROVIDERADMIN lpProviderAdmin = NULL;

		EC_MAPI(lpServiceAdmin->AdminProviders(
			(LPMAPIUID)lpServiceUID->Value.bin.lpb,
			0, // fMapiUnicode is not supported
			&lpProviderAdmin));

		if (lpProviderAdmin)
		{
			LPMAPITABLE lpProviderTable = NULL;
			EC_MAPI(lpProviderAdmin->GetProviderTable(
				0, // fMapiUnicode is not supported
				&lpProviderTable));

			if (lpProviderTable)
			{
				LPSRowSet lpRowSet = NULL;
				EC_MAPI(HrQueryAllRows(lpProviderTable, NULL, NULL, NULL, 0, &lpRowSet));
				if (lpRowSet && lpRowSet->cRows >= 1)
				{
					for (ULONG i = 0; i < lpRowSet->cRows; i++)
					{
						ExportProfileProvider(fProfile, i, lpProviderAdmin, &lpRowSet->aRow[i]);
					}
				}

				FreeProws(lpRowSet);
				lpProviderTable->Release();
				lpProviderTable = NULL;
			}

			lpProviderAdmin->Release();
			lpProviderAdmin = NULL;
		}
	}

	OutputToFile(fProfile, _T("</service>\n"));
}

void ExportProfile(LPCSTR szProfile, LPWSTR szFileName)
{
	if (!szProfile) return;

	DebugPrint(DBGGeneric, _T("ExportProfile: Saving profile \"%hs\" to \"%ws\"\n"), szProfile, szFileName);

	HRESULT hRes = S_OK;
	LPPROFADMIN lpProfAdmin = NULL;
	FILE* fProfile = NULL;

	if (szFileName)
	{
		fProfile = MyOpenFile(szFileName, true);
	}

	OutputToFile(fProfile, g_szXMLHeader);
	Outputf(DBGNoDebug, fProfile, true, _T("<profile profilename= \"%hs\">\n"), szProfile);

	EC_MAPI(MAPIAdminProfiles(0, &lpProfAdmin));

	if (lpProfAdmin)
	{
		LPSERVICEADMIN lpServiceAdmin = NULL;
#pragma warning(push)
#pragma warning(disable:4616)
#pragma warning(disable:6276)
		EC_MAPI(lpProfAdmin->AdminServices(
			(TCHAR*)szProfile,
			(TCHAR*)"",
			NULL,
			MAPI_DIALOG,
			&lpServiceAdmin));
#pragma warning(pop)
		if (lpServiceAdmin)
		{
			LPMAPITABLE lpServiceTable = NULL;

			EC_MAPI(lpServiceAdmin->GetMsgServiceTable(
				0, // fMapiUnicode is not supported
				&lpServiceTable));

			if (lpServiceTable)
			{
				LPSRowSet lpRowSet = NULL;
				EC_MAPI(HrQueryAllRows(lpServiceTable, NULL, NULL, NULL, 0, &lpRowSet));
				if (lpRowSet && lpRowSet->cRows >= 1)
				{
					for (ULONG i = 0; i < lpRowSet->cRows; i++)
					{
						ExportProfileService(fProfile, i, lpServiceAdmin, &lpRowSet->aRow[i]);
					}
				}

				FreeProws(lpRowSet);
				lpServiceTable->Release();
			}
			lpServiceAdmin->Release();
		}
		lpProfAdmin->Release();

		OutputToFile(fProfile, _T("</profile>"));

		if (fProfile)
		{
			CloseFile(fProfile);
		}
	}
}