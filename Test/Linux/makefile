.PHONY: all clean pre-build
.DEFAULT_GOAL := all

CPP_COMPILE_OPTIONS=-I ../../Import
include $(VCPROOT)/vl/makefile-cpp

pre-build:
	if ! [ -d ./Bin ]; then mkdir ./Bin; fi
	if ! [ -d ./Obj ]; then mkdir ./Obj; fi
	if ! [ -d ./Coverage ]; then mkdir ./Coverage; fi
	if ! [ -d ../Output ]; then mkdir ../Output; fi

clean:
	if [ -d ./Bin ]; then rm -r ./Bin; fi
	if [ -d ./Obj ]; then rm -r ./Obj; fi
	if [ -d ./Coverage ]; then rm -r ./Coverage; fi
	if [ -d ./../Output ]; then rm -r ../Output; fi

all:pre-build ./Bin/UnitTest

./Bin/UnitTest:./Obj/Vlpp.o ./Obj/FileSystem.o ./Obj/HttpUtility.o ./Obj/Locale.o ./Obj/Accessor.o ./Obj/BroadcastStream.o ./Obj/CacheStream.o ./Obj/CharFormat.o ./Obj/CompressionStream.o ./Obj/FileStream.o ./Obj/MemoryStream.o ./Obj/MemoryWrapperStream.o ./Obj/RecorderStream.o ./Obj/Threading.o ./Obj/ThreadingLinux.o ./Obj/TestFileSystem.o ./Obj/TestLocaleString.o ./Obj/TestStream.o ./Obj/TestThread.o ./Obj/Main.o
	$(CPP_LINK)

./Obj/Vlpp.o: ../../Import/Vlpp.cpp ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/FileSystem.o: ../../Source/FileSystem.cpp ../../Source/FileSystem.h ../../Import/Vlpp.h ../../Source/Stream/CharFormat.h ../../Source/Stream/Interfaces.h ../../Source/Stream/MemoryStream.h ../../Source/Stream/MemoryWrapperStream.h ../../Source/Locale.h ../../Source/Stream/FileStream.h ../../Source/Stream/Accessor.h
	$(CPP_COMPILE)

./Obj/HttpUtility.o: ../../Source/HttpUtility.cpp ../../Source/HttpUtility.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/Locale.o: ../../Source/Locale.cpp ../../Source/Locale.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/Accessor.o: ../../Source/Stream/Accessor.cpp ../../Source/Stream/Accessor.h ../../Source/Stream/Interfaces.h ../../Import/Vlpp.h ../../Source/Stream/MemoryStream.h
	$(CPP_COMPILE)

./Obj/BroadcastStream.o: ../../Source/Stream/BroadcastStream.cpp ../../Source/Stream/BroadcastStream.h ../../Source/Stream/Interfaces.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/CacheStream.o: ../../Source/Stream/CacheStream.cpp ../../Source/Stream/CacheStream.h ../../Source/Stream/Interfaces.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/CharFormat.o: ../../Source/Stream/CharFormat.cpp ../../Source/Stream/CharFormat.h ../../Source/Stream/Interfaces.h ../../Import/Vlpp.h ../../Source/Stream/MemoryStream.h ../../Source/Stream/MemoryWrapperStream.h
	$(CPP_COMPILE)

./Obj/CompressionStream.o: ../../Source/Stream/CompressionStream.cpp ../../Source/Stream/CompressionStream.h ../../Source/Stream/Interfaces.h ../../Import/Vlpp.h ../../Source/Stream/Accessor.h ../../Source/Stream/MemoryStream.h ../../Source/Stream/MemoryWrapperStream.h
	$(CPP_COMPILE)

./Obj/FileStream.o: ../../Source/Stream/FileStream.cpp ../../Source/Stream/FileStream.h ../../Source/Stream/Interfaces.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/MemoryStream.o: ../../Source/Stream/MemoryStream.cpp ../../Source/Stream/MemoryStream.h ../../Source/Stream/Interfaces.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/MemoryWrapperStream.o: ../../Source/Stream/MemoryWrapperStream.cpp ../../Source/Stream/MemoryWrapperStream.h ../../Source/Stream/Interfaces.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/RecorderStream.o: ../../Source/Stream/RecorderStream.cpp ../../Source/Stream/RecorderStream.h ../../Source/Stream/Interfaces.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/Threading.o: ../../Source/Threading.cpp ../../Source/Threading.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/ThreadingLinux.o: ../../Source/ThreadingLinux.cpp ../../Source/Threading.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/TestFileSystem.o: ../Source/TestFileSystem.cpp ../Source/../../Source/FileSystem.h ../../Import/Vlpp.h ../Source/../../Source/Stream/CharFormat.h ../Source/../../Source/Stream/Interfaces.h ../Source/../../Source/Stream/MemoryStream.h ../Source/../../Source/Stream/MemoryWrapperStream.h ../Source/../../Source/Locale.h ../Source/../../Source/Stream/FileStream.h ../Source/../../Source/Stream/Accessor.h ../Source/../../Source/Threading.h
	$(CPP_COMPILE)

./Obj/TestLocaleString.o: ../Source/TestLocaleString.cpp ../Source/../../Source/Locale.h ../../Import/Vlpp.h ../Source/../../Source/Stream/Accessor.h ../Source/../../Source/Stream/Interfaces.h ../Source/../../Source/Stream/MemoryStream.h ../Source/../../Source/Stream/CharFormat.h ../Source/../../Source/Stream/MemoryWrapperStream.h ../Source/../../Source/Stream/FileStream.h
	$(CPP_COMPILE)

./Obj/TestStream.o: ../Source/TestStream.cpp ../Source/../../Source/Stream/Interfaces.h ../../Import/Vlpp.h ../Source/../../Source/Stream/MemoryWrapperStream.h ../Source/../../Source/Stream/MemoryStream.h ../Source/../../Source/Stream/FileStream.h ../Source/../../Source/Stream/RecorderStream.h ../Source/../../Source/Stream/BroadcastStream.h ../Source/../../Source/Stream/CacheStream.h ../Source/../../Source/Stream/Accessor.h ../Source/../../Source/Stream/CharFormat.h ../Source/../../Source/Stream/CompressionStream.h ../Source/../../Source/Locale.h
	$(CPP_COMPILE)

./Obj/TestThread.o: ../Source/TestThread.cpp ../Source/../../Source/Threading.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/Main.o: Main.cpp ../../Import/Vlpp.h
	$(CPP_COMPILE)
