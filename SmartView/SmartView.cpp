#include "stdafx.h"
#include "..\stdafx.h"
#include "SmartView.h"
#include "BinaryParser.h"
#include "..\InterpretProp2.h"
#include "..\InterpretProp.h"
#include "..\ExtraPropTags.h"
#include "..\MAPIFunctions.h"
#include "..\String.h"
#include "..\guids.h"
#include "..\MySecInfo.h"
#include "..\NamedPropCache.h"
#include "..\ParseProperty.h"

#include "SmartViewParser.h"
#include "PCL.h"
#include "TombStone.h"
#include "VerbStream.h"
#include "NickNameCache.h"
#include "FolderUserFieldStream.h"
#include "RecipientRowStream.h"
#include "WebViewPersistStream.h"
#include "FlatEntryList.h"
#include "AdditionalRenEntryIDs.h"
#include "PropertyDefinitionStream.h"
#include "SearchFolderDefinition.h"
#include "EntryList.h"
#include "RuleCondition.h"
#include "RestrictionStruct.h"
#include "PropertyStruct.h"
#include "EntryIdStruct.h"

#define _MaxBytes 0xFFFF
#define _MaxDepth 50
#define _MaxEID 500
#define _MaxEntriesSmall 500
#define _MaxEntriesLarge 1000
#define _MaxEntriesExtraLarge 1500
#define _MaxEntriesEnormous 10000

wstring InterpretMVLongAsString(SLongArray myLongArray, ULONG ulPropTag, ULONG ulPropNameID, _In_opt_ LPGUID lpguidNamedProp);

// Functions to parse PT_LONG/PT-I2 properties

_Check_return_ CString RTimeToString(DWORD rTime);
_Check_return_ wstring RTimeToSzString(DWORD rTime, bool bLabel);
_Check_return_ wstring PTI8ToSzString(LARGE_INTEGER liI8, bool bLabel);

// End: Functions to parse PT_LONG/PT-I2 properties

typedef LPVOID BINTOSTRUCT(ULONG cbBin, _In_count_(cbBin) LPBYTE lpBin);
typedef BINTOSTRUCT *LPBINTOSTRUCT;
typedef void DELETESTRUCT(LPVOID lpStruct);
typedef DELETESTRUCT *LPDELETESTRUCT;
typedef LPWSTR STRUCTTOSTRING(LPVOID lpStruct);
typedef STRUCTTOSTRING *LPSTRUCTTOSTRING;

typedef void (BINTOSTRING)(SBinary myBin, LPWSTR* lpszResultString);
typedef BINTOSTRING *LPBINTOSTRING;
struct SMART_VIEW_PARSERS_ENTRY
{
	__ParsingTypeEnum iStructType;
	LPBINTOSTRUCT fBinToStruct;
	LPDELETESTRUCT fDeleteStruct;
	LPSTRUCTTOSTRING fStructToString;
};
#define MAKE_SV_ENTRY(_fIDSType, _fType) {(_fIDSType), (LPBINTOSTRUCT) BinTo##_fType, (LPDELETESTRUCT) Delete##_fType, (LPSTRUCTTOSTRING) _fType##ToString},
SMART_VIEW_PARSERS_ENTRY g_SmartViewParsers[] = {
	{ IDS_STNOPARSING, NULL, NULL, NULL },
	MAKE_SV_ENTRY(IDS_STTIMEZONEDEFINITION, TimeZoneDefinitionStruct)
	MAKE_SV_ENTRY(IDS_STTIMEZONE, TimeZoneStruct)
	// MAKE_SV_ENTRY(IDS_STSECURITYDESCRIPTOR, NULL}
	MAKE_SV_ENTRY(IDS_STEXTENDEDFOLDERFLAGS, ExtendedFlagsStruct)
	MAKE_SV_ENTRY(IDS_STAPPOINTMENTRECURRENCEPATTERN, AppointmentRecurrencePatternStruct)
	MAKE_SV_ENTRY(IDS_STRECURRENCEPATTERN, RecurrencePatternStruct)
	MAKE_SV_ENTRY(IDS_STREPORTTAG, ReportTagStruct)
	MAKE_SV_ENTRY(IDS_STCONVERSATIONINDEX, ConversationIndexStruct)
	MAKE_SV_ENTRY(IDS_STTASKASSIGNERS, TaskAssignersStruct)
	MAKE_SV_ENTRY(IDS_STGLOBALOBJECTID, GlobalObjectIdStruct)
	// MAKE_SV_ENTRY(IDS_STSID, SIDStruct)
	// MAKE_SV_ENTRY(IDS_STDECODEENTRYID)
	// MAKE_SV_ENTRY(IDS_STENCODEENTRYID)
};
ULONG g_cSmartViewParsers = _countof(g_SmartViewParsers);

LPSMARTVIEWPARSER GetSmartViewParser(__ParsingTypeEnum iStructType, ULONG cbBin, _In_count_(cbBin) LPBYTE lpBin)
{
	switch (iStructType)
	{
	case IDS_STNOPARSING:
		return NULL;
		break;
	case IDS_STTOMBSTONE:
		return new TombStone(cbBin, lpBin);
		break;
	case IDS_STPCL:
		return new PCL(cbBin, lpBin);
		break;
	case IDS_STVERBSTREAM:
		return new VerbStream(cbBin, lpBin);
		break;
	case IDS_STNICKNAMECACHE:
		return new NickNameCache(cbBin, lpBin);
		break;
	case IDS_STFOLDERUSERFIELDS:
		return new FolderUserFieldStream(cbBin, lpBin);
		break;
	case IDS_STRECIPIENTROWSTREAM:
		return new RecipientRowStream(cbBin, lpBin);
		break;
	case IDS_STWEBVIEWPERSISTSTREAM:
		return new WebViewPersistStream(cbBin, lpBin);
		break;
	case IDS_STFLATENTRYLIST:
		return new FlatEntryList(cbBin, lpBin);
		break;
	case IDS_STADDITIONALRENENTRYIDSEX:
		return new AdditionalRenEntryIDs(cbBin, lpBin);
		break;
	case IDS_STPROPERTYDEFINITIONSTREAM:
		return new PropertyDefinitionStream(cbBin, lpBin);
		break;
	case IDS_STSEARCHFOLDERDEFINITION:
		return new SearchFolderDefinition(cbBin, lpBin);
		break;
	case IDS_STENTRYLIST:
		return new EntryList(cbBin, lpBin);
		break;
	case IDS_STRULECONDITION:
		return new RuleCondition(cbBin, lpBin, false);
		break;
	case IDS_STEXTENDEDRULECONDITION:
		return new RuleCondition(cbBin, lpBin, true);
		break;
	case IDS_STRESTRICTION:
		return new RestrictionStruct(cbBin, lpBin);
		break;
	case IDS_STPROPERTY:
		return new PropertyStruct(cbBin, lpBin);
		break;
	case IDS_STENTRYID:
		return new EntryIdStruct(cbBin, lpBin);
		break;
	}

	return NULL;
}

_Check_return_ ULONG BuildFlagIndexFromTag(ULONG ulPropTag,
	ULONG ulPropNameID,
	_In_opt_z_ LPWSTR lpszPropNameString,
	_In_opt_ LPCGUID lpguidNamedProp)
{
	ULONG ulPropID = PROP_ID(ulPropTag);

	// Non-zero less than 0x8000 is a regular prop, we use the ID as the index
	if (ulPropID && ulPropID < 0x8000) return ulPropID;

	// Else we build our index from the guid and named property ID
	// In the future, we can look at using lpszPropNameString for MNID_STRING named properties
	if (lpguidNamedProp &&
		(ulPropNameID || lpszPropNameString))
	{
		ULONG ulGuid = NULL;
		if (*lpguidNamedProp == PSETID_Meeting)        ulGuid = guidPSETID_Meeting;
		else if (*lpguidNamedProp == PSETID_Address)        ulGuid = guidPSETID_Address;
		else if (*lpguidNamedProp == PSETID_Task)           ulGuid = guidPSETID_Task;
		else if (*lpguidNamedProp == PSETID_Appointment)    ulGuid = guidPSETID_Appointment;
		else if (*lpguidNamedProp == PSETID_Common)         ulGuid = guidPSETID_Common;
		else if (*lpguidNamedProp == PSETID_Log)            ulGuid = guidPSETID_Log;
		else if (*lpguidNamedProp == PSETID_PostRss)        ulGuid = guidPSETID_PostRss;
		else if (*lpguidNamedProp == PSETID_Sharing)        ulGuid = guidPSETID_Sharing;
		else if (*lpguidNamedProp == PSETID_Note)           ulGuid = guidPSETID_Note;

		if (ulGuid && ulPropNameID)
		{
			return PROP_TAG(ulGuid, ulPropNameID);
		}
		// Case not handled yet
		// else if (ulGuid && lpszPropNameString)
		// {
		// }
	}
	return NULL;
} // BuildFlagIndexFromTag

_Check_return_ __ParsingTypeEnum FindSmartViewParserForProp(const ULONG ulPropTag, const ULONG ulPropNameID, _In_opt_ const LPCGUID lpguidNamedProp)
{
	ULONG ulCurEntry = 0;
	ULONG ulIndex = BuildFlagIndexFromTag(ulPropTag, ulPropNameID, NULL, lpguidNamedProp);
	bool bMV = (PROP_TYPE(ulPropTag) & MV_FLAG) == MV_FLAG;

	while (ulCurEntry < ulSmartViewParserArray)
	{
		if (SmartViewParserArray[ulCurEntry].ulIndex == ulIndex &&
			SmartViewParserArray[ulCurEntry].bMV == bMV)
			return SmartViewParserArray[ulCurEntry].iStructType;
		ulCurEntry++;
	}

	return IDS_STNOPARSING;
}

_Check_return_ __ParsingTypeEnum FindSmartViewParserForProp(const ULONG ulPropTag, const ULONG ulPropNameID, _In_opt_ const LPCGUID lpguidNamedProp, bool bMVRow)
{
	ULONG ulLookupPropTag = ulPropTag;
	if (bMVRow) ulLookupPropTag |= MV_FLAG;

	return FindSmartViewParserForProp(ulLookupPropTag, ulPropNameID, lpguidNamedProp);
}

wstring InterpretPropSmartView(_In_ LPSPropValue lpProp, // required property value
	_In_opt_ LPMAPIPROP lpMAPIProp, // optional source object
	_In_opt_ LPMAPINAMEID lpNameID, // optional named property information to avoid GetNamesFromIDs call
	_In_opt_ LPSBinary lpMappingSignature, // optional mapping signature for object to speed named prop lookups
	bool bIsAB, // true if we know we're dealing with an address book property (they can be > 8000 and not named props)
	bool bMVRow) // did the row come from a MV prop?
{
	wstring lpszSmartView;

	if (!RegKeys[regkeyDO_SMART_VIEW].ulCurDWORD) return L"";
	if (!lpProp) return L"";

	HRESULT hRes = S_OK;
	__ParsingTypeEnum iStructType = IDS_STNOPARSING;

	// Named Props
	LPMAPINAMEID* lppPropNames = 0;

	// If we weren't passed named property information and we need it, look it up
	// We check bIsAB here - some address book providers return garbage which will crash us
	if (!lpNameID &&
		lpMAPIProp && // if we have an object
		!bIsAB &&
		RegKeys[regkeyPARSED_NAMED_PROPS].ulCurDWORD && // and we're parsing named props
		(RegKeys[regkeyGETPROPNAMES_ON_ALL_PROPS].ulCurDWORD || PROP_ID(lpProp->ulPropTag) >= 0x8000)) // and it's either a named prop or we're doing all props
	{
		SPropTagArray tag = { 0 };
		LPSPropTagArray lpTag = &tag;
		ULONG ulPropNames = 0;
		tag.cValues = 1;
		tag.aulPropTag[0] = lpProp->ulPropTag;

		WC_H_GETPROPS(GetNamesFromIDs(lpMAPIProp,
			lpMappingSignature,
			&lpTag,
			NULL,
			NULL,
			&ulPropNames,
			&lppPropNames));
		if (SUCCEEDED(hRes) && ulPropNames == 1 && lppPropNames && lppPropNames[0])
		{
			lpNameID = lppPropNames[0];
		}
		hRes = S_OK;
	}

	ULONG ulPropNameID = NULL;
	LPGUID lpPropNameGUID = NULL;

	if (lpNameID)
	{
		lpPropNameGUID = lpNameID->lpguid;
		if (lpNameID->ulKind == MNID_ID)
		{
			ulPropNameID = lpNameID->Kind.lID;
		}
	}

	ULONG ulLookupPropTag = NULL;
	switch (PROP_TYPE(lpProp->ulPropTag))
	{
	case PT_LONG:
	case PT_I2:
	case PT_I8:
		lpszSmartView = InterpretNumberAsString(lpProp->Value, lpProp->ulPropTag, ulPropNameID, NULL, lpPropNameGUID, true);
		break;
	case PT_MV_LONG:
		lpszSmartView = InterpretMVLongAsString(lpProp->Value.MVl, lpProp->ulPropTag, ulPropNameID, lpPropNameGUID);
		break;
	case PT_BINARY:
		ulLookupPropTag = lpProp->ulPropTag;
		if (bMVRow) ulLookupPropTag |= MV_FLAG;

		iStructType = FindSmartViewParserForProp(ulLookupPropTag, ulPropNameID, lpPropNameGUID);
		// We special-case this property
		if (!iStructType && PR_ROAMING_BINARYSTREAM == ulLookupPropTag && lpMAPIProp)
		{
			LPSPropValue lpPropSubject = NULL;

			WC_MAPI(HrGetOneProp(
				lpMAPIProp,
				PR_SUBJECT_W,
				&lpPropSubject));

			if (CheckStringProp(lpPropSubject, PT_UNICODE) && 0 == wcscmp(lpPropSubject->Value.lpszW, L"IPM.Configuration.Autocomplete"))
			{
				iStructType = IDS_STNICKNAMECACHE;
			}
		}

		if (iStructType)
		{
			lpszSmartView = InterpretBinaryAsString(lpProp->Value.bin, iStructType, lpMAPIProp, lpProp->ulPropTag);
		}

		break;
	case PT_MV_BINARY:
		iStructType = FindSmartViewParserForProp(lpProp->ulPropTag, ulPropNameID, lpPropNameGUID);
		if (iStructType)
		{
			lpszSmartView = InterpretMVBinaryAsString(lpProp->Value.MVbin, iStructType, lpMAPIProp, lpProp->ulPropTag);
		}

		break;
	}
	MAPIFreeBuffer(lppPropNames);

	return lpszSmartView;
}

