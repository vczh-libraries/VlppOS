/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "Locale.h"
#include <Windows.h>

#ifndef VCZH_MSVC
static_assert(false, "Do not build this file for non-Windows applications.");
#endif

namespace vl
{
	using namespace collections;

/***********************************************************************
Locale Helper Functions
***********************************************************************/

	SYSTEMTIME DateTimeToSystemTime(const DateTime& dateTime)
	{
		SYSTEMTIME systemTime;
		systemTime.wYear = (WORD)dateTime.year;
		systemTime.wMonth = (WORD)dateTime.month;
		systemTime.wDayOfWeek = (WORD)dateTime.dayOfWeek;
		systemTime.wDay = (WORD)dateTime.day;
		systemTime.wHour = (WORD)dateTime.hour;
		systemTime.wMinute = (WORD)dateTime.minute;
		systemTime.wSecond = (WORD)dateTime.second;
		systemTime.wMilliseconds = (WORD)dateTime.milliseconds;
		return systemTime;
	}

	BOOL CALLBACK Locale_EnumLocalesProcEx(
		_In_  LPWSTR lpLocaleString,
		_In_  DWORD dwFlags,
		_In_  LPARAM lParam
	)
	{
		((List<Locale>*)lParam)->Add(Locale(lpLocaleString));
		return TRUE;
	}

	BOOL CALLBACK Locale_EnumDateFormatsProcExEx(
		_In_  LPWSTR lpDateFormatString,
		_In_  CALID CalendarID,
		_In_  LPARAM lParam
	)
	{
		((List<WString>*)lParam)->Add(lpDateFormatString);
		return TRUE;
	}

	BOOL CALLBACK EnumTimeFormatsProcEx(
		_In_  LPWSTR lpTimeFormatString,
		_In_  LPARAM lParam
	)
	{
		((List<WString>*)lParam)->Add(lpTimeFormatString);
		return TRUE;
	}

	WString Transform(const WString& localeName, const WString& input, DWORD flag)
	{
		int length = LCMapStringEx(localeName.Buffer(), flag, input.Buffer(), (int)input.Length() + 1, NULL, 0, NULL, NULL, NULL);
		Array<wchar_t> buffer(length);
		LCMapStringEx(localeName.Buffer(), flag, input.Buffer(), (int)input.Length() + 1, &buffer[0], (int)buffer.Count(), NULL, NULL, NULL);
		return &buffer[0];
	}

	DWORD TranslateNormalization(Locale::Normalization normalization)
	{
		DWORD result = 0;
		if (normalization & Locale::IgnoreCase) result |= NORM_IGNORECASE;
		if (normalization & Locale::IgnoreCaseLinguistic) result |= NORM_IGNORECASE | NORM_LINGUISTIC_CASING;
		if (normalization & Locale::IgnoreKanaType) result |= NORM_IGNOREKANATYPE;
		if (normalization & Locale::IgnoreNonSpace) result |= NORM_IGNORENONSPACE;
		if (normalization & Locale::IgnoreSymbol) result |= NORM_IGNORESYMBOLS;
		if (normalization & Locale::IgnoreWidth) result |= NORM_IGNOREWIDTH;
		if (normalization & Locale::DigitsAsNumbers) result |= SORT_DIGITSASNUMBERS;
		if (normalization & Locale::StringSoft) result |= SORT_STRINGSORT;
		return result;
	}

/***********************************************************************
WindowsLocaleImpl
***********************************************************************/

	class WindowsLocaleImpl : public Object, public ILocaleImpl
	{
	public:
		Locale Invariant() const override
		{
			return Locale(LOCALE_NAME_INVARIANT);
		}

		Locale SystemDefault() const override
		{
			wchar_t buffer[LOCALE_NAME_MAX_LENGTH + 1] = { 0 };
			GetSystemDefaultLocaleName(buffer, LOCALE_NAME_MAX_LENGTH);
			return Locale(buffer);
		}

		Locale UserDefault() const override
		{
			wchar_t buffer[LOCALE_NAME_MAX_LENGTH + 1] = { 0 };
			GetUserDefaultLocaleName(buffer, LOCALE_NAME_MAX_LENGTH);
			return Locale(buffer);
		}

		void Enumerate(collections::List<Locale>& locales) const override
		{
			EnumSystemLocalesEx(&Locale_EnumLocalesProcEx, LOCALE_ALL, (LPARAM)&locales, NULL);
		}

