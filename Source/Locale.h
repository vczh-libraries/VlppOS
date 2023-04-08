/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_LOCALE
#define VCZH_LOCALE

#include <Vlpp.h>

namespace vl
{
	/// <summary>Locale awared operations. Macro "INVLOC" is a shortcut to get a invariant locale.</summary>
	/// <remarks>
	/// <p>
	/// For all string operations that the normalization does not set to <b>None</b>,
	/// and all non-string operations,
	/// the result is platform-dependent.
	/// This class is designed to process human-readable text,
	/// do not rely on the result.
	/// </p>
	/// <p>
	/// In Linux and macOS, only en-US is supported, with a hard-coded set of date and time formats,
	/// and string operations only support <b>None</b> and <b>IgnoreCase</b> for normalization.
	/// </p>
	/// </remarks>
	class Locale : public Object
	{
	protected:
		WString						localeName;

	public:
		/// <summary>Create a locale with a specified local name.</summary>
		/// <param name="_localeName">The name of the locale. If it is not provided, it becomes the invariant locale.</param>
		/// <remarks>
		/// In Windows, the specified locale need to be installed in order to take effect.
		/// In Linux and macOS, only en-US is supported.
		/// </remarks>
		Locale() = default;
		Locale(const Locale&) = default;
		Locale(Locale&&) = default;
		~Locale() = default;

		Locale& operator=(const Locale&) = default;
		Locale& operator=(Locale&&) = default;

		Locale(const WString& _localeName);

		std::strong_ordering		operator<=>(const Locale& locale)const { return localeName <=> locale.localeName; }
		bool						operator==(const Locale& locale)const { return localeName == locale.localeName; }

		/// <summary>Get the invariant locale. An invariant locale is neutral, it is not awared of any language specified thing.</summary>
		/// <returns>The invariant locale.</returns>
		static Locale				Invariant();
		/// <summary>Get the system default locale. This locale controls the code page that used by the the system to interpret ANSI string buffers.</summary>
		/// <returns>The system default locale.</returns>
		static Locale				SystemDefault();
		/// <summary>Get the user default locale. This locale reflect the user's settings and UI language.</summary>
		/// <returns>The user default locale.</returns>
		static Locale				UserDefault();
		/// <summary>Get all supported locales.</summary>
		/// <param name="locales">All supported locales.</param>
		static void					Enumerate(collections::List<Locale>& locales);

		/// <summary>Get the name of this locale.</summary>
		/// <returns>The name of this locale.</returns>
		const WString&				GetName()const;

		/// <summary>Get all short date formats for this locale.</summary>
		/// <param name="formats">Returns all formats.</param>
		void						GetShortDateFormats(collections::List<WString>& formats)const;
		/// <summary>Get all long date formats for this locale.</summary>
		/// <param name="formats">Returns all formats.</param>
		void						GetLongDateFormats(collections::List<WString>& formats)const;
		/// <summary>Get all Year-Month date formats for this locale.</summary>
		/// <param name="formats">Returns all formats.</param>
		void						GetYearMonthDateFormats(collections::List<WString>& formats)const;
		/// <summary>Get all long time formats for this locale.</summary>
		/// <param name="formats">Returns all formats.</param>
		void						GetLongTimeFormats(collections::List<WString>& formats)const;
		/// <summary>Get all short time formats for this locale.</summary>
		/// <param name="formats">Returns all formats.</param>
		void						GetShortTimeFormats(collections::List<WString>& formats)const;

		/// <summary>Convert a date to a formatted string.</summary>
		/// <returns>The formatted string.</returns>
		/// <param name="format">The format to use.</param>
		/// <param name="date">The date to convert.</param>
		/// <remarks>
		/// The value of the "format" argument must come from any of the following functions.
		/// Otherwise the behavior is undefined.
		/// <ul>
		///   <li><see cref="GetShortDateFormats"/></li>
		///   <li><see cref="GetLongDateFormats"/></li>
		///   <li><see cref="GetYearMonthDateFormats"/></li>
		/// </ul>
		/// </remarks>
		WString						FormatDate(const WString& format, DateTime date)const;
		/// <summary>Convert a time to a formatted string.</summary>
		/// <returns>The formatted string.</returns>
		/// <param name="format">The format to use.</param>
		/// <param name="time">The time to convert.</param>
		/// <remarks>
		/// The value of the "format" argument must come from any of the following functions.
		/// Otherwise the behavior is undefined.
		/// <ul>
		///   <li><see cref="GetLongTimeFormats"/></li>
		///   <li><see cref="GetShortTimeFormats"/></li>
		/// </ul>
		/// </remarks>
		WString						FormatTime(const WString& format, DateTime time)const;

