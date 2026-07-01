cmake_minimum_required(VERSION 3.24)

project(OpenTree VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Concurrent)
find_package(Qt6 QUIET COMPONENTS WebEngineWidgets WebChannel)

qt_standard_project_setup()

qt_add_executable(OpenTree
    src/main.cpp
    src/app/MainWindow.h
    src/app/MainWindow.cpp
    src/app/AppController.h
    src/app/AppController.cpp
    src/ui/TreePanel.h
    src/ui/TreePanel.cpp
    src/ui/DetailsPanel.h
    src/ui/DetailsPanel.cpp
    src/ui/ChartPanel.h
    src/ui/ChartPanel.cpp
    src/ui/ExtensionsPanel.h
    src/ui/ExtensionsPanel.cpp
    src/ui/GraphPanel.h
    src/ui/GraphPanel.cpp
    src/ui/HeatmapPanel.h
    src/ui/HeatmapPanel.cpp
    src/ui/ThemeManager.h
    src/ui/ThemeManager.cpp
    src/ui/SnapshotSettingsDialog.h
    src/ui/SnapshotSettingsDialog.cpp
    src/ui/SnapshotTimelineWidget.h
    src/ui/SnapshotTimelineWidget.cpp
    src/ui/SnapshotTrendWidget.h
    src/ui/SnapshotTrendWidget.cpp
    src/ui/TimelinePanel.h
    src/ui/TimelinePanel.cpp
    src/models/FolderTreeModel.h
    src/models/FolderTreeModel.cpp
    src/domain/ScanTypes.h
    src/services/ConfigService.h
    src/services/ConfigService.cpp
    src/services/SnapshotService.h
    src/services/SnapshotService.cpp
    src/services/ScanService.h
    src/services/ScanService.cpp
    src/data/DatabaseManager.h
    src/data/DatabaseManager.cpp
    src/data/FolderRepository.h
    src/data/FolderRepository.cpp
    src/data/FileRepository.h
    src/data/FileRepository.cpp
    src/integrations/EverythingClient.h
    src/integrations/EverythingClient.cpp
    src/utils/Logger.h
    src/utils/Logger.cpp
    src/utils/FileMetadataUtils.h
    src/utils/FileMetadataUtils.cpp
    src/utils/PathUtils.h
    src/utils/PathUtils.cpp
    src/utils/SizeFormatter.h
    src/utils/SizeFormatter.cpp
    src/utils/TaskSchedulerUtils.h
    src/utils/TaskSchedulerUtils.cpp
    resources/resources.qrc
)

if (WIN32)
    target_sources(OpenTree PRIVATE resources/windows/opentree.rc)
endif()

target_include_directories(OpenTree PRIVATE
    src
    third_party/include
)

target_link_libraries(OpenTree PRIVATE
    Qt6::Widgets
    Qt6::Sql
    Qt6::Concurrent
)

if (Qt6WebEngineWidgets_FOUND AND Qt6WebChannel_FOUND)
    target_link_libraries(OpenTree PRIVATE Qt6::WebEngineWidgets Qt6::WebChannel)
    target_compile_definitions(OpenTree PRIVATE OPENTREE_HAVE_WEBENGINE=1)
endif()

if (WIN32)
    target_link_libraries(OpenTree PRIVATE shell32)
    if (MINGW)
        target_link_options(OpenTree PRIVATE -mwindows)
    endif()
endif()

add_custom_command(TARGET OpenTree POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dll/Everything64.dll"
        "$<TARGET_FILE_DIR:OpenTree>/Everything64.dll"
)

if (WIN32)
    set(QT_WINDEPLOYQT_EXECUTABLE "${CMAKE_PREFIX_PATH}/bin/windeployqt.exe")
    if (EXISTS "${QT_WINDEPLOYQT_EXECUTABLE}")
        add_custom_command(TARGET OpenTree POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXECUTABLE}"
                --dir "$<TARGET_FILE_DIR:OpenTree>"
                --no-translations
                --no-opengl-sw
                "$<TARGET_FILE:OpenTree>"
            COMMENT "Deploying Qt runtime dependencies"
        )
    endif()
endif()