wstring InterpretMVBinaryAsString(SBinaryArray myBinArray, __ParsingTypeEnum  iStructType, _In_opt_ LPMAPIPROP lpMAPIProp, ULONG ulPropTag)
{
	if (!RegKeys[regkeyDO_SMART_VIEW].ulCurDWORD) return L"";

	ULONG ulRow = 0;
	wstring szResult;

	for (ulRow = 0; ulRow < myBinArray.cValues; ulRow++)
	{
		if (ulRow != 0)
		{
			szResult += L"\r\n\r\n"; // STRING_OK
		}

		szResult += formatmessage(IDS_MVROWBIN, ulRow);
		szResult += InterpretBinaryAsString(myBinArray.lpbin[ulRow], iStructType, lpMAPIProp, ulPropTag);
	}

	return szResult;
}

wstring InterpretNumberAsStringProp(ULONG ulVal, ULONG ulPropTag)
{
	_PV pV = { 0 };
	pV.ul = ulVal;
	return InterpretNumberAsString(pV, ulPropTag, NULL, NULL, NULL, false);
}

wstring InterpretNumberAsStringNamedProp(ULONG ulVal, ULONG ulPropNameID, _In_opt_ LPCGUID lpguidNamedProp)
{
	_PV pV = { 0 };
	pV.ul = ulVal;
	return InterpretNumberAsString(pV, PT_LONG, ulPropNameID, NULL, lpguidNamedProp, false);
}

// Interprets a PT_LONG, PT_I2. or PT_I8 found in lpProp and returns a string
// Will not return a string if the lpProp is not a PT_LONG/PT_I2/PT_I8 or we don't recognize the property
// Will use named property details to look up named property flags
wstring InterpretNumberAsString(_PV pV, ULONG ulPropTag, ULONG ulPropNameID, _In_opt_z_ LPWSTR lpszPropNameString, _In_opt_ LPCGUID lpguidNamedProp, bool bLabel)
{
	wstring lpszResultString;
	if (!ulPropTag) return L"";

	if (PROP_TYPE(ulPropTag) != PT_LONG &&
		PROP_TYPE(ulPropTag) != PT_I2 &&
		PROP_TYPE(ulPropTag) != PT_I8) return L"";

	ULONG ulPropID = NULL;
	__ParsingTypeEnum iParser = FindSmartViewParserForProp(ulPropTag, ulPropNameID, lpguidNamedProp);
	switch (iParser)
	{
	case IDS_STLONGRTIME:
		lpszResultString = RTimeToSzString(pV.ul, bLabel);
		break;
	case IDS_STPTI8:
		lpszResultString = PTI8ToSzString(pV.li, bLabel);
		break;
	case IDS_STSFIDMID:
		lpszResultString = FidMidToSzString(pV.li.QuadPart, bLabel);
		break;
		// insert future parsers here
	default:
		ulPropID = BuildFlagIndexFromTag(ulPropTag, ulPropNameID, lpszPropNameString, lpguidNamedProp);
		if (ulPropID)
		{
			CString szPrefix;
			HRESULT hRes = S_OK;
			if (bLabel)
			{
				EC_B(szPrefix.LoadString(IDS_FLAGS_PREFIX));
			}

			LPTSTR szTmp = NULL;
			InterpretFlags(ulPropID, pV.ul, szPrefix, &szTmp);
			lpszResultString = LPTSTRToWstring(szTmp);
			delete[] szTmp;
		}

		break;
	}

	return lpszResultString;
}

wstring InterpretMVLongAsString(SLongArray myLongArray, ULONG ulPropTag, ULONG ulPropNameID, _In_opt_ LPGUID lpguidNamedProp)
{
	if (!RegKeys[regkeyDO_SMART_VIEW].ulCurDWORD) return L"";

	ULONG ulRow = 0;
	wstring szResult;
	wstring szSmartView;
	bool bHasData = false;

	for (ulRow = 0; ulRow < myLongArray.cValues; ulRow++)
	{
		_PV pV = { 0 };
		pV.ul = myLongArray.lpl[ulRow];
		szSmartView = InterpretNumberAsString(pV, CHANGE_PROP_TYPE(ulPropTag, PT_LONG), ulPropNameID, NULL, lpguidNamedProp, true);
		if (!szSmartView.empty())
		{
			if (bHasData)
			{
				szResult += L"\r\n"; // STRING_OK
			}

			bHasData = true;
			szResult += formatmessage(IDS_MVROWLONG,
				ulRow,
				szSmartView.c_str());
		}
	}

	return szResult;
}

wstring InterpretBinaryAsString(SBinary myBin, __ParsingTypeEnum iStructType, _In_opt_ LPMAPIPROP lpMAPIProp, ULONG ulPropTag)
{
	if (!RegKeys[regkeyDO_SMART_VIEW].ulCurDWORD) return L"";
	wstring szResultString = AddInSmartView(iStructType, myBin.cb, myBin.lpb);
	if (!szResultString.empty())
	{
		return szResultString;
	}

	ULONG i = 0;
	for (i = 0; i < g_cSmartViewParsers; i++)
	{
		if (iStructType == g_SmartViewParsers[i].iStructType)
		{
			if (g_SmartViewParsers[i].fBinToStruct && g_SmartViewParsers[i].fStructToString && g_SmartViewParsers[i].fDeleteStruct)
			{
				LPVOID pStruct = g_SmartViewParsers[i].fBinToStruct(myBin.cb, myBin.lpb);
				if (pStruct)
				{
					LPWSTR szTmp = g_SmartViewParsers[i].fStructToString(pStruct);

					if (szTmp)
					{
						szResultString = szTmp;
						delete[] szTmp;
					}

					g_SmartViewParsers[i].fDeleteStruct(pStruct);
					pStruct = NULL;
				}
			}

			return szResultString;
		}
	}

	LPSMARTVIEWPARSER svp = GetSmartViewParser(iStructType, myBin.cb, myBin.lpb);
	if (svp)
	{
		szResultString = svp->ToString();
		delete svp;
		return szResultString;
	}

	// These parsers have some special casing
	LPWSTR szTmp = NULL;

	switch (iStructType)
	{
	case IDS_STSECURITYDESCRIPTOR:
		SDBinToString(myBin, lpMAPIProp, ulPropTag, &szTmp);
		break;
	case IDS_STSID:
		SIDBinToString(myBin, &szTmp);
		break;
	case IDS_STDECODEENTRYID:
		szTmp = DecodeID(myBin.cb, myBin.lpb);
		break;
	case IDS_STENCODEENTRYID:
		szTmp = EncodeID(myBin.cb, (LPENTRYID)myBin.lpb);
		break;
	}

	if (szTmp)
	{
		szResultString = szTmp;
		delete[] szTmp;
	}

	return szResultString;
}

_Check_return_ CString RTimeToString(DWORD rTime)
{
	CString PropString;
	FILETIME fTime = { 0 };
	LARGE_INTEGER liNumSec = { 0 };
	liNumSec.LowPart = rTime;
	// Resolution of RTime is in minutes, FILETIME is in 100 nanosecond intervals
	// Scale between the two is 10000000*60
	liNumSec.QuadPart = liNumSec.QuadPart * 10000000 * 60;
	fTime.dwLowDateTime = liNumSec.LowPart;
	fTime.dwHighDateTime = liNumSec.HighPart;
	FileTimeToString(&fTime, &PropString, NULL);
	return PropString;
} // RTimeToString

_Check_return_ wstring RTimeToSzString(DWORD rTime, bool bLabel)
{
	wstring szRTime;
	if (bLabel)
	{
		szRTime = L"RTime: "; // STRING_OK
	}

	LPWSTR szTmp = CStringToLPWSTR(RTimeToString(rTime));
	szRTime += szTmp;
	delete[] szTmp;
	return szRTime;
}

_Check_return_ wstring PTI8ToSzString(LARGE_INTEGER liI8, bool bLabel)
{
	if (bLabel)
	{
		return formatmessage(IDS_PTI8FORMATLABEL, liI8.LowPart, liI8.HighPart);
	}
	else
	{
		return formatmessage(IDS_PTI8FORMAT, liI8.LowPart, liI8.HighPart);
	}
}

typedef WORD REPLID;
#define cbGlobcnt 6

struct ID
{
	REPLID replid;
	BYTE globcnt[cbGlobcnt];
};

_Check_return_ WORD WGetReplId(ID id)
{
	return id.replid;
} // WGetReplId

_Check_return_ ULONGLONG UllGetIdGlobcnt(ID id)
{
	ULONGLONG ul = 0;
	for (int i = 0; i < cbGlobcnt; ++i)
	{
		ul <<= 8;
		ul += id.globcnt[i];
	}

	return ul;
} // UllGetIdGlobcnt

_Check_return_ wstring FidMidToSzString(LONGLONG llID, bool bLabel)
{
	ID* pid = (ID*)&llID;
	if (bLabel)
	{
		return formatmessage(IDS_FIDMIDFORMATLABEL, WGetReplId(*pid), UllGetIdGlobcnt(*pid));
	}
	else
	{
		return formatmessage(IDS_FIDMIDFORMAT, WGetReplId(*pid), UllGetIdGlobcnt(*pid));
	}
}

_Check_return_ CString JunkDataToString(size_t cbJunkData, _In_count_(cbJunkData) LPBYTE lpJunkData)
{
	if (!cbJunkData || !lpJunkData) return _T("");
	DebugPrint(DBGSmartView, _T("Had 0x%08X = %u bytes left over.\n"), (int)cbJunkData, (UINT)cbJunkData);
	CString szTmp;
	SBinary sBin = { 0 };

	sBin.cb = (ULONG)cbJunkData;
	sBin.lpb = lpJunkData;
	szTmp.FormatMessage(IDS_JUNKDATASIZE,
		cbJunkData);
	szTmp += BinToHexString(&sBin, true).c_str();
	return szTmp;
} // JunkDataToString

//////////////////////////////////////////////////////////////////////////
// RecurrencePatternStruct
//////////////////////////////////////////////////////////////////////////

_Check_return_ RecurrencePatternStruct* BinToRecurrencePatternStruct(ULONG cbBin, _In_count_(cbBin) LPBYTE lpBin)
{
	return BinToRecurrencePatternStructWithSize(cbBin, lpBin, NULL);
} // BinToRecurrencePatternStruct

