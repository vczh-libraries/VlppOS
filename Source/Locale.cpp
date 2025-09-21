/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "Locale.h"

namespace vl
{
	using namespace collections;

/***********************************************************************
EnUsLocaleImpl
***********************************************************************/

#ifdef VCZH_GCC
#define _wcsicmp wcscasecmp
#define _wcsnicmp wcsncasecmp
#endif

	Locale EnUsLocaleImpl::Invariant() const
	{
		return Locale(L"");
	}

	Locale EnUsLocaleImpl::SystemDefault() const
	{
		return Locale(L"en-US");
	}

	Locale EnUsLocaleImpl::UserDefault() const
	{
		return Locale(L"en-US");
	}

	void EnUsLocaleImpl::Enumerate(List<Locale>& locales) const
	{
		locales.Add(Locale(L"en-US"));
	}

	void EnUsLocaleImpl::GetShortDateFormats(const WString&, List<WString>& formats) const
	{
		formats.Add(L"MM/dd/yyyy");
		formats.Add(L"yyyy-MM-dd");
	}

	void EnUsLocaleImpl::GetLongDateFormats(const WString&, List<WString>& formats) const
	{
		formats.Add(L"dddd, dd MMMM yyyy");
	}

	void EnUsLocaleImpl::GetYearMonthDateFormats(const WString&, List<WString>& formats) const
	{
		formats.Add(L"yyyy MMMM");
	}

	void EnUsLocaleImpl::GetLongTimeFormats(const WString&, List<WString>& formats) const
	{
		formats.Add(L"HH:mm:ss");
	}

	void EnUsLocaleImpl::GetShortTimeFormats(const WString&, List<WString>& formats) const
	{
		formats.Add(L"HH:mm");
		formats.Add(L"hh:mm tt");
	}

