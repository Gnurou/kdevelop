add_subdirectory(plasma)

########### next target ###############

find_package(SharedMimeInfo REQUIRED)
macro_optional_find_package(KDeclarative)

set(kdevelop_SRCS  main.cpp kdevideextension.cpp splash.cpp)
if(KDECLARATIVE_FOUND)
    set(kdevelop_SRCS ${kdevelop_SRCS}
                   welcomepage/sessionsmodel.cpp welcomepage/welcomepageview.cpp welcomepage/uihelper.cpp)
    add_definitions(-DWITH_WELCOMEPAGE)
	qt4_add_resources(kdevelop_SRCS welcomepage/welcomepage.qrc)
endif(KDECLARATIVE_FOUND)
if(APPLE)
    kde4_add_app_icon(kdevelop_SRCS "${KDE4_ICON_INSTALL_DIR}/oxygen/*/apps/kdevelop.png")
else()
    kde4_add_app_icon(kdevelop_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/../pics/hi*-app-kdevelop.png")
endif(APPLE)

kde4_add_executable(kdevelop ${kdevelop_SRCS} )

target_link_libraries(kdevelop
    ${KDE4_KDECORE_LIBS}
    ${KDEVPLATFORM_INTERFACES_LIBRARIES}
    ${KDEVPLATFORM_SHELL_LIBRARIES}
    ${KDE4_KIO_LIBS}
    ${KDE4_KCMUTILS_LIBS}
    ${KDE4_KPARTS_LIBS}
    ${KDE4_KNOTIFYCONFIG_LIBS}
    ${KDE4_KTEXTEDITOR_LIBS}
    ${KDE4_THREADWEAVER_LIBRARIES}

    ${QT_QTDECLARATIVE_LIBRARIES}
)
if(KDECLARATIVE_FOUND)
    target_link_libraries(kdevelop ${KDECLARATIVE_LIBRARY})
endif(KDECLARATIVE_FOUND)

install(TARGETS kdevelop ${INSTALL_TARGETS_DEFAULT_ARGS} )

install(FILES kdevelop! DESTINATION bin PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ WORLD_EXECUTE WORLD_READ)

if(APPLE)
    kde4_add_app_icon(kdevelop_bin_SRCS "${KDE4_ICON_INSTALL_DIR}/oxygen/*/apps/kdevelop.png")
else()
    kde4_add_app_icon(kdevelop_bin_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/../pics/hi*-app-kdevelop.png")
endif(APPLE)

if (APPLE)
    # the MacOSX bundle display name property (CFBundleDisplayName) is not currently supported by cmake,
    # so has to be set for all targets in this cmake file
    set(MACOSX_BUNDLE_DISPLAY_NAME KDevelop)
    # We provide our own Info.plist to disable AppNap for KDevelop:
    set_target_properties(kdevelop PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in)
    set_target_properties(kdevelop PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "org.kde.KDevelop")
    set_target_properties(kdevelop PROPERTIES MACOSX_BUNDLE_BUNDLE_NAME "KDevelop")
    set_target_properties(kdevelop PROPERTIES MACOSX_BUNDLE_DISPLAY_NAME "KDevelop")
    set_target_properties(kdevelop PROPERTIES MACOSX_BUNDLE_INFO_STRING "The KDevelop Integrated Development Environment")
    set_target_properties(kdevelop PROPERTIES MACOSX_BUNDLE_LONG_VERSION_STRING "KDevelop ${KDEVELOP_VERSION_MAJOR}.${KDEVELOP_VERSION_MINOR}")
    set_target_properties(kdevelop PROPERTIES MACOSX_BUNDLE_SHORT_VERSION_STRING "${KDEVELOP_VERSION_MAJOR}.${KDEVELOP_VERSION_MINOR}")
    set_target_properties(kdevelop PROPERTIES MACOSX_BUNDLE_BUNDLE_VERSION "${KDEVELOP_VERSION_MAJOR}.${KDEVELOP_VERSION_MINOR}")
    set_target_properties(kdevelop PROPERTIES MACOSX_BUNDLE_ICON_FILE "kdevelop_SRCS.icns")
    set_target_properties(kdevelop PROPERTIES MACOSX_BUNDLE_COPYRIGHT "KDevelop Team")
endif (APPLE)


########### install files ###############

install( PROGRAMS kdevelop.desktop  DESTINATION  ${XDG_APPS_INSTALL_DIR} )
install( PROGRAMS kdevelop_ps.desktop  DESTINATION  ${XDG_APPS_INSTALL_DIR} )
install( FILES kdeveloprc DESTINATION ${CONFIG_INSTALL_DIR} )
install( FILES kdevelopui.rc katepartui.rc kdevelop.notifyrc DESTINATION ${DATA_INSTALL_DIR}/kdevelop )
install( FILES kdevelop.xml DESTINATION ${XDG_MIME_INSTALL_DIR} )
install( FILES splash.qml DESTINATION ${DATA_INSTALL_DIR}/kdevelop )
update_xdg_mimetypes( ${XDG_MIME_INSTALL_DIR} )