		void GetShortDateFormats(const WString& localeName, collections::List<WString>& formats) const override
		{
			EnumDateFormatsExEx(&Locale_EnumDateFormatsProcExEx, localeName.Buffer(), DATE_SHORTDATE, (LPARAM)&formats);
		}

		void GetLongDateFormats(const WString& localeName, collections::List<WString>& formats) const override
		{
			EnumDateFormatsExEx(&Locale_EnumDateFormatsProcExEx, localeName.Buffer(), DATE_LONGDATE, (LPARAM)&formats);
		}

		void GetYearMonthDateFormats(const WString& localeName, collections::List<WString>& formats) const override
		{
			EnumDateFormatsExEx(&Locale_EnumDateFormatsProcExEx, localeName.Buffer(), DATE_YEARMONTH, (LPARAM)&formats);
		}

		void GetLongTimeFormats(const WString& localeName, collections::List<WString>& formats) const override
		{
			EnumTimeFormatsEx(&EnumTimeFormatsProcEx, localeName.Buffer(), 0, (LPARAM)&formats);
		}

		void GetShortTimeFormats(const WString& localeName, collections::List<WString>& formats) const override
		{
			EnumTimeFormatsEx(&EnumTimeFormatsProcEx, localeName.Buffer(), TIME_NOSECONDS, (LPARAM)&formats);
		}

		WString FormatDate(const WString& localeName, const WString& format, DateTime date) const override
		{
			SYSTEMTIME st = DateTimeToSystemTime(date);
			int length = GetDateFormatEx(localeName.Buffer(), 0, &st, format.Buffer(), NULL, 0, NULL);
			if (length == 0) return L"";
			Array<wchar_t> buffer(length);
			GetDateFormatEx(localeName.Buffer(), 0, &st, format.Buffer(), &buffer[0], (int)buffer.Count(), NULL);
			return &buffer[0];
		}

		WString FormatTime(const WString& localeName, const WString& format, DateTime time) const override
		{
			SYSTEMTIME st = DateTimeToSystemTime(time);
			int length = GetTimeFormatEx(localeName.Buffer(), 0, &st, format.Buffer(), NULL, 0);
			if (length == 0) return L"";
			Array<wchar_t> buffer(length);
			GetTimeFormatEx(localeName.Buffer(), 0, &st, format.Buffer(), &buffer[0], (int)buffer.Count());
			return &buffer[0];
		}

		WString FormatNumber(const WString& localeName, const WString& number) const override
		{
			int length = GetNumberFormatEx(localeName.Buffer(), 0, number.Buffer(), NULL, NULL, 0);
			if (length == 0) return L"";
			Array<wchar_t> buffer(length);
			GetNumberFormatEx(localeName.Buffer(), 0, number.Buffer(), NULL, &buffer[0], (int)buffer.Count());
			return &buffer[0];
		}

		WString FormatCurrency(const WString& localeName, const WString& currency) const override
		{
			int length = GetCurrencyFormatEx(localeName.Buffer(), 0, currency.Buffer(), NULL, NULL, 0);
			if (length == 0) return L"";
			Array<wchar_t> buffer(length);
			GetCurrencyFormatEx(localeName.Buffer(), 0, currency.Buffer(), NULL, &buffer[0], (int)buffer.Count());
			return &buffer[0];
		}

		WString GetShortDayOfWeekName(const WString& localeName, vint dayOfWeek) const override
		{
			return FormatDate(localeName, L"ddd", DateTime::FromDateTime(2000, 1, 2 + dayOfWeek));
		}

		WString GetLongDayOfWeekName(const WString& localeName, vint dayOfWeek) const override
		{
			return FormatDate(localeName, L"dddd", DateTime::FromDateTime(2000, 1, 2 + dayOfWeek));
		}

		WString GetShortMonthName(const WString& localeName, vint month) const override
		{
			return FormatDate(localeName, L"MMM", DateTime::FromDateTime(2000, month, 1));
		}

		WString GetLongMonthName(const WString& localeName, vint month) const override
		{
			return FormatDate(localeName, L"MMMM", DateTime::FromDateTime(2000, month, 1));
		}

		WString ToLower(const WString& localeName, const WString& str) const override
		{
			return Transform(localeName, str, LCMAP_LOWERCASE);
		}

		WString ToUpper(const WString& localeName, const WString& str) const override
		{
			return Transform(localeName, str, LCMAP_UPPERCASE);
		}

		WString ToLinguisticLower(const WString& localeName, const WString& str) const override
		{
			return Transform(localeName, str, LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING);
		}