// Parses lpBin as a RecurrencePatternStruct
// lpcbBytesRead returns the number of bytes consumed
// Allocates return value with new.
// clean up with delete.
_Check_return_ RecurrencePatternStruct* BinToRecurrencePatternStructWithSize(ULONG cbBin, _In_count_(cbBin) LPBYTE lpBin, _Out_opt_ size_t* lpcbBytesRead)
{
	if (!lpBin) return NULL;
	if (lpcbBytesRead) *lpcbBytesRead = NULL;
	CBinaryParser Parser(cbBin, lpBin);

	RecurrencePatternStruct rpPattern = { 0 };

	Parser.GetWORD(&rpPattern.ReaderVersion);
	Parser.GetWORD(&rpPattern.WriterVersion);
	Parser.GetWORD(&rpPattern.RecurFrequency);
	Parser.GetWORD(&rpPattern.PatternType);
	Parser.GetWORD(&rpPattern.CalendarType);
	Parser.GetDWORD(&rpPattern.FirstDateTime);
	Parser.GetDWORD(&rpPattern.Period);
	Parser.GetDWORD(&rpPattern.SlidingFlag);

	switch (rpPattern.PatternType)
	{
	case rptMinute:
		break;
	case rptWeek:
		Parser.GetDWORD(&rpPattern.PatternTypeSpecific.WeekRecurrencePattern);
		break;
	case rptMonth:
	case rptMonthEnd:
	case rptHjMonth:
	case rptHjMonthEnd:
		Parser.GetDWORD(&rpPattern.PatternTypeSpecific.MonthRecurrencePattern);
		break;
	case rptMonthNth:
	case rptHjMonthNth:
		Parser.GetDWORD(&rpPattern.PatternTypeSpecific.MonthNthRecurrencePattern.DayOfWeek);
		Parser.GetDWORD(&rpPattern.PatternTypeSpecific.MonthNthRecurrencePattern.N);
		break;
	}

	Parser.GetDWORD(&rpPattern.EndType);
	Parser.GetDWORD(&rpPattern.OccurrenceCount);
	Parser.GetDWORD(&rpPattern.FirstDOW);
	Parser.GetDWORD(&rpPattern.DeletedInstanceCount);

	if (rpPattern.DeletedInstanceCount && rpPattern.DeletedInstanceCount < _MaxEntriesSmall)
	{
		rpPattern.DeletedInstanceDates = new DWORD[rpPattern.DeletedInstanceCount];
		if (rpPattern.DeletedInstanceDates)
		{
			memset(rpPattern.DeletedInstanceDates, 0, sizeof(DWORD)* rpPattern.DeletedInstanceCount);
			DWORD i = 0;
			for (i = 0; i < rpPattern.DeletedInstanceCount; i++)
			{
				Parser.GetDWORD(&rpPattern.DeletedInstanceDates[i]);
			}
		}
	}

	Parser.GetDWORD(&rpPattern.ModifiedInstanceCount);

	if (rpPattern.ModifiedInstanceCount &&
		rpPattern.ModifiedInstanceCount <= rpPattern.DeletedInstanceCount &&
		rpPattern.ModifiedInstanceCount < _MaxEntriesSmall)
	{
		rpPattern.ModifiedInstanceDates = new DWORD[rpPattern.ModifiedInstanceCount];
		if (rpPattern.ModifiedInstanceDates)
		{
			memset(rpPattern.ModifiedInstanceDates, 0, sizeof(DWORD)* rpPattern.ModifiedInstanceCount);
			DWORD i = 0;
			for (i = 0; i < rpPattern.ModifiedInstanceCount; i++)
			{
				Parser.GetDWORD(&rpPattern.ModifiedInstanceDates[i]);
			}
		}
	}
	Parser.GetDWORD(&rpPattern.StartDate);
	Parser.GetDWORD(&rpPattern.EndDate);

	// Only fill out junk data if we've not been asked to report back how many bytes we read
	// If we've been asked to report back, then someone else will handle the remaining data
	if (!lpcbBytesRead)
	{
		rpPattern.JunkDataSize = Parser.GetRemainingData(&rpPattern.JunkData);
	}
	if (lpcbBytesRead) *lpcbBytesRead = Parser.GetCurrentOffset();

	RecurrencePatternStruct* prpPattern = new RecurrencePatternStruct;
	if (prpPattern)
	{
		*prpPattern = rpPattern;
	}

	return prpPattern;
} // BinToRecurrencePatternStructWithSize

void DeleteRecurrencePatternStruct(_In_ RecurrencePatternStruct* prpPattern)
{
	if (!prpPattern) return;
	delete[] prpPattern->DeletedInstanceDates;
	delete[] prpPattern->ModifiedInstanceDates;
	delete[] prpPattern->JunkData;
	delete prpPattern;
} // DeleteRecurrencePatternStruct

// result allocated with new
// clean up with delete[]
_Check_return_ LPWSTR RecurrencePatternStructToString(_In_ RecurrencePatternStruct* prpPattern)
{
	if (!prpPattern) return NULL;

	CString szRP;
	CString szTmp;

	LPTSTR szRecurFrequency = NULL;
	LPTSTR szPatternType = NULL;
	LPTSTR szCalendarType = NULL;
	InterpretFlags(flagRecurFrequency, prpPattern->RecurFrequency, &szRecurFrequency);
	InterpretFlags(flagPatternType, prpPattern->PatternType, &szPatternType);
	InterpretFlags(flagCalendarType, prpPattern->CalendarType, &szCalendarType);
	szTmp.FormatMessage(IDS_RPHEADER,
		prpPattern->ReaderVersion,
		prpPattern->WriterVersion,
		prpPattern->RecurFrequency, szRecurFrequency,
		prpPattern->PatternType, szPatternType,
		prpPattern->CalendarType, szCalendarType,
		prpPattern->FirstDateTime,
		prpPattern->Period,
		prpPattern->SlidingFlag);
	delete[] szRecurFrequency;
	delete[] szPatternType;
	delete[] szCalendarType;
	szRecurFrequency = NULL;
	szPatternType = NULL;
	szCalendarType = NULL;
	szRP += szTmp;

	LPTSTR szDOW = NULL;
	LPTSTR szN = NULL;
	switch (prpPattern->PatternType)
	{
	case rptMinute:
		break;
	case rptWeek:
		InterpretFlags(flagDOW, prpPattern->PatternTypeSpecific.WeekRecurrencePattern, &szDOW);
		szTmp.FormatMessage(IDS_RPPATTERNWEEK,
			prpPattern->PatternTypeSpecific.WeekRecurrencePattern, szDOW);
		szRP += szTmp;
		break;
	case rptMonth:
	case rptMonthEnd:
	case rptHjMonth:
	case rptHjMonthEnd:
		szTmp.FormatMessage(IDS_RPPATTERNMONTH,
			prpPattern->PatternTypeSpecific.MonthRecurrencePattern);
		szRP += szTmp;
		break;
	case rptMonthNth:
	case rptHjMonthNth:
		InterpretFlags(flagDOW, prpPattern->PatternTypeSpecific.MonthNthRecurrencePattern.DayOfWeek, &szDOW);
		InterpretFlags(flagN, prpPattern->PatternTypeSpecific.MonthNthRecurrencePattern.N, &szN);
		szTmp.FormatMessage(IDS_RPPATTERNMONTHNTH,
			prpPattern->PatternTypeSpecific.MonthNthRecurrencePattern.DayOfWeek, szDOW,
			prpPattern->PatternTypeSpecific.MonthNthRecurrencePattern.N, szN);
		szRP += szTmp;
		break;
	}
	delete[] szDOW;
	delete[] szN;
	szDOW = NULL;
	szN = NULL;

	LPTSTR szEndType = NULL;
	LPTSTR szFirstDOW = NULL;
	InterpretFlags(flagEndType, prpPattern->EndType, &szEndType);
	InterpretFlags(flagFirstDOW, prpPattern->FirstDOW, &szFirstDOW);

	szTmp.FormatMessage(IDS_RPHEADER2,
		prpPattern->EndType, szEndType,
		prpPattern->OccurrenceCount,
		prpPattern->FirstDOW, szFirstDOW,
		prpPattern->DeletedInstanceCount);
	szRP += szTmp;
	delete[] szEndType;
	delete[] szFirstDOW;
	szEndType = NULL;
	szFirstDOW = NULL;

	if (prpPattern->DeletedInstanceCount && prpPattern->DeletedInstanceDates)
	{
		DWORD i = 0;
		for (i = 0; i < prpPattern->DeletedInstanceCount; i++)
		{
			szTmp.FormatMessage(IDS_RPDELETEDINSTANCEDATES,
				i, prpPattern->DeletedInstanceDates[i], RTimeToString(prpPattern->DeletedInstanceDates[i]));
			szRP += szTmp;
		}
	}

	szTmp.FormatMessage(IDS_RPMODIFIEDINSTANCECOUNT,
		prpPattern->ModifiedInstanceCount);
	szRP += szTmp;

	if (prpPattern->ModifiedInstanceCount && prpPattern->ModifiedInstanceDates)
	{
		DWORD i = 0;
		for (i = 0; i < prpPattern->ModifiedInstanceCount; i++)
		{
			szTmp.FormatMessage(IDS_RPMODIFIEDINSTANCEDATES,
				i, prpPattern->ModifiedInstanceDates[i], RTimeToString(prpPattern->ModifiedInstanceDates[i]));
			szRP += szTmp;
		}
	}

	szTmp.FormatMessage(IDS_RPDATE,
		prpPattern->StartDate, RTimeToString(prpPattern->StartDate),
		prpPattern->EndDate, RTimeToString(prpPattern->EndDate));
	szRP += szTmp;

	szRP += JunkDataToString(prpPattern->JunkDataSize, prpPattern->JunkData);

	return CStringToLPWSTR(szRP);
} // RecurrencePatternStructToString

//////////////////////////////////////////////////////////////////////////
// End RecurrencePatternStruct
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// AppointmentRecurrencePatternStruct
//////////////////////////////////////////////////////////////////////////

// Allocates return value with new.
// Clean up with DeleteAppointmentRecurrencePatternStruct.
_Check_return_ AppointmentRecurrencePatternStruct* BinToAppointmentRecurrencePatternStruct(ULONG cbBin, _In_count_(cbBin) LPBYTE lpBin)
{
	if (!lpBin) return NULL;

	AppointmentRecurrencePatternStruct arpPattern = { 0 };
	CBinaryParser Parser(cbBin, lpBin);

	size_t cbBinRead = 0;
	arpPattern.RecurrencePattern = BinToRecurrencePatternStructWithSize(cbBin, lpBin, &cbBinRead);
	Parser.Advance(cbBinRead);
	Parser.GetDWORD(&arpPattern.ReaderVersion2);
	Parser.GetDWORD(&arpPattern.WriterVersion2);
	Parser.GetDWORD(&arpPattern.StartTimeOffset);
	Parser.GetDWORD(&arpPattern.EndTimeOffset);
	Parser.GetWORD(&arpPattern.ExceptionCount);

	if (arpPattern.ExceptionCount &&
		arpPattern.ExceptionCount == arpPattern.RecurrencePattern->ModifiedInstanceCount &&
		arpPattern.ExceptionCount < _MaxEntriesSmall)
	{
		arpPattern.ExceptionInfo = new ExceptionInfoStruct[arpPattern.ExceptionCount];
		if (arpPattern.ExceptionInfo)
		{
			memset(arpPattern.ExceptionInfo, 0, sizeof(ExceptionInfoStruct)* arpPattern.ExceptionCount);
			WORD i = 0;
			for (i = 0; i < arpPattern.ExceptionCount; i++)
			{
				Parser.GetDWORD(&arpPattern.ExceptionInfo[i].StartDateTime);
				Parser.GetDWORD(&arpPattern.ExceptionInfo[i].EndDateTime);
				Parser.GetDWORD(&arpPattern.ExceptionInfo[i].OriginalStartDate);
				Parser.GetWORD(&arpPattern.ExceptionInfo[i].OverrideFlags);
				if (arpPattern.ExceptionInfo[i].OverrideFlags & ARO_SUBJECT)
				{
					Parser.GetWORD(&arpPattern.ExceptionInfo[i].SubjectLength);
					Parser.GetWORD(&arpPattern.ExceptionInfo[i].SubjectLength2);
					if (arpPattern.ExceptionInfo[i].SubjectLength2 && arpPattern.ExceptionInfo[i].SubjectLength2 + 1 == arpPattern.ExceptionInfo[i].SubjectLength)
					{
						Parser.GetStringA(arpPattern.ExceptionInfo[i].SubjectLength2, &arpPattern.ExceptionInfo[i].Subject);
					}
				}
				if (arpPattern.ExceptionInfo[i].OverrideFlags & ARO_MEETINGTYPE)
				{
					Parser.GetDWORD(&arpPattern.ExceptionInfo[i].MeetingType);
				}
				if (arpPattern.ExceptionInfo[i].OverrideFlags & ARO_REMINDERDELTA)
				{
					Parser.GetDWORD(&arpPattern.ExceptionInfo[i].ReminderDelta);
				}
				if (arpPattern.ExceptionInfo[i].OverrideFlags & ARO_REMINDER)
				{
					Parser.GetDWORD(&arpPattern.ExceptionInfo[i].ReminderSet);
				}
				if (arpPattern.ExceptionInfo[i].OverrideFlags & ARO_LOCATION)
				{
					Parser.GetWORD(&arpPattern.ExceptionInfo[i].LocationLength);
					Parser.GetWORD(&arpPattern.ExceptionInfo[i].LocationLength2);
					if (arpPattern.ExceptionInfo[i].LocationLength2 && arpPattern.ExceptionInfo[i].LocationLength2 + 1 == arpPattern.ExceptionInfo[i].LocationLength)
					{
						Parser.GetStringA(arpPattern.ExceptionInfo[i].LocationLength2, &arpPattern.ExceptionInfo[i].Location);
					}
				}
				if (arpPattern.ExceptionInfo[i].OverrideFlags & ARO_BUSYSTATUS)
				{
					Parser.GetDWORD(&arpPattern.ExceptionInfo[i].BusyStatus);
				}
				if (arpPattern.ExceptionInfo[i].OverrideFlags & ARO_ATTACHMENT)
				{
					Parser.GetDWORD(&arpPattern.ExceptionInfo[i].Attachment);
				}
				if (arpPattern.ExceptionInfo[i].OverrideFlags & ARO_SUBTYPE)
				{
					Parser.GetDWORD(&arpPattern.ExceptionInfo[i].SubType);
				}
				if (arpPattern.ExceptionInfo[i].OverrideFlags & ARO_APPTCOLOR)
				{
					Parser.GetDWORD(&arpPattern.ExceptionInfo[i].AppointmentColor);
				}
			}
		}
	}
	Parser.GetDWORD(&arpPattern.ReservedBlock1Size);
	Parser.GetBYTES(arpPattern.ReservedBlock1Size, _MaxBytes, &arpPattern.ReservedBlock1);

	if (arpPattern.ExceptionCount &&
		arpPattern.ExceptionCount == arpPattern.RecurrencePattern->ModifiedInstanceCount &&
		arpPattern.ExceptionCount < _MaxEntriesSmall &&
		arpPattern.ExceptionInfo)
	{
		arpPattern.ExtendedException = new ExtendedExceptionStruct[arpPattern.ExceptionCount];
		if (arpPattern.ExtendedException)
		{
			memset(arpPattern.ExtendedException, 0, sizeof(ExtendedExceptionStruct)* arpPattern.ExceptionCount);
			WORD i = 0;
			for (i = 0; i < arpPattern.ExceptionCount; i++)
			{
				if (arpPattern.WriterVersion2 >= 0x0003009)
				{
					Parser.GetDWORD(&arpPattern.ExtendedException[i].ChangeHighlight.ChangeHighlightSize);
					Parser.GetDWORD(&arpPattern.ExtendedException[i].ChangeHighlight.ChangeHighlightValue);
					if (arpPattern.ExtendedException[i].ChangeHighlight.ChangeHighlightSize > sizeof(DWORD))
					{
						Parser.GetBYTES(arpPattern.ExtendedException[i].ChangeHighlight.ChangeHighlightSize - sizeof(DWORD), _MaxBytes, &arpPattern.ExtendedException[i].ChangeHighlight.Reserved);
					}
				}
				Parser.GetDWORD(&arpPattern.ExtendedException[i].ReservedBlockEE1Size);
				Parser.GetBYTES(arpPattern.ExtendedException[i].ReservedBlockEE1Size, _MaxBytes, &arpPattern.ExtendedException[i].ReservedBlockEE1);

				if (arpPattern.ExceptionInfo[i].OverrideFlags & ARO_SUBJECT ||
					arpPattern.ExceptionInfo[i].OverrideFlags & ARO_LOCATION)
				{
					Parser.GetDWORD(&arpPattern.ExtendedException[i].StartDateTime);
					Parser.GetDWORD(&arpPattern.ExtendedException[i].EndDateTime);
					Parser.GetDWORD(&arpPattern.ExtendedException[i].OriginalStartDate);
				}
				if (arpPattern.ExceptionInfo[i].OverrideFlags & ARO_SUBJECT)
				{
					Parser.GetWORD(&arpPattern.ExtendedException[i].WideCharSubjectLength);
					if (arpPattern.ExtendedException[i].WideCharSubjectLength)
					{
						Parser.GetStringW(arpPattern.ExtendedException[i].WideCharSubjectLength, &arpPattern.ExtendedException[i].WideCharSubject);
					}
				}
				if (arpPattern.ExceptionInfo[i].OverrideFlags & ARO_LOCATION)
				{
					Parser.GetWORD(&arpPattern.ExtendedException[i].WideCharLocationLength);
					if (arpPattern.ExtendedException[i].WideCharLocationLength)
					{
						Parser.GetStringW(arpPattern.ExtendedException[i].WideCharLocationLength, &arpPattern.ExtendedException[i].WideCharLocation);
					}
				}
				if (arpPattern.ExceptionInfo[i].OverrideFlags & ARO_SUBJECT ||
					arpPattern.ExceptionInfo[i].OverrideFlags & ARO_LOCATION)
				{
					Parser.GetDWORD(&arpPattern.ExtendedException[i].ReservedBlockEE2Size);
					Parser.GetBYTES(arpPattern.ExtendedException[i].ReservedBlockEE2Size, _MaxBytes, &arpPattern.ExtendedException[i].ReservedBlockEE2);

				}
			}
		}
	}
	Parser.GetDWORD(&arpPattern.ReservedBlock2Size);
	Parser.GetBYTES(arpPattern.ReservedBlock2Size, _MaxBytes, &arpPattern.ReservedBlock2);

	arpPattern.JunkDataSize = Parser.GetRemainingData(&arpPattern.JunkData);

	AppointmentRecurrencePatternStruct* parpPattern = new AppointmentRecurrencePatternStruct;
	if (parpPattern)
	{
		*parpPattern = arpPattern;
	}

	return parpPattern;
} // BinToAppointmentRecurrencePatternStruct

