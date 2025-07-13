/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "Locale.h"

namespace vl
{
	using namespace collections;

	extern ILocaleImpl* GetOSLocaleImpl();

	ILocaleImpl* localeImpl = nullptr;

	ILocaleImpl* GetLocaleImpl()
	{
		return localeImpl ? localeImpl : GetOSLocaleImpl();
	}

	void InjectLocaleImpl(ILocaleImpl* impl)
	{
		localeImpl = impl;
	}

/***********************************************************************
DefaultLocaleImpl
***********************************************************************/

#ifdef VCZH_GCC
#define _wcsicmp wcscasecmp
#define _wcsnicmp wcsncasecmp
#endif

	class DefaultLocaleImpl : public Object, public ILocaleImpl
	{
	public:
		Locale Invariant() const override
		{
			return Locale(L"");
		}

		Locale SystemDefault() const override
		{
			return Locale(L"en-US");
		}

		Locale UserDefault() const override
		{
			return Locale(L"en-US");
		}

		void Enumerate(List<Locale>& locales) const override
		{
			locales.Add(Locale(L"en-US"));
		}

		void GetShortDateFormats(const WString&, List<WString>& formats) const override
		{
			formats.Add(L"MM/dd/yyyy");
			formats.Add(L"yyyy-MM-dd");
		}

		void GetLongDateFormats(const WString&, List<WString>& formats) const override
		{
			formats.Add(L"dddd, dd MMMM yyyy");
		}

		void GetYearMonthDateFormats(const WString&, List<WString>& formats) const override
		{
			formats.Add(L"yyyy MMMM");
		}

		void GetLongTimeFormats(const WString&, List<WString>& formats) const override
		{
			formats.Add(L"HH:mm:ss");
		}

		void GetShortTimeFormats(const WString&, List<WString>& formats) const override
		{
			formats.Add(L"HH:mm");
			formats.Add(L"hh:mm tt");
		}

		WString FormatDate(const WString& localeName, const WString& format, DateTime date) const override
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

		WString FormatTime(const WString& localeName, const WString& format, DateTime time) const override
		{
			return FormatDate(localeName, format, time);
		}

		WString FormatNumber(const WString&, const WString& number) const override
		{
			return number;
		}

		WString FormatCurrency(const WString&, const WString& currency) const override
		{
			return currency;
		}

		WString GetShortDayOfWeekName(const WString&, vint dayOfWeek) const override
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

		WString GetLongDayOfWeekName(const WString&, vint dayOfWeek) const override
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

		WString GetShortMonthName(const WString&, vint month) const override
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

		WString GetLongMonthName(const WString&, vint month) const override
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

		WString ToLower(const WString&, const WString& str) const override
		{
			return wlower(str);
		}

		WString ToUpper(const WString&, const WString& str) const override
		{
			return wupper(str);
		}

		WString ToLinguisticLower(const WString&, const WString& str) const override
		{
			return wlower(str);
		}

		WString ToLinguisticUpper(const WString&, const WString& str) const override
		{
			return wupper(str);
		}

		vint Compare(const WString&, const WString& s1, const WString& s2, Locale::Normalization normalization) const override
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

		vint CompareOrdinal(const WString& s1, const WString& s2) const override
		{
			return wcscmp(s1.Buffer(), s2.Buffer());
		}

		vint CompareOrdinalIgnoreCase(const WString& s1, const WString& s2) const override
		{
			return _wcsicmp(s1.Buffer(), s2.Buffer());
		}

		Pair<vint, vint> FindFirst(const WString&, const WString& text, const WString& find, Locale::Normalization normalization) const override
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

		Pair<vint, vint> FindLast(const WString&, const WString& text, const WString& find, Locale::Normalization normalization) const override
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

		bool StartsWith(const WString&, const WString& text, const WString& find, Locale::Normalization normalization) const override
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

		bool EndsWith(const WString&, const WString& text, const WString& find, Locale::Normalization normalization) const override
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
	};

#ifdef VCZH_GCC
#undef _wcsicmp
#undef _wcsnicmp
#endif

	DefaultLocaleImpl defaultLocaleImpl;

	ILocaleImpl* GetDefaultLocaleImpl()
	{
		return &defaultLocaleImpl;
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
		return GetDefaultLocaleImpl()->Invariant();
	}

	Locale Locale::SystemDefault()
	{
		return GetDefaultLocaleImpl()->SystemDefault();
	}

	Locale Locale::UserDefault()
	{
		return GetDefaultLocaleImpl()->UserDefault();
	}

