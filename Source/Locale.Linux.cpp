/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "Locale.h"
#include <stdio.h>
#include <ctype.h>
#include <wctype.h>
#include <wchar.h>

#ifndef VCZH_GCC
static_assert(false, "Do not build this file for Windows applications.");
#endif

namespace vl
{
	using namespace collections;

/***********************************************************************
Locale
***********************************************************************/

	Locale Locale::Invariant()
	{
		return Locale(L"");
	}

	Locale Locale::SystemDefault()
	{
		return Locale(L"en-US");
	}

	Locale Locale::UserDefault()
	{
		return Locale(L"en-US");
	}

	void Locale::Enumerate(collections::List<Locale>& locales)
	{
		locales.Add(Locale(L"en-US"));
	}

	void Locale::GetShortDateFormats(collections::List<WString>& formats)const
	{
		formats.Add(L"MM/dd/yyyy");
		formats.Add(L"yyyy-MM-dd");
	}

	void Locale::GetLongDateFormats(collections::List<WString>& formats)const
	{
		formats.Add(L"dddd, dd MMMM yyyy");
	}

	void Locale::GetYearMonthDateFormats(collections::List<WString>& formats)const
	{
		formats.Add(L"yyyy MMMM");
	}

	void Locale::GetLongTimeFormats(collections::List<WString>& formats)const
	{
		formats.Add(L"HH:mm:ss");
	}

	void Locale::GetShortTimeFormats(collections::List<WString>& formats)const
	{
		formats.Add(L"HH:mm");
		formats.Add(L"hh:mm tt");
	}

	WString Locale::FormatDate(const WString& format, DateTime date)const
	{
		/*
		auto df = L"yyyy,MM,MMM,MMMM,dd,ddd,dddd";
		auto ds = L"2000,01,Jan,January,02,Sun,Sunday";
		auto tf = L"hh,HH,mm,ss,tt";
		auto ts = L"01,13,02,03,PM";
		*/
		WString result;
		const wchar_t* reading = format.Buffer();

		while (*reading)
		{
			if (wcsncmp(reading, L"yyyy", 4) == 0)
			{
				WString fragment = itow(date.year);
				while (fragment.Length() < 4) fragment = L"0" + fragment;
				result += fragment;
				reading += 4;
			}
			else if (wcsncmp(reading, L"MMMM", 4) == 0)
			{
				result += GetLongMonthName(date.month);
				reading += 4;
			}
			else if (wcsncmp(reading, L"MMM", 3) == 0)
			{
				result += GetShortMonthName(date.month);
				reading += 3;
			}
			else if (wcsncmp(reading, L"MM", 2) == 0)
			{
				WString fragment = itow(date.month);
				while (fragment.Length() < 2) fragment = L"0" + fragment;
				result += fragment;
				reading += 2;
			}
			else if (wcsncmp(reading, L"dddd", 4) == 0)
			{
				result += GetLongDayOfWeekName(date.dayOfWeek);
				reading += 4;
			}
			else if (wcsncmp(reading, L"ddd", 3) == 0)
			{
				result += GetShortDayOfWeekName(date.dayOfWeek);
				reading += 3;
			}
			else if (wcsncmp(reading, L"dd", 2) == 0)
			{
				WString fragment = itow(date.day);
				while (fragment.Length() < 2) fragment = L"0" + fragment;
				result += fragment;
				reading += 2;
			}
			else if (wcsncmp(reading, L"hh", 2) == 0)
			{
				WString fragment = itow(date.hour > 12 ? date.hour - 12 : date.hour);
				while (fragment.Length() < 2) fragment = L"0" + fragment;
				result += fragment;
				reading += 2;
			}
			else if (wcsncmp(reading, L"HH", 2) == 0)
			{
				WString fragment = itow(date.hour);
				while (fragment.Length() < 2) fragment = L"0" + fragment;
				result += fragment;
				reading += 2;
			}
			else if (wcsncmp(reading, L"mm", 2) == 0)
			{
				WString fragment = itow(date.minute);
				while (fragment.Length() < 2) fragment = L"0" + fragment;
				result += fragment;
				reading += 2;
			}
			else if (wcsncmp(reading, L"ss", 2) == 0)
			{
				WString fragment = itow(date.second);
				while (fragment.Length() < 2) fragment = L"0" + fragment;
				result += fragment;
				reading += 2;
			}
			else if (wcsncmp(reading, L"tt", 2) == 0)
			{
				result += date.hour > 12 ? L"PM" : L"AM";
				reading += 2;
			}
			else
			{
				result += WString::FromChar(*reading);
				reading++;
			}
		}
		return result;
	}

	WString Locale::FormatTime(const WString& format, DateTime time)const
	{
		return FormatDate(format, time);
	}

	WString Locale::FormatNumber(const WString& number)const
	{
		return number;
	}

	WString Locale::FormatCurrency(const WString& currency)const
	{
		return currency;
	}

	WString Locale::GetShortDayOfWeekName(vint dayOfWeek)const
	{
		switch (dayOfWeek)
		{
		case 0: return L"Sun";
		case 1: return L"Mon";
		case 2:	return L"Tue";
		case 3:	return L"Wed";
		case 4:	return L"Thu";
		case 5:	return L"Fri";
		case 6:	return L"Sat";
		}
		return L"";
	}