void DeleteAppointmentRecurrencePatternStruct(_In_ AppointmentRecurrencePatternStruct* parpPattern)
{
	if (!parpPattern) return;
	DeleteRecurrencePatternStruct(parpPattern->RecurrencePattern);
	int i = 0;
	if (parpPattern->ExceptionCount && parpPattern->ExceptionInfo)
	{
		for (i = 0; i < parpPattern->ExceptionCount; i++)
		{
			delete[] parpPattern->ExceptionInfo[i].Subject;
			delete[] parpPattern->ExceptionInfo[i].Location;
		}
	}
	delete[] parpPattern->ExceptionInfo;
	if (parpPattern->ExceptionCount && parpPattern->ExtendedException)
	{
		for (i = 0; i < parpPattern->ExceptionCount; i++)
		{
			delete[] parpPattern->ExtendedException[i].ChangeHighlight.Reserved;
			delete[] parpPattern->ExtendedException[i].ReservedBlockEE1;
			delete[] parpPattern->ExtendedException[i].WideCharSubject;
			delete[] parpPattern->ExtendedException[i].WideCharLocation;
			delete[] parpPattern->ExtendedException[i].ReservedBlockEE2;
		}
	}
	delete[] parpPattern->ExtendedException;
	delete[] parpPattern->ReservedBlock2;
	delete[] parpPattern->JunkData;
	delete parpPattern;
} // DeleteAppointmentRecurrencePatternStruct

// result allocated with new
// clean up with delete[]
_Check_return_ LPWSTR AppointmentRecurrencePatternStructToString(_In_ AppointmentRecurrencePatternStruct* parpPattern)
{
	if (!parpPattern) return NULL;

	CString szARP;
	CString szTmp;
	LPWSTR szRecurrencePattern = NULL;

	szRecurrencePattern = RecurrencePatternStructToString(parpPattern->RecurrencePattern);
	szARP = szRecurrencePattern;
	delete[] szRecurrencePattern;

	szTmp.FormatMessage(IDS_ARPHEADER,
		parpPattern->ReaderVersion2,
		parpPattern->WriterVersion2,
		parpPattern->StartTimeOffset, RTimeToString(parpPattern->StartTimeOffset),
		parpPattern->EndTimeOffset, RTimeToString(parpPattern->EndTimeOffset),
		parpPattern->ExceptionCount);
	szARP += szTmp;

	WORD i = 0;
	if (parpPattern->ExceptionCount && parpPattern->ExceptionInfo)
	{
		for (i = 0; i < parpPattern->ExceptionCount; i++)
		{
			CString szExceptionInfo;
			LPTSTR szOverrideFlags = NULL;
			InterpretFlags(flagOverrideFlags, parpPattern->ExceptionInfo[i].OverrideFlags, &szOverrideFlags);
			szExceptionInfo.FormatMessage(IDS_ARPEXHEADER,
				i, parpPattern->ExceptionInfo[i].StartDateTime, RTimeToString(parpPattern->ExceptionInfo[i].StartDateTime),
				parpPattern->ExceptionInfo[i].EndDateTime, RTimeToString(parpPattern->ExceptionInfo[i].EndDateTime),
				parpPattern->ExceptionInfo[i].OriginalStartDate, RTimeToString(parpPattern->ExceptionInfo[i].OriginalStartDate),
				parpPattern->ExceptionInfo[i].OverrideFlags, szOverrideFlags);
			delete[] szOverrideFlags;
			szOverrideFlags = NULL;
			if (parpPattern->ExceptionInfo[i].OverrideFlags & ARO_SUBJECT)
			{
				szTmp.FormatMessage(IDS_ARPEXSUBJECT,
					i, parpPattern->ExceptionInfo[i].SubjectLength,
					parpPattern->ExceptionInfo[i].SubjectLength2,
					parpPattern->ExceptionInfo[i].Subject ? parpPattern->ExceptionInfo[i].Subject : "");
				szExceptionInfo += szTmp;
			}
			if (parpPattern->ExceptionInfo[i].OverrideFlags & ARO_MEETINGTYPE)
			{
				LPWSTR szFlags = wstringToLPWSTR(InterpretNumberAsStringNamedProp(parpPattern->ExceptionInfo[i].MeetingType, dispidApptStateFlags, (LPGUID)&PSETID_Appointment));
				szTmp.FormatMessage(IDS_ARPEXMEETINGTYPE,
					i, parpPattern->ExceptionInfo[i].MeetingType, szFlags);
				delete[] szFlags;
				szFlags = NULL;
				szExceptionInfo += szTmp;
			}
			if (parpPattern->ExceptionInfo[i].OverrideFlags & ARO_REMINDERDELTA)
			{
				szTmp.FormatMessage(IDS_ARPEXREMINDERDELTA,
					i, parpPattern->ExceptionInfo[i].ReminderDelta);
				szExceptionInfo += szTmp;
			}
			if (parpPattern->ExceptionInfo[i].OverrideFlags & ARO_REMINDER)
			{
				szTmp.FormatMessage(IDS_ARPEXREMINDERSET,
					i, parpPattern->ExceptionInfo[i].ReminderSet);
				szExceptionInfo += szTmp;
			}
			if (parpPattern->ExceptionInfo[i].OverrideFlags & ARO_LOCATION)
			{
				szTmp.FormatMessage(IDS_ARPEXLOCATION,
					i, parpPattern->ExceptionInfo[i].LocationLength,
					parpPattern->ExceptionInfo[i].LocationLength2,
					parpPattern->ExceptionInfo[i].Location ? parpPattern->ExceptionInfo[i].Location : "");
				szExceptionInfo += szTmp;
			}
			if (parpPattern->ExceptionInfo[i].OverrideFlags & ARO_BUSYSTATUS)
			{
				LPWSTR szFlags = wstringToLPWSTR(InterpretNumberAsStringNamedProp(parpPattern->ExceptionInfo[i].BusyStatus, dispidBusyStatus, (LPGUID)&PSETID_Appointment));
				szTmp.FormatMessage(IDS_ARPEXBUSYSTATUS,
					i, parpPattern->ExceptionInfo[i].BusyStatus, szFlags);
				delete[] szFlags;
				szFlags = NULL;
				szExceptionInfo += szTmp;
			}
			if (parpPattern->ExceptionInfo[i].OverrideFlags & ARO_ATTACHMENT)
			{
				szTmp.FormatMessage(IDS_ARPEXATTACHMENT,
					i, parpPattern->ExceptionInfo[i].Attachment);
				szExceptionInfo += szTmp;
			}
			if (parpPattern->ExceptionInfo[i].OverrideFlags & ARO_SUBTYPE)
			{
				szTmp.FormatMessage(IDS_ARPEXSUBTYPE,
					i, parpPattern->ExceptionInfo[i].SubType);
				szExceptionInfo += szTmp;
			}
			if (parpPattern->ExceptionInfo[i].OverrideFlags & ARO_APPTCOLOR)
			{
				szTmp.FormatMessage(IDS_ARPEXAPPOINTMENTCOLOR,
					i, parpPattern->ExceptionInfo[i].AppointmentColor);
				szExceptionInfo += szTmp;
			}
			szARP += szExceptionInfo;
		}
	}

	szTmp.FormatMessage(IDS_ARPRESERVED1,
		parpPattern->ReservedBlock1Size);
	szARP += szTmp;
	if (parpPattern->ReservedBlock1Size)
	{
		SBinary sBin = { 0 };
		sBin.cb = parpPattern->ReservedBlock1Size;
		sBin.lpb = parpPattern->ReservedBlock1;
		szARP += wstringToCString(BinToHexString(&sBin, true));
	}

	if (parpPattern->ExceptionCount && parpPattern->ExtendedException)
	{
		for (i = 0; i < parpPattern->ExceptionCount; i++)
		{
			CString szExtendedException;
			if (parpPattern->WriterVersion2 >= 0x00003009)
			{
				LPWSTR szFlags = wstringToLPWSTR(InterpretNumberAsStringNamedProp(parpPattern->ExtendedException[i].ChangeHighlight.ChangeHighlightValue, dispidChangeHighlight, (LPGUID)&PSETID_Appointment));
				szTmp.FormatMessage(IDS_ARPEXCHANGEHIGHLIGHT,
					i, parpPattern->ExtendedException[i].ChangeHighlight.ChangeHighlightSize,
					parpPattern->ExtendedException[i].ChangeHighlight.ChangeHighlightValue, szFlags);
				delete[] szFlags;
				szFlags = NULL;
				szExtendedException += szTmp;
				if (parpPattern->ExtendedException[i].ChangeHighlight.ChangeHighlightSize > sizeof(DWORD))
				{
					szTmp.FormatMessage(IDS_ARPEXCHANGEHIGHLIGHTRESERVED,
						i);
					szExtendedException += szTmp;
					SBinary sBin = { 0 };
					sBin.cb = parpPattern->ExtendedException[i].ChangeHighlight.ChangeHighlightSize - sizeof(DWORD);
					sBin.lpb = parpPattern->ExtendedException[i].ChangeHighlight.Reserved;
					szExtendedException += wstringToCString(BinToHexString(&sBin, true));
					szExtendedException += _T("\n"); // STRING_OK
				}
			}
			szTmp.FormatMessage(IDS_ARPEXRESERVED1,
				i, parpPattern->ExtendedException[i].ReservedBlockEE1Size);
			szExtendedException += szTmp;
			if (parpPattern->ExtendedException[i].ReservedBlockEE1Size)
			{
				SBinary sBin = { 0 };
				sBin.cb = parpPattern->ExtendedException[i].ReservedBlockEE1Size;
				sBin.lpb = parpPattern->ExtendedException[i].ReservedBlockEE1;
				szExtendedException += wstringToCString(BinToHexString(&sBin, true));
			}
			if (parpPattern->ExceptionInfo)
			{
				if (parpPattern->ExceptionInfo[i].OverrideFlags & ARO_SUBJECT ||
					parpPattern->ExceptionInfo[i].OverrideFlags & ARO_LOCATION)
				{
					szTmp.FormatMessage(IDS_ARPEXDATETIME,
						i, parpPattern->ExtendedException[i].StartDateTime, RTimeToString(parpPattern->ExtendedException[i].StartDateTime),
						parpPattern->ExtendedException[i].EndDateTime, RTimeToString(parpPattern->ExtendedException[i].EndDateTime),
						parpPattern->ExtendedException[i].OriginalStartDate, RTimeToString(parpPattern->ExtendedException[i].OriginalStartDate));
					szExtendedException += szTmp;
				}
				if (parpPattern->ExceptionInfo[i].OverrideFlags & ARO_SUBJECT)
				{
					szTmp.FormatMessage(IDS_ARPEXWIDESUBJECT,
						i, parpPattern->ExtendedException[i].WideCharSubjectLength,
						parpPattern->ExtendedException[i].WideCharSubject ? parpPattern->ExtendedException[i].WideCharSubject : L"");
					szExtendedException += szTmp;
				}
				if (parpPattern->ExceptionInfo[i].OverrideFlags & ARO_LOCATION)
				{
					szTmp.FormatMessage(IDS_ARPEXWIDELOCATION,
						i, parpPattern->ExtendedException[i].WideCharLocationLength,
						parpPattern->ExtendedException[i].WideCharLocation ? parpPattern->ExtendedException[i].WideCharLocation : L"");
					szExtendedException += szTmp;
				}
			}
			szTmp.FormatMessage(IDS_ARPEXRESERVED1,
				i, parpPattern->ExtendedException[i].ReservedBlockEE2Size);
			szExtendedException += szTmp;
			if (parpPattern->ExtendedException[i].ReservedBlockEE2Size)
			{
				SBinary sBin = { 0 };
				sBin.cb = parpPattern->ExtendedException[i].ReservedBlockEE2Size;
				sBin.lpb = parpPattern->ExtendedException[i].ReservedBlockEE2;
				szExtendedException += wstringToCString(BinToHexString(&sBin, true));
			}

			szARP += szExtendedException;
		}
	}

	szTmp.FormatMessage(IDS_ARPRESERVED2,
		parpPattern->ReservedBlock2Size);
	szARP += szTmp;
	if (parpPattern->ReservedBlock2Size)
	{
		SBinary sBin = { 0 };
		sBin.cb = parpPattern->ReservedBlock2Size;
		sBin.lpb = parpPattern->ReservedBlock2;
		szARP += wstringToCString(BinToHexString(&sBin, true));
	}

	szARP += JunkDataToString(parpPattern->JunkDataSize, parpPattern->JunkData);

	return CStringToLPWSTR(szARP);
} // AppointmentRecurrencePatternStructToString