	WString EnUsLocaleImpl::FormatDate(const WString& localeName, const WString& format, DateTime date) const
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
				result += GetLongMonthName(localeName, date.month);
				reading += 4;
			}
			else if (wcsncmp(reading, L"MMM", 3) == 0)
			{
				result += GetShortMonthName(localeName, date.month);
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
				result += GetLongDayOfWeekName(localeName, date.dayOfWeek);
				reading += 4;
			}
			else if (wcsncmp(reading, L"ddd", 3) == 0)
			{
				result += GetShortDayOfWeekName(localeName, date.dayOfWeek);
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

	WString EnUsLocaleImpl::FormatTime(const WString& localeName, const WString& format, DateTime time) const
	{
		return FormatDate(localeName, format, time);
	}

	WString EnUsLocaleImpl::FormatNumber(const WString&, const WString& number) const
	{
		return number;
	}

	WString EnUsLocaleImpl::FormatCurrency(const WString&, const WString& currency) const
	{
		return currency;
	}

	WString EnUsLocaleImpl::GetShortDayOfWeekName(const WString&, vint dayOfWeek) const
	{
		switch (dayOfWeek)
		{
		case 0: return L"Sun";
		case 1: return L"Mon";
		case 2: return L"Tue";
		case 3: return L"Wed";
		case 4: return L"Thu";
		case 5: return L"Fri";
		case 6: return L"Sat";
		}
		return L"";
	}

	WString EnUsLocaleImpl::GetLongDayOfWeekName(const WString&, vint dayOfWeek) const
	{
		switch (dayOfWeek)
		{
		case 0: return L"Sunday";
		case 1: return L"Monday";
		case 2: return L"Tuesday";
		case 3: return L"Wednesday";
		case 4: return L"Thursday";
		case 5: return L"Friday";
		case 6: return L"Saturday";
		}
		return L"";
	}

	WString EnUsLocaleImpl::GetShortMonthName(const WString&, vint month) const
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

	WString EnUsLocaleImpl::GetLongMonthName(const WString&, vint month) const
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

	WString EnUsLocaleImpl::ToLower(const WString&, const WString& str) const
	{
		return wlower(str);
	}

	WString EnUsLocaleImpl::ToUpper(const WString&, const WString& str) const
	{
		return wupper(str);
	}

	WString EnUsLocaleImpl::ToLinguisticLower(const WString&, const WString& str) const
	{
		return wlower(str);
	}

	WString EnUsLocaleImpl::ToLinguisticUpper(const WString&, const WString& str) const
	{
		return wupper(str);
	}

	vint EnUsLocaleImpl::Compare(const WString&, const WString& s1, const WString& s2, Locale::Normalization normalization) const
	{
		switch (normalization)
		{
		case Locale::Normalization::None:
			return wcscmp(s1.Buffer(), s2.Buffer());
		case Locale::Normalization::IgnoreCase:
			return _wcsicmp(s1.Buffer(), s2.Buffer());
		default:
			return 0;
		}
	}

	vint EnUsLocaleImpl::CompareOrdinal(const WString& s1, const WString& s2) const
	{
		return wcscmp(s1.Buffer(), s2.Buffer());
	}

	vint EnUsLocaleImpl::CompareOrdinalIgnoreCase(const WString& s1, const WString& s2) const
	{
		return _wcsicmp(s1.Buffer(), s2.Buffer());
	}

	Pair<vint, vint> EnUsLocaleImpl::FindFirst(const WString&, const WString& text, const WString& find, Locale::Normalization normalization) const
	{
		if (text.Length() < find.Length() || find.Length() == 0)
		{
			return Pair<vint, vint>(-1, 0);
		}
		const wchar_t* result = nullptr;
		switch (normalization)
		{
		case Locale::Normalization::None:
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
		case Locale::Normalization::IgnoreCase:
			{
				const wchar_t* reading = text.Buffer();
				while (*reading)
				{
					if (_wcsnicmp(reading, find.Buffer(), find.Length()) == 0)
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

	Pair<vint, vint> EnUsLocaleImpl::FindLast(const WString&, const WString& text, const WString& find, Locale::Normalization normalization) const
	{
		if (text.Length() < find.Length() || find.Length() == 0)
		{
			return Pair<vint, vint>(-1, 0);
		}
		const wchar_t* result = nullptr;
		switch (normalization)
		{
		case Locale::Normalization::None:
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
		case Locale::Normalization::IgnoreCase:
			{
				const wchar_t* reading = text.Buffer();
				while (*reading)
				{
					if (_wcsnicmp(reading, find.Buffer(), find.Length()) == 0)
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

	bool EnUsLocaleImpl::StartsWith(const WString&, const WString& text, const WString& find, Locale::Normalization normalization) const
	{
		if (text.Length() < find.Length() || find.Length() == 0)
		{
			return false;
		}
		switch (normalization)
		{
		case Locale::Normalization::None:
			return wcsncmp(text.Buffer(), find.Buffer(), find.Length()) == 0;
		case Locale::Normalization::IgnoreCase:
			return _wcsnicmp(text.Buffer(), find.Buffer(), find.Length()) == 0;
		}
		return false;
	}

	bool EnUsLocaleImpl::EndsWith(const WString&, const WString& text, const WString& find, Locale::Normalization normalization) const
	{
		if (text.Length() < find.Length() || find.Length() == 0)
		{
			return false;
		}
		switch (normalization)
		{
		case Locale::Normalization::None:
			return wcsncmp(text.Buffer() + text.Length() - find.Length(), find.Buffer(), find.Length()) == 0;
		case Locale::Normalization::IgnoreCase:
			return _wcsnicmp(text.Buffer() + text.Length() - find.Length(), find.Buffer(), find.Length()) == 0;
		}
		return false;
	}

#ifdef VCZH_GCC
#undef _wcsicmp
#undef _wcsnicmp
#endif

/***********************************************************************
InjectLocaleImpl
***********************************************************************/

	extern ILocaleImpl* GetOSLocaleImpl();

	feature_injection::FeatureInjection<ILocaleImpl>& GetLocaleInjection()
	{
		static feature_injection::FeatureInjection<ILocaleImpl> injection(GetOSLocaleImpl());
		return injection;
	}

	void InjectLocaleImpl(ILocaleImpl* impl)
	{
		GetLocaleInjection().Inject(impl);
	}

	void EjectLocaleImpl(ILocaleImpl* impl)
	{
		if (impl == nullptr)
		{
			GetLocaleInjection().EjectAll();
		}
		else
		{
			GetLocaleInjection().Eject(impl);
		}
	}

/***********************************************************************
Locale
***********************************************************************/

	Locale::Locale(const WString& _localeName)
		:localeName(_localeName)
	{
	}

	const WString& Locale::GetName()const
	{
		return localeName;
	}

/***********************************************************************
Locale (static)
***********************************************************************/

	Locale Locale::Invariant()
	{
		return GetLocaleInjection().Get()->Invariant();
	}

	Locale Locale::SystemDefault()
	{
		return GetLocaleInjection().Get()->SystemDefault();
	}

	Locale Locale::UserDefault()
	{
		return GetLocaleInjection().Get()->UserDefault();
	}

	void Locale::Enumerate(collections::List<Locale>& locales)
	{
		GetLocaleInjection().Get()->Enumerate(locales);
	}

/***********************************************************************
Locale (ILocaleImpl redirections)
***********************************************************************/

	void Locale::GetShortDateFormats(collections::List<WString>& formats)const
	{
		GetLocaleInjection().Get()->GetShortDateFormats(localeName, formats);
	}

	void Locale::GetLongDateFormats(collections::List<WString>& formats)const
	{
		GetLocaleInjection().Get()->GetLongDateFormats(localeName, formats);
	}

	void Locale::GetYearMonthDateFormats(collections::List<WString>& formats)const
	{
		GetLocaleInjection().Get()->GetYearMonthDateFormats(localeName, formats);
	}

	void Locale::GetLongTimeFormats(collections::List<WString>& formats)const
	{
		GetLocaleInjection().Get()->GetLongTimeFormats(localeName, formats);
	}

	void Locale::GetShortTimeFormats(collections::List<WString>& formats)const
	{
		GetLocaleInjection().Get()->GetShortTimeFormats(localeName, formats);
	}

	WString Locale::FormatDate(const WString& format, DateTime date)const
	{
		return GetLocaleInjection().Get()->FormatDate(localeName, format, date);
	}

	WString Locale::FormatTime(const WString& format, DateTime time)const
	{
		return GetLocaleInjection().Get()->FormatTime(localeName, format, time);
	}

	WString Locale::FormatNumber(const WString& number)const
	{
		return GetLocaleInjection().Get()->FormatNumber(localeName, number);
	}

	WString Locale::FormatCurrency(const WString& currency)const
	{
		return GetLocaleInjection().Get()->FormatCurrency(localeName, currency);
	}

	WString Locale::GetShortDayOfWeekName(vint dayOfWeek)const
	{
		return GetLocaleInjection().Get()->GetShortDayOfWeekName(localeName, dayOfWeek);
	}

	WString Locale::GetLongDayOfWeekName(vint dayOfWeek)const
	{
		return GetLocaleInjection().Get()->GetLongDayOfWeekName(localeName, dayOfWeek);
	}

	WString Locale::GetShortMonthName(vint month)const
	{
		return GetLocaleInjection().Get()->GetShortMonthName(localeName, month);
	}

	WString Locale::GetLongMonthName(vint month)const
	{
		return GetLocaleInjection().Get()->GetLongMonthName(localeName, month);
	}

	WString Locale::ToLower(const WString& str)const
	{
		return GetLocaleInjection().Get()->ToLower(localeName, str);
	}

	WString Locale::ToUpper(const WString& str)const
	{
		return GetLocaleInjection().Get()->ToUpper(localeName, str);
	}

	WString Locale::ToLinguisticLower(const WString& str)const
	{
		return GetLocaleInjection().Get()->ToLinguisticLower(localeName, str);
	}

	WString Locale::ToLinguisticUpper(const WString& str)const
	{
		return GetLocaleInjection().Get()->ToLinguisticUpper(localeName, str);
	}

	vint Locale::Compare(const WString& s1, const WString& s2, Normalization normalization)const
	{
		return GetLocaleInjection().Get()->Compare(localeName, s1, s2, normalization);
	}

	vint Locale::CompareOrdinal(const WString& s1, const WString& s2)const
	{
		return GetLocaleInjection().Get()->CompareOrdinal(s1, s2);
	}

	vint Locale::CompareOrdinalIgnoreCase(const WString& s1, const WString& s2)const
	{
		return GetLocaleInjection().Get()->CompareOrdinalIgnoreCase(s1, s2);
	}

	collections::Pair<vint, vint> Locale::FindFirst(const WString& text, const WString& find, Normalization normalization)const
	{
		return GetLocaleInjection().Get()->FindFirst(localeName, text, find, normalization);
	}

	collections::Pair<vint, vint> Locale::FindLast(const WString& text, const WString& find, Normalization normalization)const
	{
		return GetLocaleInjection().Get()->FindLast(localeName, text, find, normalization);
	}

	bool Locale::StartsWith(const WString& text, const WString& find, Normalization normalization)const
	{
		return GetLocaleInjection().Get()->StartsWith(localeName, text, find, normalization);
	}

	bool Locale::EndsWith(const WString& text, const WString& find, Normalization normalization)const
	{
		return GetLocaleInjection().Get()->EndsWith(localeName, text, find, normalization);
	}
}
