/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "CharFormat.h"

namespace vl
{
	namespace stream
	{
/***********************************************************************
Helper Functions
***********************************************************************/

		bool CanBeMbcs(unsigned char* buffer, vint size)
		{
			for(vint i=0;i<size;i++)
			{
				if(buffer[i]==0) return false;
			}
			return true;
		}

		bool CanBeUtf8(unsigned char* buffer, vint size)
		{
			for(vint i=0;i<size;i++)
			{
				unsigned char c=(unsigned char)buffer[i];
				if(c==0)
				{
					return false;
				}
				else
				{
					vint count10xxxxxx=0;
					if((c&0x80)==0x00) /* 0x0xxxxxxx */ count10xxxxxx=0;
					else if((c&0xE0)==0xC0) /* 0x110xxxxx */ count10xxxxxx=1;
					else if((c&0xF0)==0xE0) /* 0x1110xxxx */ count10xxxxxx=2;
					else if((c&0xF8)==0xF0) /* 0x11110xxx */ count10xxxxxx=3;
					else if((c&0xFC)==0xF8) /* 0x111110xx */ count10xxxxxx=4;
					else if((c&0xFE)==0xFC) /* 0x1111110x */ count10xxxxxx=5;

					if(size<=i+count10xxxxxx)
					{
						return false;
					}
					else
					{
						for(vint j=0;j<count10xxxxxx;j++)
						{
							c=(unsigned char)buffer[i+j+1];
							if((c&0xC0)!=0x80) /* 0x10xxxxxx */ return false;
						}
					}
					i+=count10xxxxxx;
				}
			}
			return true;
		}

		bool CanBeUtf16(unsigned char* buffer, vint size, bool& hitSurrogatePairs)
		{
			hitSurrogatePairs = false;
			if (size % 2 != 0) return false;
			bool needTrail = false;
			for (vint i = 0; i < size; i += 2)
			{
				vuint16_t c = buffer[i] + (buffer[i + 1] << 8);
				if (c == 0) return false;
				vint type = 0;
				if (0xD800 <= c && c <= 0xDBFF) type = 1;
				else if (0xDC00 <= c && c <= 0xDFFF) type = 2;
				if (needTrail)
				{
					if (type == 2)
					{
						needTrail = false;
					}
					else
					{
						return false;
					}
				}
				else
				{
					if (type == 1)
					{
						needTrail = true;
						hitSurrogatePairs = true;
					}
					else if (type != 0)
					{
						return false;
					}
				}
			}
			return !needTrail;
		}

		bool CanBeUtf16BE(unsigned char* buffer, vint size, bool& hitSurrogatePairs)
		{
			hitSurrogatePairs = false;
			if (size % 2 != 0) return false;
			bool needTrail = false;
			for (vint i = 0; i < size; i += 2)
			{
				vuint16_t c = buffer[i + 1] + (buffer[i] << 8);
				if (c == 0) return false;
				vint type = 0;
				if (0xD800 <= c && c <= 0xDBFF) type = 1;
				else if (0xDC00 <= c && c <= 0xDFFF) type = 2;
				if (needTrail)
				{
					if (type == 2)
					{
						needTrail = false;
					}
					else
					{
						return false;
					}
				}
				else
				{
					if (type == 1)
					{
						needTrail = true;
						hitSurrogatePairs = true;
					}
					else if (type != 0)
					{
						return false;
					}
				}
			}
			return !needTrail;
		}
		
/***********************************************************************
TestEncoding
***********************************************************************/

		extern void TestEncodingInternal(
			unsigned char* buffer,
			vint size,
			BomEncoder::Encoding& encoding,
			bool containsBom,
			bool utf16HitSurrogatePairs,
			bool utf16BEHitSurrogatePairs,
			bool roughMbcs,
			bool roughUtf8,
			bool roughUtf16,
			bool roughUtf16BE
		);

		void TestEncoding(unsigned char* buffer, vint size, BomEncoder::Encoding& encoding, bool& containsBom)
		{
			if (size >= 3 && strncmp((char*)buffer, "\xEF\xBB\xBF", 3) == 0)
			{
				encoding = BomEncoder::Utf8;
				containsBom = true;
			}
			else if (size >= 2 && strncmp((char*)buffer, "\xFF\xFE", 2) == 0)
			{
				encoding = BomEncoder::Utf16;
				containsBom = true;
			}
			else if (size >= 2 && strncmp((char*)buffer, "\xFE\xFF", 2) == 0)
			{
				encoding = BomEncoder::Utf16BE;
				containsBom = true;
			}
			else
			{
				encoding = BomEncoder::Mbcs;
				containsBom = false;

				bool utf16HitSurrogatePairs = false;
				bool utf16BEHitSurrogatePairs = false;
				bool roughMbcs = CanBeMbcs(buffer, size);
				bool roughUtf8 = CanBeUtf8(buffer, size);
				bool roughUtf16 = CanBeUtf16(buffer, size, utf16HitSurrogatePairs);
				bool roughUtf16BE = CanBeUtf16BE(buffer, size, utf16BEHitSurrogatePairs);

				vint roughCount = (roughMbcs ? 1 : 0) + (roughUtf8 ? 1 : 0) + (roughUtf16 ? 1 : 0) + (roughUtf16BE ? 1 : 0);
				if (roughCount == 1)
				{
					if (roughUtf8) encoding = BomEncoder::Utf8;
					else if (roughUtf16) encoding = BomEncoder::Utf16;
					else if (roughUtf16BE) encoding = BomEncoder::Utf16BE;
				}
				else if (roughCount > 1)
				{
					TestEncodingInternal(buffer, size, encoding, containsBom, utf16HitSurrogatePairs, utf16BEHitSurrogatePairs, roughMbcs, roughUtf8, roughUtf16, roughUtf16BE);
				}
			}
		}
	}
}