//////////////////////////////////////////////////////////////////////////
// End AppointmentRecurrencePatternStruct
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// SDBin
//////////////////////////////////////////////////////////////////////////

void SDBinToString(SBinary myBin, _In_opt_ LPMAPIPROP lpMAPIProp, ULONG ulPropTag, _Deref_out_opt_z_ LPWSTR* lpszResultString)
{
	if (!lpszResultString) return;
	*lpszResultString = NULL;

	HRESULT hRes = S_OK;
	LPBYTE lpSDToParse = myBin.lpb;
	ULONG ulSDToParse = myBin.cb;

	if (lpSDToParse)
	{
		eAceType acetype = acetypeMessage;
		switch (GetMAPIObjectType(lpMAPIProp))
		{
		case (MAPI_STORE) :
		case (MAPI_ADDRBOOK) :
		case (MAPI_FOLDER) :
		case (MAPI_ABCONT) :
						   acetype = acetypeContainer;
			break;
		}

		if (PR_FREEBUSY_NT_SECURITY_DESCRIPTOR == ulPropTag)
			acetype = acetypeFreeBusy;

		CString szDACL;
		CString szInfo;
		CString szTmp;

		WC_H(SDToString(lpSDToParse, ulSDToParse, acetype, &szDACL, &szInfo));

		LPTSTR szFlags = NULL;
		InterpretFlags(flagSecurityVersion, SECURITY_DESCRIPTOR_VERSION(lpSDToParse), &szFlags);

		CString szResult;
		szResult.FormatMessage(IDS_SECURITYDESCRIPTORHEADER);
		szResult += szInfo;
		szTmp.FormatMessage(IDS_SECURITYDESCRIPTORVERSION, SECURITY_DESCRIPTOR_VERSION(lpSDToParse), szFlags);
		szResult += szTmp;
		szResult += szDACL;

		delete[] szFlags;
		szFlags = NULL;

		*lpszResultString = CStringToLPWSTR(szResult);
	}
} // SDBinToString

//////////////////////////////////////////////////////////////////////////
// SIDDBin
//////////////////////////////////////////////////////////////////////////

void SIDBinToString(SBinary myBin, _Deref_out_z_ LPWSTR* lpszResultString)
{
	if (!lpszResultString) return;
	HRESULT hRes = S_OK;
	PSID SidStart = myBin.lpb;
	LPTSTR lpSidName = NULL;
	LPTSTR lpSidDomain = NULL;
	LPTSTR lpStringSid = NULL;

	if (SidStart &&
		myBin.cb >= sizeof(SID) - sizeof(DWORD) + sizeof(DWORD)* ((PISID)SidStart)->SubAuthorityCount &&
		IsValidSid(SidStart))
	{
		DWORD dwSidName = 0;
		DWORD dwSidDomain = 0;
		SID_NAME_USE SidNameUse;

		if (!LookupAccountSid(
			NULL,
			SidStart,
			NULL,
			&dwSidName,
			NULL,
			&dwSidDomain,
			&SidNameUse))
		{
			DWORD dwErr = GetLastError();
			hRes = HRESULT_FROM_WIN32(dwErr);
			if (ERROR_NONE_MAPPED != dwErr &&
				STRSAFE_E_INSUFFICIENT_BUFFER != hRes)
			{
				LogFunctionCall(hRes, NULL, false, false, true, dwErr, "LookupAccountSid", __FILE__, __LINE__);
			}
		}
		hRes = S_OK;

#pragma warning(push)
#pragma warning(disable:6211)
		if (dwSidName) lpSidName = new TCHAR[dwSidName];
		if (dwSidDomain) lpSidDomain = new TCHAR[dwSidDomain];
#pragma warning(pop)

		// Only make the call if we got something to get
		if (lpSidName || lpSidDomain)
		{
			WC_B(LookupAccountSid(
				NULL,
				SidStart,
				lpSidName,
				&dwSidName,
				lpSidDomain,
				&dwSidDomain,
				&SidNameUse));
			hRes = S_OK;
		}

		EC_B(GetTextualSid(SidStart, &lpStringSid));
	}

	CString szDomain;
	CString szName;
	CString szSID;

	if (lpSidDomain) szDomain = lpSidDomain;
	else EC_B(szDomain.LoadString(IDS_NODOMAIN));
	if (lpSidName) szName = lpSidName;
	else EC_B(szName.LoadString(IDS_NONAME));
	if (lpStringSid) szSID = lpStringSid;
	else EC_B(szSID.LoadString(IDS_NOSID));

	CString szResult;
	szResult.FormatMessage(IDS_SIDHEADER, szDomain, szName, szSID);

	if (lpStringSid) delete[] lpStringSid;
	if (lpSidDomain) delete[] lpSidDomain;
	if (lpSidName) delete[] lpSidName;

	*lpszResultString = CStringToLPWSTR(szResult);
} // SIDBinToString

//////////////////////////////////////////////////////////////////////////
// ExtendedFlagsStruct
//////////////////////////////////////////////////////////////////////////

_Check_return_ ExtendedFlagsStruct* BinToExtendedFlagsStruct(ULONG cbBin, _In_count_(cbBin) LPBYTE lpBin)
{
	if (!lpBin) return NULL;

	ExtendedFlagsStruct efExtendedFlags = { 0 };
	CBinaryParser Parser(cbBin, lpBin);

	// Run through the parser once to count the number of flag structs
	for (;;)
	{
		// Must have at least 2 bytes left to have another flag
		if (Parser.RemainingBytes() < 2) break;
		BYTE ulId = NULL;
		BYTE cbData = NULL;
		Parser.GetBYTE(&ulId);
		Parser.GetBYTE(&cbData);
		// Must have at least cbData bytes left to be a valid flag
		if (Parser.RemainingBytes() < cbData) break;

		Parser.Advance(cbData);
		efExtendedFlags.ulNumFlags++;
	}

	// Set up to parse for real
	CBinaryParser Parser2(cbBin, lpBin);
	if (efExtendedFlags.ulNumFlags && efExtendedFlags.ulNumFlags < _MaxEntriesSmall)
		efExtendedFlags.pefExtendedFlags = new ExtendedFlagStruct[efExtendedFlags.ulNumFlags];

	if (efExtendedFlags.pefExtendedFlags)
	{
		memset(efExtendedFlags.pefExtendedFlags, 0, sizeof(ExtendedFlagStruct)*efExtendedFlags.ulNumFlags);
		ULONG i = 0;
		bool bBadData = false;

		for (i = 0; i < efExtendedFlags.ulNumFlags; i++)
		{
			Parser2.GetBYTE(&efExtendedFlags.pefExtendedFlags[i].Id);
			Parser2.GetBYTE(&efExtendedFlags.pefExtendedFlags[i].Cb);

			// If the structure says there's more bytes than remaining buffer, we're done parsing.
			if (Parser2.RemainingBytes() < efExtendedFlags.pefExtendedFlags[i].Cb)
			{
				efExtendedFlags.ulNumFlags = i;
				break;
			}

			switch (efExtendedFlags.pefExtendedFlags[i].Id)
			{
			case EFPB_FLAGS:
				if (efExtendedFlags.pefExtendedFlags[i].Cb == sizeof(DWORD))
					Parser2.GetDWORD(&efExtendedFlags.pefExtendedFlags[i].Data.ExtendedFlags);
				else
					bBadData = true;
				break;
			case EFPB_CLSIDID:
				if (efExtendedFlags.pefExtendedFlags[i].Cb == sizeof(GUID))
					Parser2.GetBYTESNoAlloc(sizeof(GUID), sizeof(GUID), (LPBYTE)&efExtendedFlags.pefExtendedFlags[i].Data.SearchFolderID);
				else
					bBadData = true;
				break;
			case EFPB_SFTAG:
				if (efExtendedFlags.pefExtendedFlags[i].Cb == sizeof(DWORD))
					Parser2.GetDWORD(&efExtendedFlags.pefExtendedFlags[i].Data.SearchFolderTag);
				else
					bBadData = true;
				break;
			case EFPB_TODO_VERSION:
				if (efExtendedFlags.pefExtendedFlags[i].Cb == sizeof(DWORD))
					Parser2.GetDWORD(&efExtendedFlags.pefExtendedFlags[i].Data.ToDoFolderVersion);
				else
					bBadData = true;
				break;
			default:
				Parser2.GetBYTES(efExtendedFlags.pefExtendedFlags[i].Cb, _MaxBytes, &efExtendedFlags.pefExtendedFlags[i].lpUnknownData);
				break;
			}

			// If we encountered a bad flag, stop parsing
			if (bBadData)
			{
				efExtendedFlags.ulNumFlags = i;
				break;
			}
		}
	}

	efExtendedFlags.JunkDataSize = Parser2.GetRemainingData(&efExtendedFlags.JunkData);

	ExtendedFlagsStruct* pefExtendedFlags = new ExtendedFlagsStruct;
	if (pefExtendedFlags)
	{
		*pefExtendedFlags = efExtendedFlags;
	}

	return pefExtendedFlags;
} // BinToExtendedFlagsStruct

void DeleteExtendedFlagsStruct(_In_ ExtendedFlagsStruct* pefExtendedFlags)
{
	if (!pefExtendedFlags) return;
	ULONG i = 0;
	if (pefExtendedFlags->ulNumFlags && pefExtendedFlags->pefExtendedFlags)
	{
		for (i = 0; i < pefExtendedFlags->ulNumFlags; i++)
		{
			delete[] pefExtendedFlags->pefExtendedFlags[i].lpUnknownData;
		}
	}
	delete[] pefExtendedFlags->pefExtendedFlags;
	delete[] pefExtendedFlags->JunkData;
	delete pefExtendedFlags;
} // DeleteExtendedFlagsStruct

