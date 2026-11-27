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
    CONFIG(release, debug|release){
        QMAKE_CFLAGS_RELEASE -= -O2
        QMAKE_CFLAGS_RELEASE += -O3
        QMAKE_CXXFLAGS_RELEASE -= -O2
        QMAKE_CXXFLAGS_RELEASE += -O3
        QMAKE_LFLAGS_RELEASE -= -O2
        QMAKE_LFLAGS_RELEASE += -O3
    }
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
SOURCES += ../source/glue/zayglue.cpp
HEADERS += ../source/glue/zayglue.hpp
SOURCES += ../source/glue/glue_app_navigation.cpp
HEADERS += ../source/glue/glue_app_navigation.hpp
SOURCES += ../source/glue/glue_depth_scene.cpp
HEADERS += ../source/glue/glue_depth_scene.hpp
SOURCES += ../source/glue/glue_device_channel.cpp
HEADERS += ../source/glue/glue_device_channel.hpp
SOURCES += ../source/glue/glue_dom.cpp
HEADERS += ../source/glue/glue_dom.hpp
SOURCES += ../source/glue/glue_file.cpp
HEADERS += ../source/glue/glue_file.hpp
SOURCES += ../source/glue/glue_graph_and_log.cpp
HEADERS += ../source/glue/glue_graph_and_log.hpp
SOURCES += ../source/glue/glue_input.cpp
HEADERS += ../source/glue/glue_input.hpp
SOURCES += ../source/glue/glue_installation.cpp
HEADERS += ../source/glue/glue_installation.hpp
SOURCES += ../source/glue/glue_instrumentation.cpp
HEADERS += ../source/glue/glue_instrumentation.hpp
SOURCES += ../source/glue/glue_media.cpp
HEADERS += ../source/glue/glue_media.hpp
SOURCES += ../source/glue/glue_navigation_and_depth.cpp
HEADERS += ../source/glue/glue_navigation_and_depth.hpp
SOURCES += ../source/glue/glue_network.cpp
HEADERS += ../source/glue/glue_network.hpp
SOURCES += ../source/glue/glue_rendering.cpp
HEADERS += ../source/glue/glue_rendering.hpp
SOURCES += ../source/glue/glue_runtime.cpp
HEADERS += ../source/glue/glue_runtime.hpp
SOURCES += ../source/glue/glue_table.cpp
HEADERS += ../source/glue/glue_table.hpp
SOURCES += ../source/glue/glue_vds.cpp
HEADERS += ../source/glue/glue_vds.hpp
HEADERS += ../source/glue/glue_native_state.hpp
SOURCES += ../source/dependencytable.cpp
HEADERS += ../source/dependencytable.hpp
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
ASSETS_INDEX.files += ../assets/assetsindex.json
ASSETS_INDEX.path = /assets

win32{
    RC_ICONS += ../common/windows/main.ico
}

macx{
    QMAKE_BUNDLE_DATA += ASSETS_FONT
    QMAKE_BUNDLE_DATA += ASSETS_IMAGE
    QMAKE_BUNDLE_DATA += ASSETS_SCRIPT
    QMAKE_BUNDLE_DATA += ASSETS_WIDGET
    QMAKE_BUNDLE_DATA += ASSETS_INDEX
    QMAKE_INFO_PLIST = $$PWD/../common/macx/Info.plist
    QMAKE_ASSET_CATALOGS += $$PWD/../common/macx/Assets.xcassets
    QMAKE_ASSET_CATALOGS_APP_ICON = AppIcon
}

ios{
    QMAKE_BUNDLE_DATA += ASSETS_FONT
    QMAKE_BUNDLE_DATA += ASSETS_IMAGE
    QMAKE_BUNDLE_DATA += ASSETS_SCRIPT
    QMAKE_BUNDLE_DATA += ASSETS_WIDGET
    QMAKE_BUNDLE_DATA += ASSETS_INDEX
    QMAKE_INFO_PLIST = $$PWD/../common/ios/Info.plist
    QMAKE_ASSET_CATALOGS += $$PWD/../common/ios/Assets.xcassets
    QMAKE_ASSET_CATALOGS_APP_ICON = AppIcon
}

android{
    INSTALLS += ASSETS_FONT
    INSTALLS += ASSETS_IMAGE
    INSTALLS += ASSETS_SCRIPT
    INSTALLS += ASSETS_WIDGET
    INSTALLS += ASSETS_INDEX
    ANDROID_PACKAGE_SOURCE_DIR = $$PWD/../common/android
}