		WString ToLinguisticUpper(const WString& localeName, const WString& str) const override
		{
			return Transform(localeName, str, LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING);
		}

		vint Compare(const WString& localeName, const WString& s1, const WString& s2, Locale::Normalization normalization) const override
		{
			switch (CompareStringEx(localeName.Buffer(), TranslateNormalization(normalization), s1.Buffer(), (int)s1.Length(), s2.Buffer(), (int)s2.Length(), NULL, NULL, NULL))
			{
			case CSTR_LESS_THAN: return -1;
			case CSTR_GREATER_THAN: return 1;
			default: return 0;
			}
		}

		vint CompareOrdinal(const WString& s1, const WString& s2) const override
		{
			switch (CompareStringOrdinal(s1.Buffer(), (int)s1.Length(), s2.Buffer(), (int)s2.Length(), FALSE))
			{
			case CSTR_LESS_THAN: return -1;
			case CSTR_GREATER_THAN: return 1;
			default: return 0;
			}
		}

		vint CompareOrdinalIgnoreCase(const WString& s1, const WString& s2) const override
		{
			switch (CompareStringOrdinal(s1.Buffer(), (int)s1.Length(), s2.Buffer(), (int)s2.Length(), TRUE))
			{
			case CSTR_LESS_THAN: return -1;
			case CSTR_GREATER_THAN: return 1;
			default: return 0;
			}
		}

		collections::Pair<vint, vint> FindFirst(const WString& localeName, const WString& text, const WString& find, Locale::Normalization normalization) const override
		{
			int length = 0;
			int result = FindNLSStringEx(localeName.Buffer(), FIND_FROMSTART | TranslateNormalization(normalization), text.Buffer(), (int)text.Length(), find.Buffer(), (int)find.Length(), &length, NULL, NULL, NULL);
			return result == -1 ? collections::Pair<vint, vint>(-1, 0) : collections::Pair<vint, vint>(result, length);
		}

		collections::Pair<vint, vint> FindLast(const WString& localeName, const WString& text, const WString& find, Locale::Normalization normalization) const override
		{
			int length = 0;
			int result = FindNLSStringEx(localeName.Buffer(), FIND_FROMEND | TranslateNormalization(normalization), text.Buffer(), (int)text.Length(), find.Buffer(), (int)find.Length(), &length, NULL, NULL, NULL);
			return result == -1 ? collections::Pair<vint, vint>(-1, 0) : collections::Pair<vint, vint>(result, length);
		}

		bool StartsWith(const WString& localeName, const WString& text, const WString& find, Locale::Normalization normalization) const override
		{
			int result = FindNLSStringEx(localeName.Buffer(), FIND_STARTSWITH | TranslateNormalization(normalization), text.Buffer(), (int)text.Length(), find.Buffer(), (int)find.Length(), NULL, NULL, NULL, NULL);
			return result != -1;
		}

		bool EndsWith(const WString& localeName, const WString& text, const WString& find, Locale::Normalization normalization) const override
		{
			int result = FindNLSStringEx(localeName.Buffer(), FIND_ENDSWITH | TranslateNormalization(normalization), text.Buffer(), (int)text.Length(), find.Buffer(), (int)find.Length(), NULL, NULL, NULL, NULL);
			return result != -1;
		}
	};

	WindowsLocaleImpl windowsLocaleImpl;

	ILocaleImpl* GetOSLocaleImpl()
	{
		return &windowsLocaleImpl;
	}

/***********************************************************************
Locale (Windows Specific)
***********************************************************************/

	WString Locale::ToFullWidth(const WString& str)const
	{
		return Transform(localeName, str, LCMAP_FULLWIDTH);
	}

	WString Locale::ToHalfWidth(const WString& str)const
	{
		return Transform(localeName, str, LCMAP_HALFWIDTH);
	}

	WString Locale::ToHiragana(const WString& str)const
	{
		return Transform(localeName, str, LCMAP_HIRAGANA);
	}

	WString Locale::ToKatagana(const WString& str)const
	{
		return Transform(localeName, str, LCMAP_KATAKANA);
	}

	WString Locale::ToSimplifiedChinese(const WString& str)const
	{
		return Transform(localeName, str, LCMAP_SIMPLIFIED_CHINESE);
	}

	WString Locale::ToTraditionalChinese(const WString& str)const
	{
		return Transform(localeName, str, LCMAP_TRADITIONAL_CHINESE);
	}

	WString Locale::ToTileCase(const WString& str)const
	{
		return Transform(localeName, str, LCMAP_TITLECASE);
	}
}
