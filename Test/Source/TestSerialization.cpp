#include "../../Source/Stream/Serialization.h"
#include "../../Source/Stream/MemoryStream.h"

using namespace vl;
using namespace vl::stream;
using namespace vl::collections;

TEST_FILE
{
	TEST_CASE(L"Serialize PODs")
	{
		MemoryStream memoryStream;
		vint8_t a1 = 2, a2;
		vuint8_t b1 = 3, b2;
		vint16_t c1 = 5, c2;
		vuint16_t d1 = 7, d2;
		vint32_t e1 = 11, e2;
		vuint32_t f1 = 13, f2;
		vint64_t g1 = 17, g2;
		vuint64_t h1 = 19, h2;
		float i1 = 23, i2;
		double j1 = 27, j2;
		char cha1 = 'A', cha2;
		wchar_t chb1 = L'B', chb2;
		char8_t chc1 = u8'C', chc2;
		char16_t chd1 = u'D', chd2;
		char32_t che1 = U'E', che2;
		bool ba1 = true, ba2;
		bool bb1 = false, bb2;
		{
			internal::ContextFreeWriter writer(memoryStream);
			writer << a1 << b1 << c1 << d1 << e1 << f1 << g1 << h1 << i1 << j1;
			writer << cha1 << chb1 << chc1 << chd1 << che1;
			writer << ba1 << bb1;
		}
		memoryStream.SeekFromBegin(0);
		{
			internal::ContextFreeReader reader(memoryStream);
			reader << a2 << b2 << c2 << d2 << e2 << f2 << g2 << h2 << i2 << j2;
			reader << cha2 << chb2 << chc2 << chd2 << che2;
			reader << ba2 << bb2;
			TEST_ASSERT(memoryStream.Position() == memoryStream.Size());
		}
		TEST_ASSERT(a1 == a2);
		TEST_ASSERT(b1 == b2);
		TEST_ASSERT(c1 == c2);
		TEST_ASSERT(d1 == d2);
		TEST_ASSERT(e1 == e2);
		TEST_ASSERT(f1 == f2);
		TEST_ASSERT(g1 == g2);
		TEST_ASSERT(h1 == h2);
		TEST_ASSERT(i1 == i2);

		TEST_ASSERT(cha1 == cha2);
		TEST_ASSERT(chb1 == chb2);
		TEST_ASSERT(chc1 == chc2);
		TEST_ASSERT(chd1 == chd2);
		TEST_ASSERT(che1 == che2);

		TEST_ASSERT(ba1 == ba2);
		TEST_ASSERT(bb1 == bb2);
	});

	TEST_CASE(L"Serialize Strings")
	{
	});

	TEST_CASE(L"Serialize Enums and Structs")
	{
	});

	TEST_CASE(L"Serialize Generic Types")
	{
	});

	TEST_CASE(L"Serialize Collections")
	{
	});

	TEST_CASE(L"Serialize Others")
	{
	});
}