	void Locale::Enumerate(collections::List<Locale>& locales)
	{
		GetDefaultLocaleImpl()->Enumerate(locales);
	}

/***********************************************************************
Locale (ILocaleImpl redirections)
***********************************************************************/

	void Locale::GetShortDateFormats(collections::List<WString>& formats)const
	{
		GetDefaultLocaleImpl()->GetShortDateFormats(localeName, formats);
	}

	void Locale::GetLongDateFormats(collections::List<WString>& formats)const
	{
		GetDefaultLocaleImpl()->GetLongDateFormats(localeName, formats);
	}

	void Locale::GetYearMonthDateFormats(collections::List<WString>& formats)const
	{
		GetDefaultLocaleImpl()->GetYearMonthDateFormats(localeName, formats);
	}

	void Locale::GetLongTimeFormats(collections::List<WString>& formats)const
	{
		GetDefaultLocaleImpl()->GetLongTimeFormats(localeName, formats);
	}

	void Locale::GetShortTimeFormats(collections::List<WString>& formats)const
	{
		GetDefaultLocaleImpl()->GetShortTimeFormats(localeName, formats);
	}

	WString Locale::FormatDate(const WString& format, DateTime date)const
	{
		return GetDefaultLocaleImpl()->FormatDate(localeName, format, date);
	}

	WString Locale::FormatTime(const WString& format, DateTime time)const
	{
		return GetDefaultLocaleImpl()->FormatTime(localeName, format, time);
	}

	WString Locale::FormatNumber(const WString& number)const
	{
		return GetDefaultLocaleImpl()->FormatNumber(localeName, number);
	}

	WString Locale::FormatCurrency(const WString& currency)const
	{
		return GetDefaultLocaleImpl()->FormatCurrency(localeName, currency);
	}

	WString Locale::GetShortDayOfWeekName(vint dayOfWeek)const
	{
		return GetDefaultLocaleImpl()->GetShortDayOfWeekName(localeName, dayOfWeek);
	}

	WString Locale::GetLongDayOfWeekName(vint dayOfWeek)const
	{
		return GetDefaultLocaleImpl()->GetLongDayOfWeekName(localeName, dayOfWeek);
	}

	WString Locale::GetShortMonthName(vint month)const
	{
		return GetDefaultLocaleImpl()->GetShortMonthName(localeName, month);
	}

	WString Locale::GetLongMonthName(vint month)const
	{
		return GetDefaultLocaleImpl()->GetLongMonthName(localeName, month);
	}

	WString Locale::ToLower(const WString& str)const
	{
		return GetDefaultLocaleImpl()->ToLower(localeName, str);
	}

	WString Locale::ToUpper(const WString& str)const
	{
		return GetDefaultLocaleImpl()->ToUpper(localeName, str);
	}

	WString Locale::ToLinguisticLower(const WString& str)const
	{
		return GetDefaultLocaleImpl()->ToLinguisticLower(localeName, str);
	}

	WString Locale::ToLinguisticUpper(const WString& str)const
	{
		return GetDefaultLocaleImpl()->ToLinguisticUpper(localeName, str);
	}

	vint Locale::Compare(const WString& s1, const WString& s2, Normalization normalization)const
	{
		return GetDefaultLocaleImpl()->Compare(localeName, s1, s2, normalization);
	}

	vint Locale::CompareOrdinal(const WString& s1, const WString& s2)const
	{
		return GetDefaultLocaleImpl()->CompareOrdinal(s1, s2);
	}

	vint Locale::CompareOrdinalIgnoreCase(const WString& s1, const WString& s2)const
	{
		return GetDefaultLocaleImpl()->CompareOrdinalIgnoreCase(s1, s2);
	}

	collections::Pair<vint, vint> Locale::FindFirst(const WString& text, const WString& find, Normalization normalization)const
	{
		return GetDefaultLocaleImpl()->FindFirst(localeName, text, find, normalization);
	}

	collections::Pair<vint, vint> Locale::FindLast(const WString& text, const WString& find, Normalization normalization)const
	{
		return GetDefaultLocaleImpl()->FindLast(localeName, text, find, normalization);
	}

	bool Locale::StartsWith(const WString& text, const WString& find, Normalization normalization)const
	{
		return GetDefaultLocaleImpl()->StartsWith(localeName, text, find, normalization);
	}

	bool Locale::EndsWith(const WString& text, const WString& find, Normalization normalization)const
	{
		return GetDefaultLocaleImpl()->EndsWith(localeName, text, find, normalization);
	}
}
