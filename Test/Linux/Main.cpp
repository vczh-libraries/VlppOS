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

int main(int argc, char* argv[])
{
	return unittest::UnitTest::RunAndDisposeTests(argc, argv);
}
