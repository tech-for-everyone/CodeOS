# Qt6 Panels Makefile
# Produces .o files that the kernel Makefile links into the binary

QTDIR = $(abspath ../sysroot/usr)
MOC = /usr/lib/qt6/moc
CXX = x86_64-elf-g++
CC = x86_64-elf-gcc
AS = x86_64-elf-as
LD = x86_64-elf-ld

KERNEL_DIR = $(abspath ../../kernel)
DRIVERS_DIR = $(KERNEL_DIR)/drivers
ARCH_DIR = $(KERNEL_DIR)/arch/x86_64
PACKAGE_DIR = $(abspath ../../pkgs/core/panels/src)
PLATFORM_DIR = $(abspath ../platform)
THIS_DIR = $(abspath .)

# Host C++ standard library headers (x86_64-elf cross-compiler lacks libstdc++)
HOST_CXX_INCLUDES = \
    -isystem /usr/include/c++/16 \
    -isystem /usr/include/c++/16/x86_64-pc-linux-gnu \
    -isystem $(QTDIR)/include/stubs \
    -isystem /usr/lib/gcc/x86_64-elf/16.1.0/include

QT6_INCLUDES = \
    -I$(QTDIR)/include \
    -I$(QTDIR)/include/QtCore \
    -I$(QTDIR)/include/QtGui \
    -I$(QTDIR)/include/QtWidgets \
    -I$(QTDIR)/include/QtCore/6.11.1 \
    -I$(QTDIR)/include/QtGui/6.11.1 \
    -I$(QTDIR)/include/QtWidgets/6.11.1 \
    -I$(QTDIR)/include/QtGui/6.11.1/QtGui \
    -I$(QTDIR)/include/QtCore/6.11.1/QtCore

CODEOS_LIB_INCLUDES = \
    -I$(abspath ../fonts/include) \
    -I$(abspath ../image/include)

KERNEL_INCLUDES = \
    -I$(KERNEL_DIR) \
    -I$(KERNEL_DIR)/kernel \
    -I$(DRIVERS_DIR) \
    -I$(ARCH_DIR) \
    -I$(PACKAGE_DIR) \
    -I$(KERNEL_DIR)/lvgl \
    -I$(abspath ../../include) \
    -I/home/codeosuser/Openweb/litehtml/include

BASE_FLAGS = \
    -ffreestanding \
    -mcmodel=large \
    -mno-red-zone \
    -mno-mmx -mno-sse -mno-sse2 \
    -nostdlib -nostartfiles -nodefaultlibs \
    -fno-stack-protector -fno-PIC \
    -Wall -Wextra -Wno-builtin-declaration-mismatch \
    -O2 -g -MMD -MP

CXXFLAGS = \
    -mcmodel=large \
    -mno-red-zone \
    -nostdlib -nostartfiles -nodefaultlibs \
    -fno-stack-protector -fno-PIC \
    -Wall -Wextra -Wno-builtin-declaration-mismatch \
    -O2 -g -MMD -MP \
    -fno-exceptions -fno-rtti \
    -fno-threadsafe-statics \
    -std=gnu++17 \
    $(HOST_CXX_INCLUDES) \
    $(QT6_INCLUDES) \
    $(CODEOS_LIB_INCLUDES) \
    $(KERNEL_INCLUDES) \
    -I$(PLATFORM_DIR) \
    -DQT_STATICPLUGIN \
    -D__codeos__ \
    -DQT_NO_OPENGL \
    -DQT_NO_PRINTER \
    -DQT_NO_EXCEPTIONS \
    -include $(abspath $(QTDIR)/include/stubs/cxx_compat.h)

CFLAGS = $(BASE_FLAGS) -std=gnu11 \
    $(QT6_INCLUDES) \
    $(CODEOS_LIB_INCLUDES) \
    $(KERNEL_INCLUDES) \
    -isystem $(QTDIR)/include/stubs \
    -I$(PLATFORM_DIR)

APP_OBJS = terminal.o app_host.o calc.o settings.o \
    about.o sys_info.o exit.o \
    sys_mon.o explorer.o \
    netbeam.o ziggy.o notes.o clock.o convert.o