		/// <summary>Convert a number to a formatted string according to the locale.</summary>
		/// <returns>The formatted string.</returns>
		/// <param name="number">The number to convert.</param>
		WString						FormatNumber(const WString& number)const;
		/// <summary>Convert a currency (money) to a formatted string according to the locale.</summary>
		/// <returns>The formatted string.</returns>
		/// <param name="currency">The currency to convert.</param>
		WString						FormatCurrency(const WString& currency)const;

		/// <summary>Get the short display string of a day of week according to the locale.</summary>
		/// <returns>The display string.</returns>
		/// <param name="dayOfWeek">Day of week, begins from 0 as Sunday.</param>
		WString						GetShortDayOfWeekName(vint dayOfWeek)const;
		/// <summary>Get the long display string of a day of week according to the locale.</summary>
		/// <returns>The display string.</returns>
		/// <param name="dayOfWeek">Day of week, begins from 0 as Sunday.</param>
		WString						GetLongDayOfWeekName(vint dayOfWeek)const;
		/// <summary>Get the short display string of a month according to the locale.</summary>
		/// <returns>The display string.</returns>
		/// <param name="month">Month, begins from 1 as January.</param>
		WString						GetShortMonthName(vint month)const;
		/// <summary>Get the long display string of a month according to the locale.</summary>
		/// <returns>The display string.</returns>
		/// <param name="month">Month, begins from 1 as January.</param>
		WString						GetLongMonthName(vint month)const;
		
#ifdef VCZH_MSVC
		/// <summary>Convert characters to the full width.</summary>
		/// <returns>The converted string.</returns>
		/// <param name="str">The string to convert.</param>
		/// <remarks>This function is only available in Windows.</remarks>
		WString						ToFullWidth(const WString& str)const;
		/// <summary>Convert characters to the half width.</summary>
		/// <returns>The converted string.</returns>
		/// <param name="str">The string to convert.</param>
		/// <remarks>This function is only available in Windows.</remarks>
		WString						ToHalfWidth(const WString& str)const;
		/// <summary>Convert characters to the Hiragana.</summary>
		/// <returns>The converted string.</returns>
		/// <param name="str">The string to convert.</param>
		/// <remarks>This function is only available in Windows.</remarks>
		WString						ToHiragana(const WString& str)const;
		/// <summary>Convert characters to the Katagana.</summary>
		/// <returns>The converted string.</returns>
		/// <param name="str">The string to convert.</param>
		/// <remarks>This function is only available in Windows.</remarks>
		WString						ToKatagana(const WString& str)const;
#endif
		
		/// <summary>Convert characters to the lower case using the file system rule.</summary>
		/// <returns>The converted string.</returns>
		/// <param name="str">The string to convert.</param>
		WString						ToLower(const WString& str)const;
		/// <summary>Convert characters to the upper case using the file system rule.</summary>
		/// <returns>The converted string.</returns>
		/// <param name="str">The string to convert.</param>
		WString						ToUpper(const WString& str)const;
		/// <summary>Convert characters to the lower case using the linguistic rule.</summary>
		/// <returns>The converted string.</returns>
		/// <param name="str">The string to convert.</param>
		WString						ToLinguisticLower(const WString& str)const;
		/// <summary>Convert characters to the upper case using the linguistic rule.</summary>
		/// <returns>The converted string.</returns>
		/// <param name="str">The string to convert.</param>
		WString						ToLinguisticUpper(const WString& str)const;

#ifdef VCZH_MSVC
		/// <summary>Convert characters to Simplified Chinese.</summary>
		/// <returns>The converted string.</returns>
		/// <param name="str">The string to convert.</param>
		/// <remarks>This function is only available in Windows.</remarks>
		WString						ToSimplifiedChinese(const WString& str)const;
		/// <summary>Convert characters to the Traditional Chinese.</summary>
		/// <returns>The converted string.</returns>
		/// <param name="str">The string to convert.</param>
		/// <remarks>This function is only available in Windows.</remarks>
		WString						ToTraditionalChinese(const WString& str)const;
		/// <summary>Convert characters to the tile case, in which the first letter of each major word is capitalized.</summary>
		/// <returns>The converted string.</returns>
		/// <param name="str">The string to convert.</param>
		/// <remarks>This function is only available in Windows.</remarks>
		WString						ToTileCase(const WString& str)const;
#endif