	WString Locale::GetLongDayOfWeekName(vint dayOfWeek)const
	{
		switch (dayOfWeek)
		{
		case 0: return L"Sunday";
		case 1: return L"Monday";
		case 2:	return L"Tuesday";
		case 3:	return L"Wednesday";
		case 4:	return L"Thursday";
		case 5:	return L"Friday";
		case 6:	return L"Saturday";
		}
		return L"";
	}

	WString Locale::GetShortMonthName(vint month)const
	{
		switch (month)
		{
		case 1: return L"Jan";
		case 2: return L"Feb";
		case 3: return L"Mar";
		case 4: return L"Apr";
		case 5: return L"May";
		case 6: return L"Jun";
		case 7: return L"Jul";
		case 8: return L"Aug";
		case 9: return L"Sep";
		case 10: return L"Oct";
		case 11: return L"Nov";
		case 12: return L"Dec";
		}
		return L"";
	}

	WString Locale::GetLongMonthName(vint month)const
	{
		switch (month)
		{
		case 1: return L"January";
		case 2: return L"February";
		case 3: return L"March";
		case 4: return L"April";
		case 5: return L"May";
		case 6: return L"June";
		case 7: return L"July";
		case 8: return L"August";
		case 9: return L"September";
		case 10: return L"October";
		case 11: return L"November";
		case 12: return L"December";
		}
		return L"";
	}

	WString Locale::ToLower(const WString& str)const
	{
		return wlower(str);
	}

	WString Locale::ToUpper(const WString& str)const
	{
		return wupper(str);
	}

	WString Locale::ToLinguisticLower(const WString& str)const
	{
		return wlower(str);
	}

	WString Locale::ToLinguisticUpper(const WString& str)const
	{
		return wupper(str);
	}

	vint Locale::Compare(const WString& s1, const WString& s2, Normalization normalization)const
	{
		switch (normalization)
		{
		case Normalization::None:
			return wcscmp(s1.Buffer(), s2.Buffer());
		case Normalization::IgnoreCase:
			return wcscasecmp(s1.Buffer(), s2.Buffer());
		default:
			return 0;
		}
	}

	vint Locale::CompareOrdinal(const WString& s1, const WString& s2)const
	{
		return wcscmp(s1.Buffer(), s2.Buffer());
	}

	vint Locale::CompareOrdinalIgnoreCase(const WString& s1, const WString& s2)const
	{
		return wcscasecmp(s1.Buffer(), s2.Buffer());
	}

	collections::Pair<vint, vint> Locale::FindFirst(const WString& text, const WString& find, Normalization normalization)const
	{
		if (text.Length() < find.Length() || find.Length() == 0)
		{
			return Pair<vint, vint>(-1, 0);
		}
		const wchar_t* result = 0;
		switch (normalization)
		{
		case Normalization::None:
			{
				const wchar_t* reading = text.Buffer();
				while (*reading)
				{
					if (wcsncmp(reading, find.Buffer(), find.Length()) == 0)
					{
						result = reading;
						break;
					}
					reading++;
				}
			}
			break;
		case Normalization::IgnoreCase:
			{
				const wchar_t* reading = text.Buffer();
				while (*reading)
				{
					if (wcsncasecmp(reading, find.Buffer(), find.Length()) == 0)
					{
						result = reading;
						break;
					}
					reading++;
				}
			}
			break;
		}
		return result == nullptr ? Pair<vint, vint>(-1, 0) : Pair<vint, vint>(result - text.Buffer(), find.Length());
	}

	collections::Pair<vint, vint> Locale::FindLast(const WString& text, const WString& find, Normalization normalization)const
	{
		if (text.Length() < find.Length() || find.Length() == 0)
		{
			return Pair<vint, vint>(-1, 0);
		}
		const wchar_t* result = 0;
		switch (normalization)
		{
		case Normalization::None:
			{
				const wchar_t* reading = text.Buffer();
				while (*reading)
				{
					if (wcsncmp(reading, find.Buffer(), find.Length()) == 0)
					{
						result = reading;
					}
					reading++;
				}
			}
			break;
		case Normalization::IgnoreCase:
			{
				const wchar_t* reading = text.Buffer();
				while (*reading)
				{
					if (wcsncasecmp(reading, find.Buffer(), find.Length()) == 0)
					{
						result = reading;
					}
					reading++;
				}
			}
			break;
		}
		return result == nullptr ? Pair<vint, vint>(-1, 0) : Pair<vint, vint>(result - text.Buffer(), find.Length());
	}

	bool Locale::StartsWith(const WString& text, const WString& find, Normalization normalization)const
	{
		if (text.Length() < find.Length() || find.Length() == 0)
		{
			return false;
		}
		switch (normalization)
		{
		case Normalization::None:
			return wcsncmp(text.Buffer(), find.Buffer(), find.Length()) == 0;
		case Normalization::IgnoreCase:
			return wcsncasecmp(text.Buffer(), find.Buffer(), find.Length()) == 0;
		}
		return false;
	}

	bool Locale::EndsWith(const WString& text, const WString& find, Normalization normalization)const
	{
		if (text.Length() < find.Length() || find.Length() == 0)
		{
			return false;
		}
		switch (normalization)
		{
		case Normalization::None:
			return wcsncmp(text.Buffer() + text.Length() - find.Length(), find.Buffer(), find.Length()) == 0;
		case Normalization::IgnoreCase:
			return wcsncasecmp(text.Buffer() + text.Length() - find.Length(), find.Buffer(), find.Length()) == 0;
		}
		return false;
	}
}
