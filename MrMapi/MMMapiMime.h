#pragma once
// MAPI <-> MIME conversion for MrMAPI

struct MYOPTIONS;

// Flags to control conversion
enum MAPIMIMEFLAGS
{
	MAPIMIME_TOMAPI = 0x00000001,
	MAPIMIME_TOMIME = 0x00000002,
	MAPIMIME_RFC822 = 0x00000004,
	MAPIMIME_WRAP = 0x00000008,
	MAPIMIME_ENCODING = 0x00000010,
	MAPIMIME_ADDRESSBOOK = 0x00000020,
	MAPIMIME_UNICODE = 0x00000040,
	MAPIMIME_CHARSET = 0x00000080,
};
inline MAPIMIMEFLAGS& operator|=(MAPIMIMEFLAGS& a, MAPIMIMEFLAGS b) { return reinterpret_cast<MAPIMIMEFLAGS&>(reinterpret_cast<int&>(a) |= static_cast<int>(b)); }

void DoMAPIMIME(_In_ MYOPTIONS ProgOpts);