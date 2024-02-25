#include "../../Source/Stream/Serialization.h"
#include "../../Source/Stream/MemoryStream.h"

using namespace vl;
using namespace vl::stream;
using namespace vl::collections;

namespace TestSerialization_TestObjects
{
	enum Seasons1
	{
		Spring,
		Summer,
		Autumn,
		Winter,
	};

	enum class Seasons2
	{
		Spring,
		Summer,
		Autumn,
		Winter,
	};

	struct Strings
	{
		WString sa;
		U8String sb;
		U16String sc;
		U32String sd;
	};
}
using namespace TestSerialization_TestObjects;

namespace vl
{
	namespace stream
	{
		namespace internal
		{
			BEGIN_SERIALIZATION(Strings)
				SERIALIZE(sa)
				SERIALIZE(sb)
				SERIALIZE(sc)
				SERIALIZE(sd)
			END_SERIALIZATION
		}
	}
}

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
		MemoryStream memoryStream;
		WString sa1 = L"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才", sa2;
		U8String sb1 = u8"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才", sb2;
		U16String sc1 = u"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才", sc2;
		U32String sd1 = U"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才", sd2;
		{
			internal::ContextFreeWriter writer(memoryStream);
			writer << sa1 << sb1 << sc1 << sd1;
		}
		memoryStream.SeekFromBegin(0);
		{
			internal::ContextFreeReader reader(memoryStream);
			reader << sa2 << sb2 << sc2 << sd2;
			TEST_ASSERT(memoryStream.Position() == memoryStream.Size());
		}
		TEST_ASSERT(sa1 == sa2);
		TEST_ASSERT(sb1 == sb2);
		TEST_ASSERT(sc1 == sc2);
		TEST_ASSERT(sd1 == sd2);
	});

	TEST_CASE(L"Serialize Enums (1)")
	{
		MemoryStream memoryStream;

		Seasons1 ea1 = Spring, ea2;
		Seasons1 eb1 = Summer, eb2;
		Seasons1 ec1 = Autumn, ec2;
		Seasons1 ed1 = Winter, ed2;

		Seasons2 eca1 = Seasons2::Spring, eca2;
		Seasons2 ecb1 = Seasons2::Summer, ecb2;
		Seasons2 ecc1 = Seasons2::Autumn, ecc2;
		Seasons2 ecd1 = Seasons2::Winter, ecd2;

		Strings e1, e2;
		e1.sa = L"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";
		e1.sb = u8"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";
		e1.sc = u"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";
		e1.sd = U"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";

		{
			internal::ContextFreeWriter writer(memoryStream);
			writer << ea1 << eb1 << ec1 << ed1 << eca1 << ecb1 << ecc1 << ecd1 << e1;
		}
		memoryStream.SeekFromBegin(0);
		{
			internal::ContextFreeReader reader(memoryStream);
			reader << ea2 << eb2 << ec2 << ed2 << eca2 << ecb2 << ecc2 << ecd2 << e2;
			TEST_ASSERT(memoryStream.Position() == memoryStream.Size());
		}

		TEST_ASSERT(ea1 == ea2);
		TEST_ASSERT(eb1 == eb2);
		TEST_ASSERT(ec1 == ec2);
		TEST_ASSERT(ed1 == ed2);

		TEST_ASSERT(eca1 == eca2);
		TEST_ASSERT(ecb1 == ecb2);
		TEST_ASSERT(ecc1 == ecc2);
		TEST_ASSERT(ecd1 == ecd2);

		TEST_ASSERT(e1.sa == e2.sa);
		TEST_ASSERT(e1.sb == e2.sb);
		TEST_ASSERT(e1.sc == e2.sc);
		TEST_ASSERT(e1.sd == e2.sd);
	});

	TEST_CASE(L"Serialize Generic Types")
	{
		MemoryStream memoryStream;
		auto a1 = Ptr(new Strings);
		Ptr<Strings> a2;
		a1->sa = L"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";
		a1->sb = u8"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";
		a1->sc = u"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";
		a1->sd = U"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";
		Nullable<Strings> b1 = *a1.Obj(), b2;
		{
			internal::ContextFreeWriter writer(memoryStream);
			writer << a1 << b1;
		}
		memoryStream.SeekFromBegin(0);
		{
			internal::ContextFreeReader reader(memoryStream);
			reader << a2 << b2;
			TEST_ASSERT(memoryStream.Position() == memoryStream.Size());
		}
		TEST_ASSERT(a2);
		TEST_ASSERT(b2);
		TEST_ASSERT(a1->sa == a2->sa);
		TEST_ASSERT(a1->sb == a2->sb);
		TEST_ASSERT(a1->sc == a2->sc);
		TEST_ASSERT(a1->sd == a2->sd);
		TEST_ASSERT(b1.Value().sa == b2.Value().sa);
		TEST_ASSERT(b1.Value().sb == b2.Value().sb);
		TEST_ASSERT(b1.Value().sc == b2.Value().sc);
		TEST_ASSERT(b1.Value().sd == b2.Value().sd);
	});

	TEST_CASE(L"Serialize Collections")
	{
		MemoryStream memoryStream;
		List<Seasons2> a1, a2;
		Array<Seasons2> b1(4), b2;
		Dictionary<vint, Seasons2> c1, c2;
		Group<vint, Seasons2> d1, d2;

		a1.Add(Seasons2::Spring);
		a1.Add(Seasons2::Summer);
		a1.Add(Seasons2::Autumn);
		a1.Add(Seasons2::Winter);

		b1[0] = Seasons2::Spring;
		b1[1] = Seasons2::Summer;
		b1[2] = Seasons2::Autumn;
		b1[3] = Seasons2::Winter;

		c1.Add(1, Seasons2::Spring);
		c1.Add(2, Seasons2::Summer);
		c1.Add(3, Seasons2::Autumn);
		c1.Add(4, Seasons2::Winter);

		d1.Add(1, Seasons2::Spring);
		d1.Add(1, Seasons2::Summer);
		d1.Add(2, Seasons2::Autumn);
		d1.Add(2, Seasons2::Winter);
		d1.Add(3, Seasons2::Spring);
		d1.Add(3, Seasons2::Summer);
		d1.Add(4, Seasons2::Autumn);
		d1.Add(4, Seasons2::Winter);

		{
			internal::ContextFreeWriter writer(memoryStream);
			writer << a1 << b1 << c1 << d1;
		}
		memoryStream.SeekFromBegin(0);
		{
			internal::ContextFreeReader reader(memoryStream);
			reader << a2 << b2 << c2 << d2;
			TEST_ASSERT(memoryStream.Position() == memoryStream.Size());
		}
		TEST_ASSERT(CompareEnumerable(a1, a2) == 0);
		TEST_ASSERT(CompareEnumerable(b1, b2) == 0);
		TEST_ASSERT(CompareEnumerable(c1, c2) == 0);
		TEST_ASSERT(CompareEnumerable(d1, d2) == 0);
	});

	TEST_CASE(L"Serialize Others")
	{
		MemoryStream memoryStream;
		wchar_t a1[] = L"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";
		wchar_t a2[sizeof(a1) / sizeof(*a1)];
		{
			internal::ContextFreeWriter writer(memoryStream);
			MemoryWrapperStream s(a1, sizeof(a1));
			writer << (IStream&)s;
		}
		memoryStream.SeekFromBegin(0);
		{
			internal::ContextFreeReader reader(memoryStream);
			MemoryWrapperStream s(a2, sizeof(a2));
			reader << (IStream&)s;
			TEST_ASSERT(memoryStream.Position() == memoryStream.Size());
		}
		TEST_ASSERT(memcmp(a1, a2, sizeof(a1)) == 0);
	});
}