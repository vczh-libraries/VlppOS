.PHONY: all clean pre-build
.DEFAULT_GOAL := all

CPP_COMPILE_OPTIONS=-I ../../../Import
include $(VCPROOT)/vl/makefile-cpp

pre-build:
	if ! [ -d ./Bin ]; then mkdir ./Bin; fi
	if ! [ -d ./Obj ]; then mkdir ./Obj; fi
	if ! [ -d ./Coverage ]; then mkdir ./Coverage; fi

clean:
	if [ -d ./Bin ]; then rm -r ./Bin; fi
	if [ -d ./Obj ]; then rm -r ./Obj; fi
	if [ -d ./Coverage ]; then rm -r ./Coverage; fi

all:pre-build ./Bin/TuiPlayground

./Bin/TuiPlayground:./Obj/Vlpp.o ./Obj/Vlpp.Linux.o ./Obj/Threading.o ./Obj/Threading.Linux.o ./Obj/TUI.Input.o ./Obj/TUI.o ./Obj/TUI.Linux.o ./Obj/Main.o
	$(CPP_LINK)

./Obj/Vlpp.o: ../../../Import/Vlpp.cpp
	$(CPP_COMPILE)

./Obj/Vlpp.Linux.o: ../../../Import/Vlpp.Linux.cpp
	$(CPP_COMPILE)

./Obj/Threading.o: ../../../Source/Threading.cpp
	$(CPP_COMPILE)

./Obj/Threading.Linux.o: ../../../Source/Threading.Linux.cpp
	$(CPP_COMPILE)

./Obj/TUI.Input.o: ../../../Source/TUI/TUI.Input.cpp
	$(CPP_COMPILE)

./Obj/TUI.o: ../../../Source/TUI/TUI.cpp
	$(CPP_COMPILE)

./Obj/TUI.Linux.o: ../../../Source/TUI/TUI.Linux.cpp
	$(CPP_COMPILE)

./Obj/Main.o: ../../UnitTest/TuiPlayground/Main.cpp
	$(CPP_COMPILE)
