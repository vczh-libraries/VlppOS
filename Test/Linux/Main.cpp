#include "../../Import/Vlpp.h"

using namespace vl;

WString GetTestResourcePath()
{
	return L"../Resources/";
}

WString GetTestOutputPath()
{
	return L"../Output/";
}

int main()
{
	unittest::UnitTest::RunAndDisposeTests();
	return 0;
}