// result allocated with new, clean up with delete[]
_Check_return_ LPWSTR ExtendedFlagsStructToString(_In_ ExtendedFlagsStruct* pefExtendedFlags)
{
	if (!pefExtendedFlags) return NULL;

	CString szExtendedFlags;
	CString szTmp;
	SBinary sBin = { 0 };

	szExtendedFlags.FormatMessage(IDS_EXTENDEDFLAGSHEADER, pefExtendedFlags->ulNumFlags);

	if (pefExtendedFlags->ulNumFlags && pefExtendedFlags->pefExtendedFlags)
	{
		ULONG i = 0;
		for (i = 0; i < pefExtendedFlags->ulNumFlags; i++)
		{
			LPTSTR szFlags = NULL;
			InterpretFlags(flagExtendedFolderFlagType, pefExtendedFlags->pefExtendedFlags[i].Id, &szFlags);
			szTmp.FormatMessage(IDS_EXTENDEDFLAGID,
				pefExtendedFlags->pefExtendedFlags[i].Id, szFlags,
				pefExtendedFlags->pefExtendedFlags[i].Cb);
			delete[] szFlags;
			szFlags = NULL;
			szExtendedFlags += szTmp;

			switch (pefExtendedFlags->pefExtendedFlags[i].Id)
			{
			case EFPB_FLAGS:
				InterpretFlags(flagExtendedFolderFlag, pefExtendedFlags->pefExtendedFlags[i].Data.ExtendedFlags, &szFlags);
				szTmp.FormatMessage(IDS_EXTENDEDFLAGDATAFLAG, pefExtendedFlags->pefExtendedFlags[i].Data.ExtendedFlags, szFlags);
				delete[] szFlags;
				szFlags = NULL;
				szExtendedFlags += szTmp;
				break;
			case EFPB_CLSIDID:
				szFlags = GUIDToString(&pefExtendedFlags->pefExtendedFlags[i].Data.SearchFolderID);
				szTmp.FormatMessage(IDS_EXTENDEDFLAGDATASFID, szFlags);
				delete[] szFlags;
				szFlags = NULL;
				szExtendedFlags += szTmp;
				break;
			case EFPB_SFTAG:
				szTmp.FormatMessage(IDS_EXTENDEDFLAGDATASFTAG,
					pefExtendedFlags->pefExtendedFlags[i].Data.SearchFolderTag);
				szExtendedFlags += szTmp;
				break;
			case EFPB_TODO_VERSION:
				szTmp.FormatMessage(IDS_EXTENDEDFLAGDATATODOVERSION, pefExtendedFlags->pefExtendedFlags[i].Data.ToDoFolderVersion);
				szExtendedFlags += szTmp;
				break;
			}
			if (pefExtendedFlags->pefExtendedFlags[i].lpUnknownData)
			{
				szTmp.FormatMessage(IDS_EXTENDEDFLAGUNKNOWN);
				szExtendedFlags += szTmp;
				sBin.cb = pefExtendedFlags->pefExtendedFlags[i].Cb;
				sBin.lpb = pefExtendedFlags->pefExtendedFlags[i].lpUnknownData;
				szExtendedFlags += wstringToCString(BinToHexString(&sBin, true));
			}
		}
	}

	szExtendedFlags += JunkDataToString(pefExtendedFlags->JunkDataSize, pefExtendedFlags->JunkData);

	return CStringToLPWSTR(szExtendedFlags);
} // ExtendedFlagsStructToString

//////////////////////////////////////////////////////////////////////////
// End ExtendedFlagsStruct
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// TimeZoneStruct
//////////////////////////////////////////////////////////////////////////

// Allocates return value with new. Clean up with DeleteTimeZoneStruct.
_Check_return_ TimeZoneStruct* BinToTimeZoneStruct(ULONG cbBin, _In_count_(cbBin) LPBYTE lpBin)
{
	if (!lpBin) return NULL;

	TimeZoneStruct tzTimeZone = { 0 };
	CBinaryParser Parser(cbBin, lpBin);

	Parser.GetDWORD(&tzTimeZone.lBias);
	Parser.GetDWORD(&tzTimeZone.lStandardBias);
	Parser.GetDWORD(&tzTimeZone.lDaylightBias);
	Parser.GetWORD(&tzTimeZone.wStandardYear);
	Parser.GetWORD(&tzTimeZone.stStandardDate.wYear);
	Parser.GetWORD(&tzTimeZone.stStandardDate.wMonth);
	Parser.GetWORD(&tzTimeZone.stStandardDate.wDayOfWeek);
	Parser.GetWORD(&tzTimeZone.stStandardDate.wDay);
	Parser.GetWORD(&tzTimeZone.stStandardDate.wHour);
	Parser.GetWORD(&tzTimeZone.stStandardDate.wMinute);
	Parser.GetWORD(&tzTimeZone.stStandardDate.wSecond);
	Parser.GetWORD(&tzTimeZone.stStandardDate.wMilliseconds);
	Parser.GetWORD(&tzTimeZone.wDaylightDate);
	Parser.GetWORD(&tzTimeZone.stDaylightDate.wYear);
	Parser.GetWORD(&tzTimeZone.stDaylightDate.wMonth);
	Parser.GetWORD(&tzTimeZone.stDaylightDate.wDayOfWeek);
	Parser.GetWORD(&tzTimeZone.stDaylightDate.wDay);
	Parser.GetWORD(&tzTimeZone.stDaylightDate.wHour);
	Parser.GetWORD(&tzTimeZone.stDaylightDate.wMinute);
	Parser.GetWORD(&tzTimeZone.stDaylightDate.wSecond);
	Parser.GetWORD(&tzTimeZone.stDaylightDate.wMilliseconds);

	tzTimeZone.JunkDataSize = Parser.GetRemainingData(&tzTimeZone.JunkData);

	TimeZoneStruct* ptzTimeZone = new TimeZoneStruct;
	if (ptzTimeZone)
	{
		*ptzTimeZone = tzTimeZone;
	}

	return ptzTimeZone;
} // BinToTimeZoneStruct

void DeleteTimeZoneStruct(_In_ TimeZoneStruct* ptzTimeZone)
{
	if (!ptzTimeZone) return;
	delete[] ptzTimeZone->JunkData;
	delete ptzTimeZone;
} // DeleteTimeZoneStruct

// result allocated with new, clean up with delete[]
_Check_return_ LPWSTR TimeZoneStructToString(_In_ TimeZoneStruct* ptzTimeZone)
{
	if (!ptzTimeZone) return NULL;

	CString szTimeZone;

	szTimeZone.FormatMessage(IDS_TIMEZONE,
		ptzTimeZone->lBias,
		ptzTimeZone->lStandardBias,
		ptzTimeZone->lDaylightBias,
		ptzTimeZone->wStandardYear,
		ptzTimeZone->stStandardDate.wYear,
		ptzTimeZone->stStandardDate.wMonth,
		ptzTimeZone->stStandardDate.wDayOfWeek,
		ptzTimeZone->stStandardDate.wDay,
		ptzTimeZone->stStandardDate.wHour,
		ptzTimeZone->stStandardDate.wMinute,
		ptzTimeZone->stStandardDate.wSecond,
		ptzTimeZone->stStandardDate.wMilliseconds,
		ptzTimeZone->wDaylightDate,
		ptzTimeZone->stDaylightDate.wYear,
		ptzTimeZone->stDaylightDate.wMonth,
		ptzTimeZone->stDaylightDate.wDayOfWeek,
		ptzTimeZone->stDaylightDate.wDay,
		ptzTimeZone->stDaylightDate.wHour,
		ptzTimeZone->stDaylightDate.wMinute,
		ptzTimeZone->stDaylightDate.wSecond,
		ptzTimeZone->stDaylightDate.wMilliseconds);

	szTimeZone += JunkDataToString(ptzTimeZone->JunkDataSize, ptzTimeZone->JunkData);

	return CStringToLPWSTR(szTimeZone);
} // TimeZoneStructToString

//////////////////////////////////////////////////////////////////////////
// End TimeZoneStruct
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// TimeZoneDefinitionStruct
//////////////////////////////////////////////////////////////////////////

// Allocates return value with new. Clean up with DeleteTimeZoneDefinitionStruct.
_Check_return_ TimeZoneDefinitionStruct* BinToTimeZoneDefinitionStruct(ULONG cbBin, _In_count_(cbBin) LPBYTE lpBin)
{
	if (!lpBin) return NULL;

	TimeZoneDefinitionStruct tzdTimeZoneDefinition = { 0 };
	CBinaryParser Parser(cbBin, lpBin);

	Parser.GetBYTE(&tzdTimeZoneDefinition.bMajorVersion);
	Parser.GetBYTE(&tzdTimeZoneDefinition.bMinorVersion);
	Parser.GetWORD(&tzdTimeZoneDefinition.cbHeader);
	Parser.GetWORD(&tzdTimeZoneDefinition.wReserved);
	Parser.GetWORD(&tzdTimeZoneDefinition.cchKeyName);
	Parser.GetStringW(tzdTimeZoneDefinition.cchKeyName, &tzdTimeZoneDefinition.szKeyName);
	Parser.GetWORD(&tzdTimeZoneDefinition.cRules);

	if (tzdTimeZoneDefinition.cRules && tzdTimeZoneDefinition.cRules < _MaxEntriesSmall)
		tzdTimeZoneDefinition.lpTZRule = new TZRule[tzdTimeZoneDefinition.cRules];

	if (tzdTimeZoneDefinition.lpTZRule)
	{
		memset(tzdTimeZoneDefinition.lpTZRule, 0, sizeof(TZRule)* tzdTimeZoneDefinition.cRules);
		ULONG i = 0;
		for (i = 0; i < tzdTimeZoneDefinition.cRules; i++)
		{
			Parser.GetBYTE(&tzdTimeZoneDefinition.lpTZRule[i].bMajorVersion);
			Parser.GetBYTE(&tzdTimeZoneDefinition.lpTZRule[i].bMinorVersion);
			Parser.GetWORD(&tzdTimeZoneDefinition.lpTZRule[i].wReserved);
			Parser.GetWORD(&tzdTimeZoneDefinition.lpTZRule[i].wTZRuleFlags);
			Parser.GetWORD(&tzdTimeZoneDefinition.lpTZRule[i].wYear);
			Parser.GetBYTESNoAlloc(sizeof(tzdTimeZoneDefinition.lpTZRule[i].X), sizeof(tzdTimeZoneDefinition.lpTZRule[i].X), tzdTimeZoneDefinition.lpTZRule[i].X);
			Parser.GetDWORD(&tzdTimeZoneDefinition.lpTZRule[i].lBias);
			Parser.GetDWORD(&tzdTimeZoneDefinition.lpTZRule[i].lStandardBias);
			Parser.GetDWORD(&tzdTimeZoneDefinition.lpTZRule[i].lDaylightBias);
			Parser.GetWORD(&tzdTimeZoneDefinition.lpTZRule[i].stStandardDate.wYear);
			Parser.GetWORD(&tzdTimeZoneDefinition.lpTZRule[i].stStandardDate.wMonth);
			Parser.GetWORD(&tzdTimeZoneDefinition.lpTZRule[i].stStandardDate.wDayOfWeek);
			Parser.GetWORD(&tzdTimeZoneDefinition.lpTZRule[i].stStandardDate.wDay);
			Parser.GetWORD(&tzdTimeZoneDefinition.lpTZRule[i].stStandardDate.wHour);
			Parser.GetWORD(&tzdTimeZoneDefinition.lpTZRule[i].stStandardDate.wMinute);
			Parser.GetWORD(&tzdTimeZoneDefinition.lpTZRule[i].stStandardDate.wSecond);
			Parser.GetWORD(&tzdTimeZoneDefinition.lpTZRule[i].stStandardDate.wMilliseconds);
			Parser.GetWORD(&tzdTimeZoneDefinition.lpTZRule[i].stDaylightDate.wYear);
			Parser.GetWORD(&tzdTimeZoneDefinition.lpTZRule[i].stDaylightDate.wMonth);
			Parser.GetWORD(&tzdTimeZoneDefinition.lpTZRule[i].stDaylightDate.wDayOfWeek);
			Parser.GetWORD(&tzdTimeZoneDefinition.lpTZRule[i].stDaylightDate.wDay);
			Parser.GetWORD(&tzdTimeZoneDefinition.lpTZRule[i].stDaylightDate.wHour);
			Parser.GetWORD(&tzdTimeZoneDefinition.lpTZRule[i].stDaylightDate.wMinute);
			Parser.GetWORD(&tzdTimeZoneDefinition.lpTZRule[i].stDaylightDate.wSecond);
			Parser.GetWORD(&tzdTimeZoneDefinition.lpTZRule[i].stDaylightDate.wMilliseconds);
		}
	}

	tzdTimeZoneDefinition.JunkDataSize = Parser.GetRemainingData(&tzdTimeZoneDefinition.JunkData);

	TimeZoneDefinitionStruct* ptzdTimeZoneDefinition = new TimeZoneDefinitionStruct;
	if (ptzdTimeZoneDefinition)
	{
		*ptzdTimeZoneDefinition = tzdTimeZoneDefinition;
	}

	return ptzdTimeZoneDefinition;
} // BinToTimeZoneDefinitionStruct

void DeleteTimeZoneDefinitionStruct(_In_ TimeZoneDefinitionStruct* ptzdTimeZoneDefinition)
{
	if (!ptzdTimeZoneDefinition) return;
	delete[] ptzdTimeZoneDefinition->szKeyName;
	delete[] ptzdTimeZoneDefinition->lpTZRule;
	delete[] ptzdTimeZoneDefinition->JunkData;
	delete ptzdTimeZoneDefinition;
} // DeleteTimeZoneDefinitionStruct

