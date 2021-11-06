/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "Locale.h"

namespace vl
{

/***********************************************************************
Locale
***********************************************************************/

	Locale::Locale(const WString& _localeName)
		:localeName(_localeName)
	{
	}

	Locale::~Locale()
	{
	}

	const WString& Locale::GetName()const
	{
		return localeName;
	}
}
