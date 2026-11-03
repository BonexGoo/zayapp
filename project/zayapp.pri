TARGET = zayapp
TEMPLATE = app

QT_ENABLE_GRAPHICS = ok #ok #no
QT_ADD_PLUGINS_FIREBASE = no #ok #no
QT_PACKAGE_RPATH = no #ok #no

!include(../../Boss2D/project/boss2d.pri) {
    error("Couldn't find the boss2d.pri file...")
}

QT -= bluetooth
wasm{
    QT_WASM_INITIAL_MEMORY = 128MB
    QMAKE_LFLAGS += -sASYNCIFY=1 -lidbfs.js
}

INCLUDEPATH += ../source-gen
HEADERS += ../source-gen/boss_config.h
HEADERS += ../source-gen/boss_assets.hpp
HEADERS += ../source-gen/boss_buildtag.hpp

INCLUDEPATH += ../source
SOURCES += ../source/main.cpp
SOURCES += ../source/resource.cpp
HEADERS += ../source/resource.hpp
SOURCES += ../source/zayapp.cpp
HEADERS += ../source/zayapp.hpp
SOURCES += ../source/vdscript/vdchannel.cpp
HEADERS += ../source/vdscript/vdchannel.hpp
SOURCES += ../source/vdscript/vds_block.cpp
HEADERS += ../source/vdscript/vds_block.hpp
SOURCES += ../source/vdscript/vds_comment.cpp
HEADERS += ../source/vdscript/vds_comment.hpp
SOURCES += ../source/vdscript/vds_loop.cpp
HEADERS += ../source/vdscript/vds_loop.hpp
SOURCES += ../source/vdscript/vds_object.cpp
HEADERS += ../source/vdscript/vds_object.hpp
SOURCES += ../source/vdscript/vdscript.cpp
HEADERS += ../source/vdscript/vdscript.hpp
SOURCES += ../source/usb/visaapi.cpp
HEADERS += ../source/usb/visaapi.hpp

ASSETS_FONT.files += ../assets/font
ASSETS_FONT.path = /assets
ASSETS_IMAGE.files += ../assets/image
ASSETS_IMAGE.path = /assets
ASSETS_SCRIPT.files += ../assets/script
ASSETS_SCRIPT.path = /assets
ASSETS_WIDGET.files += ../assets/widget
ASSETS_WIDGET.path = /assets

win32{
    RC_ICONS += ../common/windows/main.ico
}

macx{
    QMAKE_BUNDLE_DATA += ASSETS_FONT
    QMAKE_BUNDLE_DATA += ASSETS_IMAGE
    QMAKE_BUNDLE_DATA += ASSETS_SCRIPT
    QMAKE_BUNDLE_DATA += ASSETS_WIDGET
    QMAKE_INFO_PLIST = $$PWD/../common/macx/Info.plist
    QMAKE_ASSET_CATALOGS += $$PWD/../common/macx/Assets.xcassets
    QMAKE_ASSET_CATALOGS_APP_ICON = AppIcon
}

ios{
    QMAKE_BUNDLE_DATA += ASSETS_FONT
    QMAKE_BUNDLE_DATA += ASSETS_IMAGE
    QMAKE_BUNDLE_DATA += ASSETS_SCRIPT
    QMAKE_BUNDLE_DATA += ASSETS_WIDGET
    QMAKE_INFO_PLIST = $$PWD/../common/ios/Info.plist
    QMAKE_ASSET_CATALOGS += $$PWD/../common/ios/Assets.xcassets
    QMAKE_ASSET_CATALOGS_APP_ICON = AppIcon
}

android{
    INSTALLS += ASSETS_FONT
    INSTALLS += ASSETS_IMAGE
    INSTALLS += ASSETS_SCRIPT
    INSTALLS += ASSETS_WIDGET
    ANDROID_PACKAGE_SOURCE_DIR = $$PWD/../common/android
}