// result allocated with new, clean up with delete[]
_Check_return_ LPWSTR TimeZoneDefinitionStructToString(_In_ TimeZoneDefinitionStruct* ptzdTimeZoneDefinition)
{
	if (!ptzdTimeZoneDefinition) return NULL;

	CString szTimeZoneDefinition;
	CString szTmp;

	szTimeZoneDefinition.FormatMessage(IDS_TIMEZONEDEFINITION,
		ptzdTimeZoneDefinition->bMajorVersion,
		ptzdTimeZoneDefinition->bMinorVersion,
		ptzdTimeZoneDefinition->cbHeader,
		ptzdTimeZoneDefinition->wReserved,
		ptzdTimeZoneDefinition->cchKeyName,
		ptzdTimeZoneDefinition->szKeyName,
		ptzdTimeZoneDefinition->cRules);

	if (ptzdTimeZoneDefinition->cRules && ptzdTimeZoneDefinition->lpTZRule)
	{
		CString szTZRule;

		WORD i = 0;

		for (i = 0; i < ptzdTimeZoneDefinition->cRules; i++)
		{
			LPTSTR szFlags = NULL;
			InterpretFlags(flagTZRule, ptzdTimeZoneDefinition->lpTZRule[i].wTZRuleFlags, &szFlags);
			szTZRule.FormatMessage(IDS_TZRULEHEADER,
				i,
				ptzdTimeZoneDefinition->lpTZRule[i].bMajorVersion,
				ptzdTimeZoneDefinition->lpTZRule[i].bMinorVersion,
				ptzdTimeZoneDefinition->lpTZRule[i].wReserved,
				ptzdTimeZoneDefinition->lpTZRule[i].wTZRuleFlags,
				szFlags,
				ptzdTimeZoneDefinition->lpTZRule[i].wYear);
			delete[] szFlags;
			szFlags = NULL;

			SBinary sBin = { 0 };
			sBin.cb = 14;
			sBin.lpb = ptzdTimeZoneDefinition->lpTZRule[i].X;
			szTZRule += wstringToCString(BinToHexString(&sBin, true));

			szTmp.FormatMessage(IDS_TZRULEFOOTER,
				i,
				ptzdTimeZoneDefinition->lpTZRule[i].lBias,
				ptzdTimeZoneDefinition->lpTZRule[i].lStandardBias,
				ptzdTimeZoneDefinition->lpTZRule[i].lDaylightBias,
				ptzdTimeZoneDefinition->lpTZRule[i].stStandardDate.wYear,
				ptzdTimeZoneDefinition->lpTZRule[i].stStandardDate.wMonth,
				ptzdTimeZoneDefinition->lpTZRule[i].stStandardDate.wDayOfWeek,
				ptzdTimeZoneDefinition->lpTZRule[i].stStandardDate.wDay,
				ptzdTimeZoneDefinition->lpTZRule[i].stStandardDate.wHour,
				ptzdTimeZoneDefinition->lpTZRule[i].stStandardDate.wMinute,
				ptzdTimeZoneDefinition->lpTZRule[i].stStandardDate.wSecond,
				ptzdTimeZoneDefinition->lpTZRule[i].stStandardDate.wMilliseconds,
				ptzdTimeZoneDefinition->lpTZRule[i].stDaylightDate.wYear,
				ptzdTimeZoneDefinition->lpTZRule[i].stDaylightDate.wMonth,
				ptzdTimeZoneDefinition->lpTZRule[i].stDaylightDate.wDayOfWeek,
				ptzdTimeZoneDefinition->lpTZRule[i].stDaylightDate.wDay,
				ptzdTimeZoneDefinition->lpTZRule[i].stDaylightDate.wHour,
				ptzdTimeZoneDefinition->lpTZRule[i].stDaylightDate.wMinute,
				ptzdTimeZoneDefinition->lpTZRule[i].stDaylightDate.wSecond,
				ptzdTimeZoneDefinition->lpTZRule[i].stDaylightDate.wMilliseconds);
			szTZRule += szTmp;

			szTimeZoneDefinition += szTZRule;
		}
	}

	szTimeZoneDefinition += JunkDataToString(ptzdTimeZoneDefinition->JunkDataSize, ptzdTimeZoneDefinition->JunkData);

	return CStringToLPWSTR(szTimeZoneDefinition);
} // TimeZoneDefinitionStructToString

//////////////////////////////////////////////////////////////////////////
// End TimeZoneDefinitionStruct
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// ReportTagStruct
//////////////////////////////////////////////////////////////////////////

// Allocates return value with new. Clean up with DeleteReportTagStruct.
_Check_return_ ReportTagStruct* BinToReportTagStruct(ULONG cbBin, _In_count_(cbBin) LPBYTE lpBin)
{
	if (!lpBin) return NULL;

	ReportTagStruct rtReportTag = { 0 };
	CBinaryParser Parser(cbBin, lpBin);

	Parser.GetBYTESNoAlloc(sizeof(rtReportTag.Cookie), sizeof(rtReportTag.Cookie), (LPBYTE)rtReportTag.Cookie);

	// Version is big endian, so we have to read individual bytes
	WORD hiWord = NULL;
	WORD loWord = NULL;
	Parser.GetWORD(&hiWord);
	Parser.GetWORD(&loWord);
	rtReportTag.Version = (hiWord << 16) | loWord;

	Parser.GetDWORD(&rtReportTag.cbStoreEntryID);
	if (rtReportTag.cbStoreEntryID)
	{
		Parser.GetBYTES(rtReportTag.cbStoreEntryID, _MaxEID, &rtReportTag.lpStoreEntryID);
	}
	Parser.GetDWORD(&rtReportTag.cbFolderEntryID);
	if (rtReportTag.cbFolderEntryID)
	{
		Parser.GetBYTES(rtReportTag.cbFolderEntryID, _MaxEID, &rtReportTag.lpFolderEntryID);
	}
	Parser.GetDWORD(&rtReportTag.cbMessageEntryID);
	if (rtReportTag.cbMessageEntryID)
	{
		Parser.GetBYTES(rtReportTag.cbMessageEntryID, _MaxEID, &rtReportTag.lpMessageEntryID);
	}
	Parser.GetDWORD(&rtReportTag.cbSearchFolderEntryID);
	if (rtReportTag.cbSearchFolderEntryID)
	{
		Parser.GetBYTES(rtReportTag.cbSearchFolderEntryID, _MaxEID, &rtReportTag.lpSearchFolderEntryID);
	}
	Parser.GetDWORD(&rtReportTag.cbMessageSearchKey);
	if (rtReportTag.cbMessageSearchKey)
	{
		Parser.GetBYTES(rtReportTag.cbMessageSearchKey, _MaxEID, &rtReportTag.lpMessageSearchKey);
	}
	Parser.GetDWORD(&rtReportTag.cchAnsiText);
	if (rtReportTag.cchAnsiText)
	{
		Parser.GetStringA(rtReportTag.cchAnsiText, &rtReportTag.lpszAnsiText);
	}

	rtReportTag.JunkDataSize = Parser.GetRemainingData(&rtReportTag.JunkData);

	ReportTagStruct* prtReportTag = new ReportTagStruct;
	if (prtReportTag)
	{
		*prtReportTag = rtReportTag;
	}

	return prtReportTag;
} // BinToReportTagStruct

void DeleteReportTagStruct(_In_ ReportTagStruct* prtReportTag)
{
	if (!prtReportTag) return;
	delete[] prtReportTag->lpStoreEntryID;
	delete[] prtReportTag->lpFolderEntryID;
	delete[] prtReportTag->lpMessageEntryID;
	delete[] prtReportTag->lpSearchFolderEntryID;
	delete[] prtReportTag->lpMessageSearchKey;
	delete[] prtReportTag->lpszAnsiText;
	delete[] prtReportTag->JunkData;
	delete prtReportTag;
} // DeleteReportTagStruct

// result allocated with new, clean up with delete[]
_Check_return_ LPWSTR ReportTagStructToString(_In_ ReportTagStruct* prtReportTag)
{
	if (!prtReportTag) return NULL;

	CString szReportTag;
	CString szTmp;

	szReportTag.FormatMessage(IDS_REPORTTAGHEADER);

	SBinary sBin = { 0 };
	sBin.cb = sizeof(prtReportTag->Cookie);
	sBin.lpb = (LPBYTE)prtReportTag->Cookie;
	szReportTag += wstringToCString(BinToHexString(&sBin, true));

	LPTSTR szFlags = NULL;
	InterpretFlags(flagReportTagVersion, prtReportTag->Version, &szFlags);
	szTmp.FormatMessage(IDS_REPORTTAGVERSION,
		prtReportTag->Version,
		szFlags);
	delete[] szFlags;
	szFlags = NULL;
	szReportTag += szTmp;

	if (prtReportTag->cbStoreEntryID)
	{
		szTmp.FormatMessage(IDS_REPORTTAGSTOREEID);
		szReportTag += szTmp;
		sBin.cb = prtReportTag->cbStoreEntryID;
		sBin.lpb = prtReportTag->lpStoreEntryID;
		szReportTag += wstringToCString(BinToHexString(&sBin, true));
	}

	if (prtReportTag->cbFolderEntryID)
	{
		szTmp.FormatMessage(IDS_REPORTTAGFOLDEREID);
		szReportTag += szTmp;
		sBin.cb = prtReportTag->cbFolderEntryID;
		sBin.lpb = prtReportTag->lpFolderEntryID;
		szReportTag += wstringToCString(BinToHexString(&sBin, true));
	}

	if (prtReportTag->cbMessageEntryID)
	{
		szTmp.FormatMessage(IDS_REPORTTAGMESSAGEEID);
		szReportTag += szTmp;
		sBin.cb = prtReportTag->cbMessageEntryID;
		sBin.lpb = prtReportTag->lpMessageEntryID;
		szReportTag += wstringToCString(BinToHexString(&sBin, true));
	}

	if (prtReportTag->cbSearchFolderEntryID)
	{
		szTmp.FormatMessage(IDS_REPORTTAGSFEID);
		szReportTag += szTmp;
		sBin.cb = prtReportTag->cbSearchFolderEntryID;
		sBin.lpb = prtReportTag->lpSearchFolderEntryID;
		szReportTag += wstringToCString(BinToHexString(&sBin, true));
	}

	if (prtReportTag->cbMessageSearchKey)
	{
		szTmp.FormatMessage(IDS_REPORTTAGMESSAGEKEY);
		szReportTag += szTmp;
		sBin.cb = prtReportTag->cbMessageSearchKey;
		sBin.lpb = prtReportTag->lpMessageSearchKey;
		szReportTag += wstringToCString(BinToHexString(&sBin, true));
	}

	if (prtReportTag->cchAnsiText)
	{
		szTmp.FormatMessage(IDS_REPORTTAGANSITEXT,
			prtReportTag->cchAnsiText,
			prtReportTag->lpszAnsiText ? prtReportTag->lpszAnsiText : ""); // STRING_OK
		szReportTag += szTmp;
	}

	szReportTag += JunkDataToString(prtReportTag->JunkDataSize, prtReportTag->JunkData);

	return CStringToLPWSTR(szReportTag);
} // ReportTagStructToString

//////////////////////////////////////////////////////////////////////////
// End ReportTagStruct
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// ConversationIndexStruct
//////////////////////////////////////////////////////////////////////////

// Allocates return value with new. Clean up with DeleteConversationIndexStruct.
_Check_return_ ConversationIndexStruct* BinToConversationIndexStruct(ULONG cbBin, _In_count_(cbBin) LPBYTE lpBin)
{
	if (!lpBin) return NULL;

	ConversationIndexStruct ciConversationIndex = { 0 };
	CBinaryParser Parser(cbBin, lpBin);

	Parser.GetBYTE(&ciConversationIndex.UnnamedByte);
	BYTE b1 = NULL;
	BYTE b2 = NULL;
	BYTE b3 = NULL;
	BYTE b4 = NULL;
	Parser.GetBYTE(&b1);
	Parser.GetBYTE(&b2);
	Parser.GetBYTE(&b3);
	ciConversationIndex.ftCurrent.dwHighDateTime = (b1 << 16) | (b2 << 8) | b3;
	Parser.GetBYTE(&b1);
	Parser.GetBYTE(&b2);
	ciConversationIndex.ftCurrent.dwLowDateTime = (b1 << 24) | (b2 << 16);
	Parser.GetBYTE(&b1);
	Parser.GetBYTE(&b2);
	Parser.GetBYTE(&b3);
	Parser.GetBYTE(&b4);
	ciConversationIndex.guid.Data1 = (b1 << 24) | (b2 << 16) | (b3 << 8) | b4;
	Parser.GetBYTE(&b1);
	Parser.GetBYTE(&b2);
	ciConversationIndex.guid.Data2 = (unsigned short)((b1 << 8) | b2);
	Parser.GetBYTE(&b1);
	Parser.GetBYTE(&b2);
	ciConversationIndex.guid.Data3 = (unsigned short)((b1 << 8) | b2);
	Parser.GetBYTESNoAlloc(sizeof(ciConversationIndex.guid.Data4), sizeof(ciConversationIndex.guid.Data4), ciConversationIndex.guid.Data4);

	if (Parser.RemainingBytes() > 0)
	{
		ciConversationIndex.ulResponseLevels = (ULONG)Parser.RemainingBytes() / 5; // Response levels consume 5 bytes each
	}

	if (ciConversationIndex.ulResponseLevels && ciConversationIndex.ulResponseLevels < _MaxEntriesSmall)
		ciConversationIndex.lpResponseLevels = new ResponseLevelStruct[ciConversationIndex.ulResponseLevels];

	if (ciConversationIndex.lpResponseLevels)
	{
		memset(ciConversationIndex.lpResponseLevels, 0, sizeof(ResponseLevelStruct)* ciConversationIndex.ulResponseLevels);
		ULONG i = 0;
		for (i = 0; i < ciConversationIndex.ulResponseLevels; i++)
		{
			Parser.GetBYTE(&b1);
			Parser.GetBYTE(&b2);
			Parser.GetBYTE(&b3);
			Parser.GetBYTE(&b4);
			ciConversationIndex.lpResponseLevels[i].TimeDelta = (b1 << 24) | (b2 << 16) | (b3 << 8) | b4;
			if (ciConversationIndex.lpResponseLevels[i].TimeDelta & 0x80000000)
			{
				ciConversationIndex.lpResponseLevels[i].TimeDelta = ciConversationIndex.lpResponseLevels[i].TimeDelta & ~0x80000000;
				ciConversationIndex.lpResponseLevels[i].DeltaCode = true;
			}
			Parser.GetBYTE(&b1);
			ciConversationIndex.lpResponseLevels[i].Random = (BYTE)(b1 >> 4);
			ciConversationIndex.lpResponseLevels[i].ResponseLevel = (BYTE)(b1 & 0xf);
		}
	}

	ciConversationIndex.JunkDataSize = Parser.GetRemainingData(&ciConversationIndex.JunkData);

	ConversationIndexStruct* pciConversationIndex = new ConversationIndexStruct;
	if (pciConversationIndex)
	{
		*pciConversationIndex = ciConversationIndex;
	}

	return pciConversationIndex;
} // BinToConversationIndexStruct