OBJS = codeos_platform.o codeos_plugin.o \
    qt_panels_common.o qt_panels_shell.o $(APP_OBJS) qt_panels_openweb.o \
    qt_panels_installer.o qt_panels_lt.o qt_panels_manager.o qt_panels_main.o \
    font_embed.o font_data.o moc_qt_panels.o ow_html.o openweb_core.o qt6_got.o \
    codeos_font.o codeos_fdb.o codeos_freetype.o codeos_image.o \
    codeos_window_manager.o codeos_file_manager.o codeos_terminal.o \
    moc_codeos_window_manager.o moc_codeos_file_manager.o moc_codeos_terminal.o

.PHONY: all clean

all: $(OBJS)

# ── MOC ──
$(PLATFORM_DIR)/codeos_plugin.moc: $(PLATFORM_DIR)/codeos_plugin.cpp
	$(MOC) $(QT6_INCLUDES) $< -o $@

moc_qt_panels.cpp: qt_panels.h
	$(MOC) $(QT6_INCLUDES) $< -o $@

# ── Compilation ──
codeos_platform.o: $(PLATFORM_DIR)/codeos_platform.c $(PLATFORM_DIR)/codeos_platform.h
	$(CC) $(CFLAGS) -c $< -o $@

codeos_plugin.o: $(PLATFORM_DIR)/codeos_plugin.cpp $(PLATFORM_DIR)/codeos_plugin.h $(PLATFORM_DIR)/codeos_plugin.moc
	$(CXX) $(CXXFLAGS) -c $(PLATFORM_DIR)/codeos_plugin.cpp -o $@

qt_panels_common.o: qt_panels_common.cpp qt_panels_common.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

qt_panels_shell.o: qt_panels_shell.cpp qt_panels.h qt_panels_common.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(APP_OBJS): %.o: ../../pkgs/extra/qt_apps/%.cpp qt_panels.h qt_panels_common.h
	$(CXX) $(CXXFLAGS) -I. -c $< -o $@

qt_panels_openweb.o: qt_panels_openweb.cpp qt_panels.h qt_panels_common.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

qt_panels_installer.o: qt_panels_installer.cpp qt_panels.h qt_panels_common.h $(KERNEL_DIR)/kernel/installer_qt.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

qt_panels_lt.o: qt_panels_lt.cpp qt_panels.h qt_panels_common.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

qt_panels_manager.o: qt_panels_manager.cpp qt_panels.h qt_panels_common.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

qt_panels_main.o: qt_panels_main.cpp qt_panels.h qt_panels_common.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

font_embed.o: font_embed.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

font_data.o: LiberationSans-Regular.ttf
	$(LD) -r -b binary -o $@ $<

ow_html.o: ow_html.c ow_html.h
	$(CC) $(CFLAGS) -c $< -o $@

openweb_core.o: openweb_core.c openweb_core.h ../../pkgs/core/panels/src/ow_http.h ow_html.h
	$(CC) $(CFLAGS) -c $< -o $@

codeos_font.o: ../fonts/src/codeos_font.c ../fonts/include/codeos_font.h
	$(CC) $(CFLAGS) -c $< -o $@

codeos_fdb.o: codeos_fdb.cpp ../fonts/include/codeos_font.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

codeos_freetype.o: ../fonts/src/codeos_freetype.c ../fonts/include/codeos_freetype.h
	$(CC) $(CFLAGS) -c $< -o $@

codeos_image.o: ../image/src/codeos_image.c ../image/include/codeos_image.h
	$(CC) $(CFLAGS) -c $< -o $@

codeos_window_manager.o: codeos_window_manager.cpp codeos_window_manager.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

codeos_file_manager.o: codeos_file_manager.cpp codeos_file_manager.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

codeos_terminal.o: codeos_terminal.cpp codeos_terminal.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

moc_codeos_window_manager.cpp: codeos_window_manager.h
	$(MOC) $(QT6_INCLUDES) $< -o $@

moc_codeos_file_manager.cpp: codeos_file_manager.h
	$(MOC) $(QT6_INCLUDES) $< -o $@

moc_codeos_terminal.cpp: codeos_terminal.h
	$(MOC) $(QT6_INCLUDES) $< -o $@

moc_codeos_window_manager.o: moc_codeos_window_manager.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

moc_codeos_file_manager.o: moc_codeos_file_manager.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

moc_codeos_terminal.o: moc_codeos_terminal.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

qt6_got.o: qt6_got.S
	$(AS) --64 -o $@ $<

moc_qt_panels.o: moc_qt_panels.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) moc_*.cpp *.d font_data.o
distclean: clean
	rm -f $(PLATFORM_DIR)/codeos_plugin.moc

-include $(OBJS:.o=.d)
