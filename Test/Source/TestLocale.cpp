#include "../../Source/Locale.h"
#include "../../Source/Stream/Accessor.h"
#include "../../Source/Stream/EncodingStream.h"
#include "../../Source/Encoding/CharFormat/CharFormat.h"
#include "../../Source/Stream/FileStream.h"

using namespace vl;
using namespace vl::collections;
using namespace vl::stream;

extern WString GetTestOutputPath();

TEST_FILE
{
	TEST_CASE(L"Locale comparisong")
	{
		{
			Locale a, b;
			TEST_ASSERT((a == b) == true);
			TEST_ASSERT((a != b) == false);
			TEST_ASSERT((a < b) == false);
			TEST_ASSERT((a <= b) == true);
			TEST_ASSERT((a > b) == false);
			TEST_ASSERT((a >= b) == true);
		}
		{
			Locale a(L"a"), b(L"b");
			TEST_ASSERT((a == b) == false);
			TEST_ASSERT((a != b) == true);
			TEST_ASSERT((a < b) == true);
			TEST_ASSERT((a <= b) == true);
			TEST_ASSERT((a > b) == false);
			TEST_ASSERT((a >= b) == false);
		}
	});

	TEST_CASE(L"Print locale awared data")
	{
		DateTime dt = DateTime::LocalTime();

		FileStream fileStream(GetTestOutputPath() + L"Locale.txt", FileStream::WriteOnly);
		BomEncoder encoder(BomEncoder::Utf16);
		EncoderStream encoderStream(fileStream, encoder);
		StreamWriter writer(encoderStream);

		writer.WriteLine(L"Invariant locale: " + Locale::Invariant().GetName());
		writer.WriteLine(L"User default locale: " + Locale::UserDefault().GetName());
		writer.WriteLine(L"System default locale: " + Locale::SystemDefault().GetName());

		writer.WriteLine(L"========================================");
		{
			Locale locale = Locale::UserDefault();
			WString input = L"abcdeABCDEａｂｃｄｅＡＢＣＤＥ战斗戰鬥あいうえおアイウエオｱｲｳｴｵ";
			writer.WriteLine(L"[Normal] => " + input);
			writer.WriteLine(L"[ToFullWidth] => " + locale.ToFullWidth(input));
			writer.WriteLine(L"[ToHalfWidth] => " + locale.ToHalfWidth(input));
			writer.WriteLine(L"[ToHiragana] => " + locale.ToHiragana(input));
			writer.WriteLine(L"[ToKatagana] => " + locale.ToKatagana(input));
			writer.WriteLine(L"[ToLower] => " + locale.ToLower(input));
			writer.WriteLine(L"[ToUpper] => " + locale.ToUpper(input));
			writer.WriteLine(L"[ToLinguisticLower] => " + locale.ToLinguisticLower(input));
			writer.WriteLine(L"[ToLinguisticUpper] => " + locale.ToLinguisticUpper(input));
			writer.WriteLine(L"[ToSimplifiedChinese] => " + locale.ToSimplifiedChinese(input));
			writer.WriteLine(L"[ToTraditionalChinese] => " + locale.ToTraditionalChinese(input));
			writer.WriteLine(L"[ToTileCase] => " + locale.ToTileCase(input));
		}

		List<Locale> locales;
		Locale::Enumerate(locales);
		locales.Insert(0, Locale::Invariant());
		for (auto locale : locales)
		{
			writer.WriteLine(L"========================================");
			writer.WriteLine(L"Locale: " + locale.GetName());
			writer.WriteLine(L"[Number 0] => " + locale.FormatNumber(L"0"));
			writer.WriteLine(L"[Number 1] => " + locale.FormatNumber(L"1"));
			writer.WriteLine(L"[Number -1] => " + locale.FormatNumber(L"-1"));
			writer.WriteLine(L"[Number 100.2] => " + locale.FormatNumber(L"100.2"));
			writer.WriteLine(L"[Number -100.2] => " + locale.FormatNumber(L"-100.2"));
			writer.WriteLine(L"[Currency 0] => " + locale.FormatCurrency(L"0"));
			writer.WriteLine(L"[Currency 1] => " + locale.FormatCurrency(L"1"));
			writer.WriteLine(L"[Currency -1] => " + locale.FormatCurrency(L"-1"));
			writer.WriteLine(L"[Currency 100.2] => " + locale.FormatCurrency(L"100.2"));
			writer.WriteLine(L"[Currency -100.2] => " + locale.FormatCurrency(L"-100.2"));
			{
				writer.WriteString(L"[ShortDayOfWeek]");
				for (vint i = 0; i <= 6; i++)
				{
					writer.WriteString(L" " + locale.GetShortDayOfWeekName(i));
				}
				writer.WriteLine(L"");

				writer.WriteString(L"[LongDayOfWeek]");
				for (vint i = 0; i <= 6; i++)
				{
					writer.WriteString(L" " + locale.GetLongDayOfWeekName(i));
				}
				writer.WriteLine(L"");

				writer.WriteString(L"[ShortMonth]");
				for (vint i = 1; i <= 12; i++)
				{
					writer.WriteString(L" " + locale.GetShortMonthName(i));
				}
				writer.WriteLine(L"");

				writer.WriteString(L"[LongMonth]");
				for (vint i = 1; i <= 12; i++)
				{
					writer.WriteString(L" " + locale.GetLongMonthName(i));
				}
				writer.WriteLine(L"");
			}
			{
				List<WString> formats;
				locale.GetLongDateFormats(formats);
				for (auto format : formats)
				{
					writer.WriteLine(L"[LongDate]" + format + L" => " + locale.FormatDate(format, dt));
				}
			}
			{
				List<WString> formats;
				locale.GetShortDateFormats(formats);
				for (auto format : formats)
				{
					writer.WriteLine(L"[ShortDate]" + format + L" => " + locale.FormatDate(format, dt));
				}
			}
			{
				List<WString> formats;
				locale.GetYearMonthDateFormats(formats);
				for (auto format : formats)
				{
					writer.WriteLine(L"[YearMonth]" + format + L" => " + locale.FormatDate(format, dt));
				}
			}
			{
				List<WString> formats;
				locale.GetLongTimeFormats(formats);
				for (auto format : formats)
				{
					writer.WriteLine(L"[LongTime]" + format + L" => " + locale.FormatTime(format, dt));
				}
			}
			{
				List<WString> formats;
				locale.GetShortTimeFormats(formats);
				for (auto format : formats)
				{
					writer.WriteLine(L"[ShortTime]" + format + L" => " + locale.FormatTime(format, dt));
				}
			}
		}
	});
}