void DeleteConversationIndexStruct(_In_ ConversationIndexStruct* pciConversationIndex)
{
	if (!pciConversationIndex) return;
	delete[] pciConversationIndex->lpResponseLevels;
	delete[] pciConversationIndex->JunkData;
	delete pciConversationIndex;
} // DeleteConversationIndexStruct

// result allocated with new, clean up with delete[]
_Check_return_ LPWSTR ConversationIndexStructToString(_In_ ConversationIndexStruct* pciConversationIndex)
{
	if (!pciConversationIndex) return NULL;

	CString szConversationIndex;
	CString szTmp;

	CString PropString;
	FileTimeToString(&pciConversationIndex->ftCurrent, &PropString, NULL);
	LPTSTR szGUID = GUIDToString(&pciConversationIndex->guid);
	szTmp.FormatMessage(IDS_CONVERSATIONINDEXHEADER,
		pciConversationIndex->UnnamedByte,
		pciConversationIndex->ftCurrent.dwLowDateTime,
		pciConversationIndex->ftCurrent.dwHighDateTime,
		PropString,
		szGUID);
	szConversationIndex += szTmp;
	delete[] szGUID;

	if (pciConversationIndex->ulResponseLevels && pciConversationIndex->lpResponseLevels)
	{
		ULONG i = 0;
		for (i = 0; i < pciConversationIndex->ulResponseLevels; i++)
		{
			szTmp.FormatMessage(IDS_CONVERSATIONINDEXRESPONSELEVEL,
				i, pciConversationIndex->lpResponseLevels[i].DeltaCode,
				pciConversationIndex->lpResponseLevels[i].TimeDelta,
				pciConversationIndex->lpResponseLevels[i].Random,
				pciConversationIndex->lpResponseLevels[i].ResponseLevel);
			szConversationIndex += szTmp;
		}
	}

	szConversationIndex += JunkDataToString(pciConversationIndex->JunkDataSize, pciConversationIndex->JunkData);

	return CStringToLPWSTR(szConversationIndex);
} // ConversationIndexStructToString

//////////////////////////////////////////////////////////////////////////
// ConversationIndexStruct
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// TaskAssignersStruct
//////////////////////////////////////////////////////////////////////////

// Allocates return value with new. Clean up with DeleteTaskAssignersStruct.
_Check_return_ TaskAssignersStruct* BinToTaskAssignersStruct(ULONG cbBin, _In_count_(cbBin) LPBYTE lpBin)
{
	if (!lpBin) return NULL;

	TaskAssignersStruct taTaskAssigners = { 0 };
	CBinaryParser Parser(cbBin, lpBin);

	Parser.GetDWORD(&taTaskAssigners.cAssigners);

	if (taTaskAssigners.cAssigners && taTaskAssigners.cAssigners < _MaxEntriesSmall)
		taTaskAssigners.lpTaskAssigners = new TaskAssignerStruct[taTaskAssigners.cAssigners];

	if (taTaskAssigners.lpTaskAssigners)
	{
		memset(taTaskAssigners.lpTaskAssigners, 0, sizeof(TaskAssignerStruct)* taTaskAssigners.cAssigners);
		DWORD i = 0;
		for (i = 0; i < taTaskAssigners.cAssigners; i++)
		{
			Parser.GetDWORD(&taTaskAssigners.lpTaskAssigners[i].cbAssigner);
			LPBYTE lpbAssigner = NULL;
			Parser.GetBYTES(taTaskAssigners.lpTaskAssigners[i].cbAssigner, _MaxEntriesSmall, &lpbAssigner);
			if (lpbAssigner)
			{
				CBinaryParser AssignerParser(taTaskAssigners.lpTaskAssigners[i].cbAssigner, lpbAssigner);
				AssignerParser.GetDWORD(&taTaskAssigners.lpTaskAssigners[i].cbEntryID);
				AssignerParser.GetBYTES(taTaskAssigners.lpTaskAssigners[i].cbEntryID, _MaxEID, &taTaskAssigners.lpTaskAssigners[i].lpEntryID);
				AssignerParser.GetStringA(&taTaskAssigners.lpTaskAssigners[i].szDisplayName);
				AssignerParser.GetStringW(&taTaskAssigners.lpTaskAssigners[i].wzDisplayName);

				taTaskAssigners.lpTaskAssigners[i].JunkDataSize = AssignerParser.GetRemainingData(&taTaskAssigners.lpTaskAssigners[i].JunkData);
				delete[] lpbAssigner;
			}
		}
	}

	taTaskAssigners.JunkDataSize = Parser.GetRemainingData(&taTaskAssigners.JunkData);

	TaskAssignersStruct* ptaTaskAssigners = new TaskAssignersStruct;
	if (ptaTaskAssigners)
	{
		*ptaTaskAssigners = taTaskAssigners;
	}

	return ptaTaskAssigners;
} // BinToTaskAssignersStruct

void DeleteTaskAssignersStruct(_In_ TaskAssignersStruct* ptaTaskAssigners)
{
	if (!ptaTaskAssigners) return;
	DWORD i = 0;
	if (ptaTaskAssigners->cAssigners && ptaTaskAssigners->lpTaskAssigners)
	{
		for (i = 0; i < ptaTaskAssigners->cAssigners; i++)
		{
			delete[] ptaTaskAssigners->lpTaskAssigners[i].lpEntryID;
			delete[] ptaTaskAssigners->lpTaskAssigners[i].szDisplayName;
			delete[] ptaTaskAssigners->lpTaskAssigners[i].wzDisplayName;
			delete[] ptaTaskAssigners->lpTaskAssigners[i].JunkData;
		}
	}
	delete[] ptaTaskAssigners->lpTaskAssigners;
	delete[] ptaTaskAssigners->JunkData;
	delete ptaTaskAssigners;
} // DeleteTaskAssignersStruct

// result allocated with new, clean up with delete[]
_Check_return_ LPWSTR TaskAssignersStructToString(_In_ TaskAssignersStruct* ptaTaskAssigners)
{
	if (!ptaTaskAssigners) return NULL;

	CString szTaskAssigners;
	CString szTmp;

	szTaskAssigners.FormatMessage(IDS_TASKASSIGNERSHEADER,
		ptaTaskAssigners->cAssigners);

	if (ptaTaskAssigners->cAssigners && ptaTaskAssigners->lpTaskAssigners)
	{
		DWORD i = 0;
		for (i = 0; i < ptaTaskAssigners->cAssigners; i++)
		{
			szTmp.FormatMessage(IDS_TASKASSIGNEREID,
				i,
				ptaTaskAssigners->lpTaskAssigners[i].cbEntryID);
			szTaskAssigners += szTmp;
			if (ptaTaskAssigners->lpTaskAssigners[i].lpEntryID)
			{
				SBinary sBin = { 0 };
				sBin.cb = ptaTaskAssigners->lpTaskAssigners[i].cbEntryID;
				sBin.lpb = ptaTaskAssigners->lpTaskAssigners[i].lpEntryID;
				szTaskAssigners += wstringToCString(BinToHexString(&sBin, true));
			}
			szTmp.FormatMessage(IDS_TASKASSIGNERNAME,
				ptaTaskAssigners->lpTaskAssigners[i].szDisplayName,
				ptaTaskAssigners->lpTaskAssigners[i].wzDisplayName);
			szTaskAssigners += szTmp;

			if (ptaTaskAssigners->lpTaskAssigners[i].JunkDataSize)
			{
				szTmp.FormatMessage(IDS_TASKASSIGNERJUNKDATA,
					ptaTaskAssigners->lpTaskAssigners[i].JunkDataSize);
				szTaskAssigners += szTmp;
				SBinary sBin = { 0 };
				sBin.cb = (ULONG)ptaTaskAssigners->lpTaskAssigners[i].JunkDataSize;
				sBin.lpb = ptaTaskAssigners->lpTaskAssigners[i].JunkData;
				szTaskAssigners += wstringToCString(BinToHexString(&sBin, true));
			}
		}
	}

	szTaskAssigners += JunkDataToString(ptaTaskAssigners->JunkDataSize, ptaTaskAssigners->JunkData);

	return CStringToLPWSTR(szTaskAssigners);
} // TaskAssignersStructToString

//////////////////////////////////////////////////////////////////////////
// End TaskAssignersStruct
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// GlobalObjectIdStruct
//////////////////////////////////////////////////////////////////////////

// Allocates return value with new. Clean up with DeleteGlobalObjectIdStruct.
_Check_return_ GlobalObjectIdStruct* BinToGlobalObjectIdStruct(ULONG cbBin, _In_count_(cbBin) LPBYTE lpBin)
{
	if (!lpBin) return NULL;

	GlobalObjectIdStruct goidGlobalObjectId = { 0 };
	CBinaryParser Parser(cbBin, lpBin);

	Parser.GetBYTESNoAlloc(sizeof(goidGlobalObjectId.Id), sizeof(goidGlobalObjectId.Id), (LPBYTE)&goidGlobalObjectId.Id);
	BYTE b1 = NULL;
	BYTE b2 = NULL;
	Parser.GetBYTE(&b1);
	Parser.GetBYTE(&b2);
	goidGlobalObjectId.Year = (WORD)((b1 << 8) | b2);
	Parser.GetBYTE(&goidGlobalObjectId.Month);
	Parser.GetBYTE(&goidGlobalObjectId.Day);
	Parser.GetLARGE_INTEGER((LARGE_INTEGER*)&goidGlobalObjectId.CreationTime);
	Parser.GetLARGE_INTEGER(&goidGlobalObjectId.X);
	Parser.GetDWORD(&goidGlobalObjectId.dwSize);
	Parser.GetBYTES(goidGlobalObjectId.dwSize, _MaxBytes, &goidGlobalObjectId.lpData);

	goidGlobalObjectId.JunkDataSize = Parser.GetRemainingData(&goidGlobalObjectId.JunkData);

	GlobalObjectIdStruct* pgoidGlobalObjectId = new GlobalObjectIdStruct;
	if (pgoidGlobalObjectId)
	{
		*pgoidGlobalObjectId = goidGlobalObjectId;
	}

	return pgoidGlobalObjectId;
} // BinToGlobalObjectIdStruct

void DeleteGlobalObjectIdStruct(_In_opt_ GlobalObjectIdStruct* pgoidGlobalObjectId)
{
	if (!pgoidGlobalObjectId) return;
	delete[] pgoidGlobalObjectId->lpData;
	delete[] pgoidGlobalObjectId->JunkData;
	delete pgoidGlobalObjectId;
} // DeleteGlobalObjectIdStruct

// result allocated with new, clean up with delete[]
_Check_return_ LPWSTR GlobalObjectIdStructToString(_In_ GlobalObjectIdStruct* pgoidGlobalObjectId)
{
	if (!pgoidGlobalObjectId) return NULL;

	CString szGlobalObjectId;
	CString szTmp;

	szGlobalObjectId.FormatMessage(IDS_GLOBALOBJECTIDHEADER);

	SBinary sBin = { 0 };
	sBin.cb = sizeof(pgoidGlobalObjectId->Id);
	sBin.lpb = pgoidGlobalObjectId->Id;
	szGlobalObjectId += wstringToCString(BinToHexString(&sBin, true));

	LPTSTR szFlags = NULL;
	InterpretFlags(flagGlobalObjectIdMonth, pgoidGlobalObjectId->Month, &szFlags);

	CString PropString;
	FileTimeToString(&pgoidGlobalObjectId->CreationTime, &PropString, NULL);
	szTmp.FormatMessage(IDS_GLOBALOBJECTIDDATA1,
		pgoidGlobalObjectId->Year,
		pgoidGlobalObjectId->Month, szFlags,
		pgoidGlobalObjectId->Day,
		pgoidGlobalObjectId->CreationTime.dwHighDateTime, pgoidGlobalObjectId->CreationTime.dwLowDateTime, PropString,
		pgoidGlobalObjectId->X.HighPart, pgoidGlobalObjectId->X.LowPart,
		pgoidGlobalObjectId->dwSize);
	delete[] szFlags;
	szFlags = NULL;
	szGlobalObjectId += szTmp;

	if (pgoidGlobalObjectId->dwSize && pgoidGlobalObjectId->lpData)
	{
		szTmp.FormatMessage(IDS_GLOBALOBJECTIDDATA2);
		szGlobalObjectId += szTmp;
		sBin.cb = pgoidGlobalObjectId->dwSize;
		sBin.lpb = pgoidGlobalObjectId->lpData;
		szGlobalObjectId += wstringToCString(BinToHexString(&sBin, true));
	}

	szGlobalObjectId += JunkDataToString(pgoidGlobalObjectId->JunkDataSize, pgoidGlobalObjectId->JunkData);

	return CStringToLPWSTR(szGlobalObjectId);
} // GlobalObjectIdStructToString

//////////////////////////////////////////////////////////////////////////
// End GlobalObjectIdStruct
//////////////////////////////////////////////////////////////////////////