		/// <summary>Mergable flags controlling how to normalize a string.</summary>
		enum Normalization
		{
			/// <summary>Do nothing.</summary>
			None=0,
			/// <summary>Ignore case using the file system rule.</summary>
			IgnoreCase=1,
#ifdef VCZH_MSVC
			/// <summary>Ignore case using the linguistic rule. This value is only available in Windows.</summary>
			IgnoreCaseLinguistic=2,
			/// <summary>Ignore the difference between between hiragana and katakana characters. This value is only available in Windows.</summary>
			IgnoreKanaType=4,
			/// <summary>Ignore nonspacing characters. This value is only available in Windows.</summary>
			IgnoreNonSpace=8,
			/// <summary>Ignore symbols and punctuation. This value is only available in Windows.</summary>
			IgnoreSymbol=16,
			/// <summary>Ignore the difference between half-width and full-width characters. This value is only available in Windows.</summary>
			IgnoreWidth=32,
			/// <summary>Treat digits as numbers during sorting. This value is only available in Windows.</summary>
			DigitsAsNumbers=64,
			/// <summary>Treat punctuation the same as symbols. This value is only available in Windows.</summary>
			StringSoft=128,
#endif
		};

		/// <summary>Compare two strings.</summary>
		/// <returns>Returns 0 if two strings are equal. Returns a positive number if the first string is larger. Returns a negative number if the second string is larger. When sorting strings, larger strings are put after then smaller strings.</returns>
		/// <param name="s1">The first string to compare.</param>
		/// <param name="s2">The second string to compare.</param>
		/// <param name="normalization">Flags controlling how to normalize a string.</param>
		vint									Compare(const WString& s1, const WString& s2, Normalization normalization)const;
		/// <summary>Compare two strings to test binary equivalence.</summary>
		/// <returns>Returns 0 if two strings are equal. Returns a positive number if the first string is larger. Returns a negative number if the second string is larger. When sorting strings, larger strings are put after then smaller strings.</returns>
		/// <param name="s1">The first string to compare.</param>
		/// <param name="s2">The second string to compare.</param>
		vint									CompareOrdinal(const WString& s1, const WString& s2)const;
		/// <summary>Compare two strings to test binary equivalence, ignoring case.</summary>
		/// <returns>Returns 0 if two strings are equal. Returns a positive number if the first string is larger. Returns a negative number if the second string is larger. When sorting strings, larger strings are put after then smaller strings.</returns>
		/// <param name="s1">The first string to compare.</param>
		/// <param name="s2">The second string to compare.</param>
		vint									CompareOrdinalIgnoreCase(const WString& s1, const WString& s2)const;
		/// <summary>Find the first position that the sub string appears in a text.</summary>
		/// <returns>Returns a pair of numbers, the first number indicating the position in the text, the second number indicating the size of the equivalence sub string in the text.</returns>
		/// <param name="text">The text to find the sub string.</param>
		/// <param name="find">The sub string to match.</param>
		/// <param name="normalization">Flags controlling how to normalize a string.</param>
		/// <remarks>For any normalization that is not <b>None</b>, the found sub string could be different to the string you want to find.</remarks>
		collections::Pair<vint, vint>			FindFirst(const WString& text, const WString& find, Normalization normalization)const;
		/// <summary>Find the last position that the sub string appears in a text.</summary>
		/// <returns>Returns a pair of numbers, the first number indicating the position in the text, the second number indicating the size of the equivalence sub string in the text.</returns>
		/// <param name="text">The text to find the sub string.</param>
		/// <param name="find">The sub string to match.</param>
		/// <param name="normalization">Flags controlling how to normalize a string.</param>
		/// <remarks>For any normalization that is not <b>None</b>, the found sub string could be different to the string you want to find.</remarks>
		collections::Pair<vint, vint>			FindLast(const WString& text, const WString& find, Normalization normalization)const;
		/// <summary>Test is the prefix of the text equivalence to the provided sub string.</summary>
		/// <returns>Returns true if the prefix of the text equivalence to the provided sub string.</returns>
		/// <param name="text">The text to test the prefix.</param>
		/// <param name="find">The sub string to match.</param>
		/// <param name="normalization">Flags controlling how to normalize a string.</param>
		/// <remarks>For any normalization that is not <b>None</b>, the found prefix could be different to the string you want to find.</remarks>
		bool									StartsWith(const WString& text, const WString& find, Normalization normalization)const;
		/// <summary>Test is the postfix of the text equivalence to the provided sub string.</summary>
		/// <returns>Returns true if the postfix of the text equivalence to the provided sub string.</returns>
		/// <param name="text">The text to test the postfix.</param>
		/// <param name="find">The sub string to match.</param>
		/// <param name="normalization">Flags controlling how to normalize a string.</param>
		/// <remarks>For any normalization that is not <b>None</b>, the postfix could be different to the string you want to find.</remarks>
		bool									EndsWith(const WString& text, const WString& find, Normalization normalization)const;
	};

#define INVLOC vl::Locale::Invariant()
}

#endif